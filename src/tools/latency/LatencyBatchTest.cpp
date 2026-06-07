// latency_offset_batch — offline batch evaluation of the chart offset detector.
//
// Walks a root directory, finds every chart project under it (a directory that
// holds a `maidata.txt` plus a `track.*` audio file), then for each project:
//   1. parses the chart's declared BPM (&wholebpm/&bpm/inline) and `&first` (s),
//   2. decodes the audio and builds the onset + transient envelopes,
//   3. runs latency_analysis::detectOffset() to estimate the offset,
//   4. compares the estimate against the declared `&first`, folding the
//      difference modulo one 8th-note: a gap that is an integer number of
//      8th-notes counts as zero error (per the spec).
//
// Two modes:
//   * single-run   — one DetectionTuning config; prints per-project rows + a
//                    summary; optional per-project CSV.
//   * sweep        — one or more `--sweep name:start:stop:step` flags define a
//                    parameter grid; each project is decoded ONCE and its
//                    envelopes reused across every combo (onset envelope is
//                    rebuilt per combo only when an onset-* param is swept), so
//                    a coordinate-descent sweep does not re-decode the corpus.
//                    Prints a combo table sorted by pass%; optional combo CSV.
//
// All envelope-mix / phase-penalty weights map 1:1 onto DetectionTuning. This
// is a manual diagnostic (needs a real corpus), built behind
// MIACODE_BUILD_DEV_TOOLS but not registered with CTest.

#include "LatencyAnalysis.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace la = miacode::latency_analysis;

namespace {

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

// Read a numeric header field (`&first=...`, `&bpm=...`) out of a maidata.txt.
// The file may be GBK- or UTF-8-encoded; the header fields are pure ASCII, so
// we scan a Latin-1 view (every byte maps) to stay encoding-agnostic. Returns
// the first match. `found` is set false when the key is absent.
double parseMaidataNumber(const QString& latin1Text, const QString& key, bool* found)
{
    if (found != nullptr) {
        *found = false;
    }
    const QRegularExpression re(
        QStringLiteral("&%1\\s*=\\s*([-+]?\\d+(?:\\.\\d+)?)").arg(key),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(latin1Text);
    if (!match.hasMatch()) {
        return 0.0;
    }
    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok) {
        return 0.0;
    }
    if (found != nullptr) {
        *found = true;
    }
    return value;
}

// Resolve the chart's BPM for offset evaluation — always from the chart file,
// never from BPM detection. The `&first` offset is defined against the beat
// grid at the start of the chart, so the ground-truth BPM is the chart's FIRST
// declared tempo. Priority: `&wholebpm=` (MiaCode's cached value, normally ==
// the first inline BPM) → `&bpm=` (some hand-authored simai) → the first inline
// `(NNN)` token in the chart body. The inline search is scoped to after the
// first `&inote` marker so a parenthesised number in a title/artist field can
// never be mistaken for the BPM.
double parseChartBpm(const QString& latin1Text, bool* found)
{
    double value = parseMaidataNumber(latin1Text, QStringLiteral("wholebpm"), found);
    if (found != nullptr && *found) {
        return value;
    }
    value = parseMaidataNumber(latin1Text, QStringLiteral("bpm"), found);
    if (found != nullptr && *found) {
        return value;
    }
    int bodyStart = latin1Text.indexOf(QStringLiteral("&inote"), 0, Qt::CaseInsensitive);
    if (bodyStart < 0) {
        bodyStart = 0;  // no inote marker — fall back to scanning the whole text
    }
    const QString body = latin1Text.mid(bodyStart);
    static const QRegularExpression inlineBpm(QStringLiteral("\\(\\s*(\\d+(?:\\.\\d+)?)\\s*\\)"));
    const QRegularExpressionMatch m = inlineBpm.match(body);
    if (m.hasMatch()) {
        bool ok = false;
        const double inline_ = m.captured(1).toDouble(&ok);
        if (ok) {
            if (found != nullptr) {
                *found = true;
            }
            return inline_;
        }
    }
    if (found != nullptr) {
        *found = false;
    }
    return 0.0;
}

// Locate the playable audio file in a project directory. Prefers track.mp3,
// then other common containers; ignores *_bak backups MiaCode leaves behind.
QString findTrackAudio(const QDir& dir)
{
    static const QStringList kPreferred{
        QStringLiteral("track.mp3"),
        QStringLiteral("track.ogg"),
        QStringLiteral("track.wav"),
        QStringLiteral("track.flac"),
        QStringLiteral("track.m4a"),
        QStringLiteral("track.aac"),
    };
    for (const QString& name : kPreferred) {
        if (dir.exists(name)) {
            return dir.filePath(name);
        }
    }
    const QStringList matches = dir.entryList({QStringLiteral("track.*")}, QDir::Files);
    for (const QString& name : matches) {
        if (name.contains(QStringLiteral("_bak"), Qt::CaseInsensitive)) {
            continue;
        }
        return dir.filePath(name);
    }
    return QString();
}

double foldResidualMs(double errorSeconds, double eighthPeriodSeconds)
{
    if (eighthPeriodSeconds <= 0.0) {
        return errorSeconds * 1000.0;
    }
    const double steps = std::round(errorSeconds / eighthPeriodSeconds);
    const double folded = errorSeconds - steps * eighthPeriodSeconds;
    return folded * 1000.0;
}

double percentile(QVector<double> values, double fraction)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double pos = fraction * static_cast<double>(values.size() - 1);
    const int lo = static_cast<int>(std::floor(pos));
    const int hi = std::min(lo + 1, static_cast<int>(values.size()) - 1);
    const double t = pos - static_cast<double>(lo);
    return values.at(lo) * (1.0 - t) + values.at(hi) * t;
}

