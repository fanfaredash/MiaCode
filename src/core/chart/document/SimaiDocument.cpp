#include "SimaiDocument.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

QString stripSingleTrailingLineBreak(QString value)
{
    if (value.endsWith("\r\n")) {
        value.chop(2);
        return value;
    }
    if (value.endsWith('\n') || value.endsWith('\r')) {
        value.chop(1);
    }
    return value;
}

bool parseDifficultySuffix(const QString& key, QString* prefix, int* id)
{
    if (prefix == nullptr || id == nullptr) {
        return false;
    }
    const int underscore = key.lastIndexOf('_');
    if (underscore <= 0 || underscore + 1 >= key.size()) {
        return false;
    }
    bool ok = false;
    const int value = key.mid(underscore + 1).toInt(&ok);
    if (!ok || !SimaiDocument::isDifficultyId(value)) {
        return false;
    }
    *prefix = key.left(underscore);
    *id = value;
    return true;
}

QString serializeField(const QString& key, const QString& value)
{
    return QString("&%1=%2").arg(key, value);
}

}  // namespace

SimaiDocument SimaiDocument::createEmpty()
{
    SimaiDocument doc;
    doc.title = QString();
    doc.artist = QString();
    doc.first = QString();
    doc.designer = QString();
    return doc;
}

SimaiDocument SimaiDocument::fromText(const QString& text)
{
    SimaiDocument doc = createEmpty();
    const QVector<SimaiRawField> fields = parseRawFields(text);
    if (fields.isEmpty()) {
        if (!text.trimmed().isEmpty()) {
            SimaiDifficultyData& master = doc.ensureDifficulty(5);
            master.chart = text;
        }
        return doc;
    }

    for (const SimaiRawField& field : fields) {
        if (field.key == "title") {
            doc.title = field.value;
            continue;
        }
        if (field.key == "artist") {
            doc.artist = field.value;
            continue;
        }
        if (field.key == "first") {
            doc.first = field.value;
            continue;
        }
        if (field.key == "des") {
            doc.designer = field.value;
            continue;
        }
        if (field.key == "video") {
            // Phase 4 — optional video-background path. Stored verbatim
            // (typically a relative filename next to the chart file);
            // path resolution to absolute filesystem location happens at
            // chart-load time in the calling code.
            doc.videoPath = field.value;
            continue;
        }

        QString prefix;
        int id = 0;
        if (parseDifficultySuffix(field.key, &prefix, &id)) {
            SimaiDifficultyData& difficultyData = doc.ensureDifficulty(id);
            if (prefix == "lv") {
                difficultyData.level = field.value;
                continue;
            }
            if (prefix == "des") {
                difficultyData.designer = field.value;
                continue;
            }
            if (prefix == "inote") {
                difficultyData.chart = field.value;
                continue;
            }
        }

        doc.extraFields.append(field);
    }

    return doc;
}

QVector<SimaiRawField> SimaiDocument::parseRawFields(const QString& text, bool prefixDummyIfNeeded)
{
    QVector<SimaiRawField> fields;
    QString source = text;
    if (source.trimmed().isEmpty()) {
        return fields;
    }
    if (prefixDummyIfNeeded) {
        const QRegularExpression startsWithField(R"(\A\s*&[^\r\n=]+=)");
        if (!startsWithField.match(source).hasMatch()) {
            source.prepend("&dummy=");
        }
    }

    const QRegularExpression headerRe(R"((?m)^&([^\r\n=]+)=)");
    QVector<QRegularExpressionMatch> matches;
    QRegularExpressionMatchIterator it = headerRe.globalMatch(source);
    while (it.hasNext()) {
        matches.append(it.next());
    }
    if (matches.isEmpty()) {
        return fields;
    }

    fields.reserve(matches.size());
    for (int i = 0; i < matches.size(); ++i) {
        const QRegularExpressionMatch& match = matches.at(i);
        const int valueStart = match.capturedEnd(0);
        const int valueEnd = (i + 1 < matches.size()) ? matches.at(i + 1).capturedStart(0) : source.size();
        SimaiRawField field;
        field.key = match.captured(1);
        field.value = stripSingleTrailingLineBreak(source.mid(valueStart, valueEnd - valueStart));
        fields.append(field);
    }

    return fields;
}