double meanOf(const QVector<double>& values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return sum / static_cast<double>(values.size());
}

// ---- DetectionTuning parameter registry (CLI name <-> struct field) ----

bool isOnsetParam(const QString& name)
{
    return name == QStringLiteral("onset-rms")
        || name == QStringLiteral("onset-meanabs")
        || name == QStringLiteral("onset-smooth");
}

// Apply a single named param to a tuning struct. Returns false on unknown name.
bool applyParam(la::DetectionTuning* tuning, const QString& name, double value)
{
    if (name == QStringLiteral("transient-weight")) { tuning->offsetTransientWeight = value; return true; }
    if (name == QStringLiteral("onset-weight"))     { tuning->offsetOnsetWeight = value; return true; }
    if (name == QStringLiteral("phase-penalty"))    { tuning->offsetPhasePenalty = value; return true; }
    if (name == QStringLiteral("meter-penalty"))    { tuning->offsetMeterPenalty = value; return true; }
    if (name == QStringLiteral("snap-threshold"))   { tuning->offsetSnapThreshold = value; return true; }
    if (name == QStringLiteral("offset-edge-weight")){ tuning->offsetEdgeWeight = value; return true; }
    if (name == QStringLiteral("onset-rms"))        { tuning->onsetRmsWeight = value; return true; }
    if (name == QStringLiteral("onset-meanabs"))    { tuning->onsetMeanAbsWeight = value; return true; }
    if (name == QStringLiteral("onset-smooth"))     { tuning->onsetBaselineDecay = value; return true; }
    return false;
}

// ---- Per-project decoded audio + parsed chart fields (decoded once) ----

struct ProjectAudio {
    QString name;
    bool decoded = false;     // audio decoded OK
    bool hasBpm = false;
    double declaredBpm = 0.0;
    double declaredFirst = 0.0;
    QVector<float> samples;
    int sampleRate = 0;
    double duration = 0.0;
    QString note;             // skip reason when !decoded
};

ProjectAudio decodeProject(const QDir& dir)
{
    ProjectAudio p;
    p.name = dir.dirName();

    QFile maidata(dir.filePath(QStringLiteral("maidata.txt")));
    if (!maidata.open(QIODevice::ReadOnly)) {
        p.note = QStringLiteral("cannot open maidata.txt");
        return p;
    }
    const QString text = QString::fromLatin1(maidata.readAll());
    maidata.close();

    p.declaredBpm = parseChartBpm(text, &p.hasBpm);
    bool hasFirst = false;
    p.declaredFirst = parseMaidataNumber(text, QStringLiteral("first"), &hasFirst);

    const QString trackPath = findTrackAudio(dir);
    if (trackPath.isEmpty()) {
        p.note = QStringLiteral("no track audio");
        return p;
    }
    la::DecodedAudio decoded = la::decodeMonoTrack(trackPath);
    if (decoded.samples.isEmpty() || decoded.sampleRate <= 0) {
        p.note = QStringLiteral("decode failed");
        return p;
    }
    p.samples = std::move(decoded.samples);
    p.sampleRate = decoded.sampleRate;
    p.duration = decoded.durationSeconds;
    p.decoded = true;
    return p;
}

// One scored project under a given tuning. residual == |folded error| in ms.
struct Eval {
    bool ok = false;
    double bpm = 0.0;
    double declaredFirstMs = 0.0;
    double detectedMs = 0.0;
    double rawErrorMs = 0.0;
    double residualMs = 0.0;
    bool pass = false;
    QString note;
};

// Score a project given prebuilt envelopes + a BPM. Pure scoring, no decode.
Eval scoreWithEnvelopes(
    const ProjectAudio& p,
    const la::Envelope& onset,
    const la::Envelope& transient,
    double bpm,
    const la::DetectionTuning& tuning,
    const QString& snapMode,
    double toleranceMs)
{
    Eval e;
    e.bpm = bpm;
    e.declaredFirstMs = p.declaredFirst * 1000.0;
    if (!(bpm > 0.0)) {
        e.note = QStringLiteral("no bpm");
        return e;
    }
    la::OffsetDetectionInputs inputs;
    inputs.bpm = bpm;
    inputs.offsetAnchorSeconds = 0.0;  // unbiased — do not anchor to declared
    inputs.trackDurationSeconds = p.duration;
    inputs.meterId = QStringLiteral("4/4");
    inputs.snapMode = snapMode;
    inputs.hasLastDetectedMeterPhase = false;
    inputs.lastDetectedMeterId = QStringLiteral("4/4");

    const double detected = la::detectOffset(onset, transient, inputs, tuning);
    const double beatPeriod = 60.0 / bpm;
    const double eighthPeriod = beatPeriod * 0.5;
    const double errorSeconds = detected - p.declaredFirst;

    e.detectedMs = detected * 1000.0;
    e.rawErrorMs = errorSeconds * 1000.0;
    e.residualMs = std::abs(foldResidualMs(errorSeconds, eighthPeriod));
    e.pass = e.residualMs <= toleranceMs;
    e.ok = true;
    return e;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("latency_offset_batch"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Batch-evaluate chart offset detection against each chart's &first "
                       "(error folded modulo one 8th-note). Supports parameter sweeps."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("root"),
        QStringLiteral("Root directory scanned recursively for chart projects."));

    const auto addValue = [&parser](const QString& name, const QString& desc, const QString& def) {
        parser.addOption(QCommandLineOption(name, desc, name, def));
    };

    addValue(QStringLiteral("bpm-source"),
        QStringLiteral("BPM source: chart (use &bpm) or detect (estimate from audio)."),
        QStringLiteral("chart"));
    addValue(QStringLiteral("snap-mode"),
        QStringLiteral("Offset snap granularity: bar | quarter | eighth."),
        QStringLiteral("eighth"));
    addValue(QStringLiteral("tolerance-ms"),
        QStringLiteral("Pass threshold for the folded residual, in milliseconds."),
        QStringLiteral("10"));
    addValue(QStringLiteral("limit"),
        QStringLiteral("Process at most N projects (0 = no limit)."),
        QStringLiteral("0"));
    addValue(QStringLiteral("csv"),
        QStringLiteral("Write a CSV report (per-project in single mode, per-combo in sweep mode)."),
        QString());

    // --- DetectionTuning knobs (base values; a sweep overrides per combo) ---
    const la::DetectionTuning def;
    addValue(QStringLiteral("transient-weight"),
        QStringLiteral("Offset score weight on the transient envelope."),
        QString::number(def.offsetTransientWeight));
    addValue(QStringLiteral("onset-weight"),
        QStringLiteral("Offset score weight on the onset envelope."),
        QString::number(def.offsetOnsetWeight));
    addValue(QStringLiteral("phase-penalty"),
        QStringLiteral("Penalty per second of |phase| (bias toward small offsets)."),
        QString::number(def.offsetPhasePenalty));
    addValue(QStringLiteral("meter-penalty"),
        QStringLiteral("Penalty for distance to the meter phase (bar mode)."),
        QString::number(def.offsetMeterPenalty));
    addValue(QStringLiteral("snap-threshold"),
        QStringLiteral("Snap acceptance ratio vs best score; >1 disables snapping."),
        QString::number(def.offsetSnapThreshold));
    addValue(QStringLiteral("offset-edge-weight"),
        QStringLiteral("Phase 1: rising-edge emphasis blend (0=off, raw transient envelope)."),
        QString::number(def.offsetEdgeWeight));
    addValue(QStringLiteral("onset-rms"),
        QStringLiteral("Onset energy weight on RMS."),
        QString::number(def.onsetRmsWeight));
    addValue(QStringLiteral("onset-meanabs"),
        QStringLiteral("Onset energy weight on mean-abs."),
        QString::number(def.onsetMeanAbsWeight));
    addValue(QStringLiteral("onset-smooth"),
        QStringLiteral("Onset IIR baseline decay (0..1)."),
        QString::number(def.onsetBaselineDecay));

    parser.addOption(QCommandLineOption(QStringLiteral("quiet"),
        QStringLiteral("Single mode: only print the summary, not per-project rows.")));
    parser.addOption(QCommandLineOption(QStringLiteral("sweep"),
        QStringLiteral("Add a sweep dimension 'name:start:stop:step' (repeatable). "
                       "Enables sweep mode (Cartesian grid over all dimensions)."),
        QStringLiteral("spec")));

    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        err() << "error: a root directory is required\n" << parser.helpText();
        err().flush();
        return 2;
    }
    const QString rootPath = QDir::cleanPath(positional.first());
    if (!QFileInfo(rootPath).isDir()) {
        err() << "error: not a directory: " << rootPath << "\n";
        err().flush();
        return 2;
    }

    const QString bpmSource = parser.value(QStringLiteral("bpm-source")).trimmed().toLower();
    const bool detectBpm = (bpmSource == QStringLiteral("detect"));
    const QString snapMode = parser.value(QStringLiteral("snap-mode")).trimmed().toLower();
    const double toleranceMs = parser.value(QStringLiteral("tolerance-ms")).toDouble();
    const int limit = parser.value(QStringLiteral("limit")).toInt();
    const QString csvPath = parser.value(QStringLiteral("csv")).trimmed();
    const bool quiet = parser.isSet(QStringLiteral("quiet"));

    // Base tuning from the scalar flags.
    la::DetectionTuning baseTuning;
    baseTuning.offsetTransientWeight = parser.value(QStringLiteral("transient-weight")).toDouble();
    baseTuning.offsetOnsetWeight = parser.value(QStringLiteral("onset-weight")).toDouble();
    baseTuning.offsetPhasePenalty = parser.value(QStringLiteral("phase-penalty")).toDouble();
    baseTuning.offsetMeterPenalty = parser.value(QStringLiteral("meter-penalty")).toDouble();
    baseTuning.offsetSnapThreshold = parser.value(QStringLiteral("snap-threshold")).toDouble();
    baseTuning.offsetEdgeWeight = parser.value(QStringLiteral("offset-edge-weight")).toDouble();
    baseTuning.onsetRmsWeight = parser.value(QStringLiteral("onset-rms")).toDouble();
    baseTuning.onsetMeanAbsWeight = parser.value(QStringLiteral("onset-meanabs")).toDouble();
    baseTuning.onsetBaselineDecay = parser.value(QStringLiteral("onset-smooth")).toDouble();

    // --- Parse sweep dimensions ---
    struct SweepDim {
        QString name;
        QVector<double> values;
    };
    QVector<SweepDim> sweepDims;
    bool sweepTouchesOnset = false;
    const QStringList sweepSpecs = parser.values(QStringLiteral("sweep"));
    for (const QString& spec : sweepSpecs) {
        const QStringList parts = spec.split(QLatin1Char(':'));
        if (parts.size() != 4) {
            err() << "error: --sweep expects name:start:stop:step, got '" << spec << "'\n";
            err().flush();
            return 2;
        }
        const QString name = parts.at(0).trimmed();
        la::DetectionTuning probe;
        if (!applyParam(&probe, name, 0.0)) {
            err() << "error: unknown sweep param '" << name << "'\n";
            err().flush();
            return 2;
        }
        bool a = false, b = false, c = false;
        const double start = parts.at(1).toDouble(&a);
        const double stop = parts.at(2).toDouble(&b);
        const double step = parts.at(3).toDouble(&c);
        if (!a || !b || !c || step <= 0.0 || stop < start) {
            err() << "error: bad sweep range in '" << spec << "'\n";
            err().flush();
            return 2;
        }
        SweepDim dim;
        dim.name = name;
        const int n = static_cast<int>(std::round((stop - start) / step)) + 1;
        for (int i = 0; i < n; ++i) {
            dim.values.append(start + static_cast<double>(i) * step);
        }
        sweepDims.append(dim);
        if (isOnsetParam(name)) {
            sweepTouchesOnset = true;
        }
    }
    const bool sweepMode = !sweepDims.isEmpty();

    // --- Build the combo list (Cartesian product of sweep dimensions) ---
    struct Combo {
        la::DetectionTuning tuning;
        QString label;
    };
    QVector<Combo> combos;
    if (sweepMode) {
        QVector<int> idx(sweepDims.size(), 0);
        while (true) {
            Combo combo;
            combo.tuning = baseTuning;
            QStringList labelParts;
            for (int d = 0; d < sweepDims.size(); ++d) {
                const double v = sweepDims.at(d).values.at(idx.at(d));
                applyParam(&combo.tuning, sweepDims.at(d).name, v);
                labelParts.append(QStringLiteral("%1=%2").arg(sweepDims.at(d).name).arg(v, 0, 'g', 4));
            }
            combo.label = labelParts.join(QLatin1Char(' '));
            combos.append(combo);

            int d = sweepDims.size() - 1;
            for (; d >= 0; --d) {
                if (++idx[d] < sweepDims.at(d).values.size()) {
                    break;
                }
                idx[d] = 0;
            }
            if (d < 0) {
                break;
            }
        }
    } else {
        combos.append({baseTuning, QStringLiteral("(base)")});
    }

    // --- Discover project directories ---
    QStringList projectDirs;
    {
        QDirIterator it(rootPath, QStringList{QStringLiteral("maidata.txt")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString dirPath = it.fileInfo().absolutePath();
            if (dirPath.contains(QStringLiteral("/.miacode/"), Qt::CaseInsensitive)) {
                continue;
            }
            projectDirs.append(dirPath);
        }
    }
    projectDirs.removeDuplicates();
    projectDirs.sort(Qt::CaseInsensitive);
    if (limit > 0 && projectDirs.size() > limit) {
        projectDirs = projectDirs.mid(0, limit);
    }

    out() << "latency_offset_batch\n";
    out() << "  root          : " << rootPath << "\n";
    out() << "  projects found: " << projectDirs.size() << "\n";
    out() << "  bpm-source    : " << (detectBpm ? "detect" : "chart") << "\n";
    out() << "  snap-mode     : " << snapMode << "\n";
    out() << "  tolerance     : " << QString::number(toleranceMs, 'f', 2) << " ms\n";
    if (sweepMode) {
        out() << "  mode          : SWEEP  (" << combos.size() << " combos, onset "
              << (sweepTouchesOnset ? "rebuilt per combo" : "cached once") << ")\n";
        QStringList dimNames;
        for (const SweepDim& d : sweepDims) {
            dimNames.append(QStringLiteral("%1[%2]").arg(d.name).arg(d.values.size()));
        }
        out() << "  sweep dims    : " << dimNames.join(QStringLiteral(" x ")) << "\n";
    } else {
        out() << "  mode          : single\n";
    }
    out() << "\n";
    out().flush();

    // ====================================================================
    // SINGLE MODE
    // ====================================================================
    if (!sweepMode) {
        struct Row { Eval e; QString name; };
        QVector<Row> rows;
        for (const QString& projectDir : projectDirs) {
            const QDir dir(projectDir);
            ProjectAudio p = decodeProject(dir);
            Row row;
            row.name = p.name;
            if (!p.decoded) {
                row.e.note = p.note;
                rows.append(row);
                continue;
            }
            const la::Envelope onset = la::buildOnsetEnvelope(p.samples, p.sampleRate, baseTuning);
            const la::Envelope transient = la::buildTransientEnvelope(p.samples, p.sampleRate);
            double bpm = p.declaredBpm;
            if (detectBpm) {
                const la::BpmDetectionResult r = la::detectBpm(onset);
                bpm = (r.bpm > 0.0) ? r.bpm : 0.0;
            }
            if (!(bpm > 0.0)) {
                row.e.note = detectBpm ? QStringLiteral("bpm not detected")
                                       : QStringLiteral("missing &bpm");
                rows.append(row);
                continue;
            }
            row.e = scoreWithEnvelopes(p, onset, transient, bpm, baseTuning, snapMode, toleranceMs);
            rows.append(row);
            if (!quiet) {
                out() << QStringLiteral("%1  %2  bpm=%3  first=%4ms  det=%5ms  rawErr=%6ms  resid=%7ms\n")
                             .arg(row.e.pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                             .arg(row.name.leftJustified(28, ' ', true))
                             .arg(QString::number(bpm, 'f', 2), 7)
                             .arg(QString::number(row.e.declaredFirstMs, 'f', 1), 8)
                             .arg(QString::number(row.e.detectedMs, 'f', 1), 8)
                             .arg(QString::number(row.e.rawErrorMs, 'f', 1), 8)
                             .arg(QString::number(row.e.residualMs, 'f', 1), 7);
                out().flush();
            }
        }

        QVector<double> residuals;
        int okCount = 0, passCount = 0, skipped = 0;
        for (const Row& row : rows) {
            if (!row.e.ok) { ++skipped; continue; }
            ++okCount;
            if (row.e.pass) ++passCount;
            residuals.append(row.e.residualMs);
        }
        out() << "\n===== summary =====\n";
        out() << "  evaluated : " << okCount << " / " << rows.size() << "  (skipped " << skipped << ")\n";
        if (okCount > 0) {
            out() << "  pass      : " << passCount << " / " << okCount << "  ("
                  << QString::number(100.0 * passCount / okCount, 'f', 1) << "%)  @ tol="
                  << QString::number(toleranceMs, 'f', 1) << "ms\n";
            out() << "  residual  : mean=" << QString::number(meanOf(residuals), 'f', 2)
                  << "ms  median=" << QString::number(percentile(residuals, 0.50), 'f', 2)
                  << "ms  p90=" << QString::number(percentile(residuals, 0.90), 'f', 2)
                  << "ms  max=" << QString::number(percentile(residuals, 1.0), 'f', 2) << "ms\n";
        }
        out().flush();

        if (!csvPath.isEmpty()) {
            QFile csv(csvPath);
            if (csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream cs(&csv);
                cs << "project,bpm,declared_first_ms,detected_ms,raw_error_ms,residual_ms,pass,note\n";
                for (const Row& row : rows) {
                    cs << '"' << row.name << "\","
                       << QString::number(row.e.bpm, 'f', 3) << ','
                       << QString::number(row.e.declaredFirstMs, 'f', 3) << ','
                       << QString::number(row.e.detectedMs, 'f', 3) << ','
                       << QString::number(row.e.rawErrorMs, 'f', 3) << ','
                       << QString::number(row.e.residualMs, 'f', 3) << ','
                       << (row.e.ok ? (row.e.pass ? "1" : "0") : "")
                       << ",\"" << row.e.note << "\"\n";
                }
                cs.flush();
                out() << "  csv       : " << csvPath << "\n";
                out().flush();
            } else {
                err() << "error: cannot write CSV: " << csvPath << "\n";
                err().flush();
            }
        }
        return 0;
    }

    // ====================================================================
    // SWEEP MODE — decode each project once, evaluate every combo.
    // ====================================================================
    struct Accum {
        int eval = 0;
        int pass = 0;
        QVector<double> residuals;
    };
    QVector<Accum> acc(combos.size());
    int evaluatedProjects = 0;
    int skippedProjects = 0;

    for (const QString& projectDir : projectDirs) {
        const QDir dir(projectDir);
        ProjectAudio p = decodeProject(dir);
        if (!p.decoded) { ++skippedProjects; continue; }
        if (!detectBpm && !(p.declaredBpm > 0.0)) { ++skippedProjects; continue; }

        // Transient envelope has no tunable params — build once per project.
        const la::Envelope transient = la::buildTransientEnvelope(p.samples, p.sampleRate);

        // Onset envelope: build once with base params unless an onset-* dim is
        // swept (then it must be rebuilt per combo).
        la::Envelope onsetBase;
        double bpmBase = p.declaredBpm;
        if (!sweepTouchesOnset) {
            onsetBase = la::buildOnsetEnvelope(p.samples, p.sampleRate, baseTuning);
            if (detectBpm) {
                const la::BpmDetectionResult r = la::detectBpm(onsetBase);
                bpmBase = (r.bpm > 0.0) ? r.bpm : 0.0;
            }
        }

        bool projectCounted = false;
        for (int c = 0; c < combos.size(); ++c) {
            la::Envelope onset = onsetBase;
            double bpm = bpmBase;
            if (sweepTouchesOnset) {
                onset = la::buildOnsetEnvelope(p.samples, p.sampleRate, combos.at(c).tuning);
                bpm = p.declaredBpm;
                if (detectBpm) {
                    const la::BpmDetectionResult r = la::detectBpm(onset);
                    bpm = (r.bpm > 0.0) ? r.bpm : 0.0;
                }
            }
            if (!(bpm > 0.0)) {
                continue;  // can't score; treated as skip (consistent across combos)
            }
            const Eval e = scoreWithEnvelopes(
                p, onset, transient, bpm, combos.at(c).tuning, snapMode, toleranceMs);
            if (!e.ok) {
                continue;
            }
            acc[c].eval += 1;
            if (e.pass) {
                acc[c].pass += 1;
            }
            acc[c].residuals.append(e.residualMs);
            projectCounted = true;
        }
        if (projectCounted) {
            ++evaluatedProjects;
        } else {
            ++skippedProjects;
        }
    }

    // Rank combos by pass% desc, then mean residual asc.
    struct ComboResult {
        int index;
        double passPct;
        double mean;
        double median;
        double p90;
    };
    QVector<ComboResult> results;
    results.reserve(combos.size());
    for (int c = 0; c < combos.size(); ++c) {
        ComboResult r;
        r.index = c;
        r.passPct = acc.at(c).eval > 0 ? 100.0 * acc.at(c).pass / acc.at(c).eval : 0.0;
        r.mean = meanOf(acc.at(c).residuals);
        r.median = percentile(acc.at(c).residuals, 0.50);
        r.p90 = percentile(acc.at(c).residuals, 0.90);
        results.append(r);
    }
    std::stable_sort(results.begin(), results.end(), [](const ComboResult& a, const ComboResult& b) {
        if (std::abs(a.passPct - b.passPct) > 1e-9) {
            return a.passPct > b.passPct;
        }
        return a.mean < b.mean;
    });

    out() << "evaluated " << evaluatedProjects << " projects (skipped " << skippedProjects << ")\n";
    out() << "combos ranked by pass% (then mean residual):\n\n";
    out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                 .arg(QStringLiteral("pass"), 5)
                 .arg(QStringLiteral("pass%"), 6)
                 .arg(QStringLiteral("mean"), 7)
                 .arg(QStringLiteral("median"), 7)
                 .arg(QStringLiteral("p90"), 7)
                 .arg(QStringLiteral("combo"));
    for (const ComboResult& r : results) {
        const Accum& a = acc.at(r.index);
        out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                     .arg(QStringLiteral("%1/%2").arg(a.pass).arg(a.eval), 5)
                     .arg(QString::number(r.passPct, 'f', 1), 6)
                     .arg(QString::number(r.mean, 'f', 2), 7)
                     .arg(QString::number(r.median, 'f', 2), 7)
                     .arg(QString::number(r.p90, 'f', 2), 7)
                     .arg(combos.at(r.index).label);
    }
    if (!results.isEmpty()) {
        const ComboResult& best = results.first();
        out() << "\nbest: " << combos.at(best.index).label
              << "  pass%=" << QString::number(best.passPct, 'f', 1)
              << "  mean=" << QString::number(best.mean, 'f', 2) << "ms\n";
    }
    out().flush();

    if (!csvPath.isEmpty()) {
        QFile csv(csvPath);
        if (csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream cs(&csv);
            cs << "combo,pass,eval,pass_pct,mean_ms,median_ms,p90_ms\n";
            for (const ComboResult& r : results) {
                const Accum& a = acc.at(r.index);
                cs << '"' << combos.at(r.index).label << "\","
                   << a.pass << ',' << a.eval << ','
                   << QString::number(r.passPct, 'f', 2) << ','
                   << QString::number(r.mean, 'f', 3) << ','
                   << QString::number(r.median, 'f', 3) << ','
                   << QString::number(r.p90, 'f', 3) << "\n";
            }
            cs.flush();
            out() << "csv: " << csvPath << "\n";
            out().flush();
        } else {
            err() << "error: cannot write CSV: " << csvPath << "\n";
            err().flush();
        }
    }
    return 0;
}