QString SimaiDocument::serializeRawFields(const QVector<SimaiRawField>& fields)
{
    QStringList blocks;
    blocks.reserve(fields.size());
    for (const SimaiRawField& field : fields) {
        if (field.key.isEmpty()) {
            continue;
        }
        blocks.append(serializeField(field.key, field.value));
    }
    return blocks.join('\n');
}

bool SimaiDocument::isDifficultyId(int id)
{
    return id >= 1 && id <= 7;
}

QString SimaiDocument::difficultyName(int id)
{
    switch (id) {
    case 1:
        return "Easy";
    case 2:
        return "Basic";
    case 3:
        return "Advanced";
    case 4:
        return "Expert";
    case 5:
        return "Master";
    case 6:
        return "Re:Master";
    case 7:
        return "Utage";
    default:
        return QString("Difficulty %1").arg(id);
    }
}

QString SimaiDocument::difficultyShortName(int id)
{
    switch (id) {
    case 1:
        return "ESY";
    case 2:
        return "BAS";
    case 3:
        return "ADV";
    case 4:
        return "EXP";
    case 5:
        return "MAS";
    case 6:
        return "REM";
    case 7:
        return "UTG";
    default:
        return QString("Difficulty %1").arg(id);
    }
}

QString SimaiDocument::toText() const
{
    QStringList blocks;
    blocks.reserve(3 + extraFields.size() + difficulties_.size() * 3);
    blocks.append(serializeField("title", title));
    blocks.append(serializeField("artist", artist));
    blocks.append(serializeField("first", first));
    if (!designer.isEmpty()) {
        blocks.append(serializeField("des", designer));
    }
    if (!videoPath.isEmpty()) {
        // Phase 4 — round-trip the video-background path so saving a
        // chart preserves the &video= field. Mirrors the placement of
        // the parser's `video` handler before the difficulty fields.
        blocks.append(serializeField("video", videoPath));
    }

    for (const SimaiRawField& field : extraFields) {
        if (field.key.isEmpty()) {
            continue;
        }
        blocks.append(serializeField(field.key, field.value));
    }

    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        const SimaiDifficultyData& difficultyData = it.value();
        const QString suffix = QString::number(difficultyData.id);
        blocks.append(serializeField("lv_" + suffix, difficultyData.level));
        blocks.append(serializeField("des_" + suffix, difficultyData.designer));
        blocks.append(serializeField("inote_" + suffix, difficultyData.chart));
    }

    return blocks.join('\n');
}

QVector<int> SimaiDocument::difficultyIds() const
{
    QVector<int> ids;
    ids.reserve(difficulties_.size());
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        ids.append(it.key());
    }
    return ids;
}

SimaiDifficultyData* SimaiDocument::difficulty(int id)
{
    auto it = difficulties_.find(id);
    if (it == difficulties_.end()) {
        return nullptr;
    }
    return &it.value();
}

const SimaiDifficultyData* SimaiDocument::difficulty(int id) const
{
    auto it = difficulties_.constFind(id);
    if (it == difficulties_.cend()) {
        return nullptr;
    }
    return &it.value();
}

SimaiDifficultyData& SimaiDocument::ensureDifficulty(int id)
{
    SimaiDifficultyData& difficultyData = difficulties_[id];
    difficultyData.id = id;
    return difficultyData;
}

void SimaiDocument::removeDifficulty(int id)
{
    difficulties_.remove(id);
}

bool SimaiDocument::inferUnifiedDesignerDefault() const
{
    // Collect distinct non-empty per-difficulty designer names.
    QVector<QString> perDiff;
    perDiff.reserve(difficulties_.size());
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        const QString& d = it.value().designer;
        if (d.isEmpty()) {
            continue;
        }
        if (!perDiff.contains(d)) {
            perDiff.append(d);
        }
    }

    // No per-diff designers at all → safely default ON; the user just hasn't
    // filled them out yet, and the top &des can broadcast freely.
    if (perDiff.isEmpty()) {
        return true;
    }

    // Multiple distinct per-diff designers → clearly authored separately,
    // default OFF so nothing gets clobbered without explicit consent.
    if (perDiff.size() > 1) {
        return false;
    }

    // Exactly one per-diff designer present. If the top &des is empty or
    // differs from it, that's still a signal that the project was authored
    // with per-diff designers in mind → default OFF.
    const QString sole = perDiff.first();
    if (designer.isEmpty() || designer != sole) {
        return false;
    }

    // Top &des matches the sole per-diff designer (and any empty ones are
    // unset placeholders) → behaviorally equivalent to "unified", default ON.
    return true;
}
