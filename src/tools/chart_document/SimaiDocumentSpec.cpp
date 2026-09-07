// Unit spec for SimaiDocument's per-difficulty designer model, focused on the
// "standalone designer" feature: a `&des_N` may exist for a slot that has no
// chart, and it must round-trip without materializing a phantom empty
// difficulty (no `&lv_N` / `&inote_N`, not listed in difficultyIds()).
//
// Run via CTest (registered as simai_document_spec) or standalone.

#include "SimaiDocument.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

namespace {

void expectTrue(bool condition, const QString& message, int* failed, QTextStream& out)
{
    if (condition) {
        out << "[PASS] " << message << '\n';
        return;
    }
    out << "[FAIL] " << message << '\n';
    ++(*failed);
}

void expectEqual(const QString& actual, const QString& expected, const QString& message, int* failed, QTextStream& out)
{
    if (actual == expected) {
        out << "[PASS] " << message << '\n';
        return;
    }
    out << "[FAIL] " << message << '\n';
    out << "  expected: [" << expected << "]\n";
    out << "  actual:   [" << actual << "]\n";
    ++(*failed);
}

// True when text contains a `&des_N=value` line (exact, on its own line).
bool hasDesLine(const QString& text, int id, const QString& value)
{
    return text.split('\n').contains(QStringLiteral("&des_%1=%2").arg(id).arg(value));
}

bool hasKeyLine(const QString& text, const QString& key)
{
    for (const QString& line : text.split('\n')) {
        if (line.startsWith(QStringLiteral("&%1=").arg(key))) {
            return true;
        }
    }
    return false;
}

// True when `text` contains `exact` as a whole line (used to assert an exact
// `&key=value`, e.g. distinguishing `&first=0` from a bare `&first=`).
bool hasLine(const QString& text, const QString& exact)
{
    return text.split('\n').contains(exact);
}

void runSpecs(QTextStream& out, int* failed)
{
    // --- A real charted difficulty keeps its designer and round-trips. ---
    {
        const QString src =
            "&title=Song\n&first=0\n&des_5=Alice\n&lv_5=12\n&inote_5=(120){1}1,\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectTrue(doc.difficulty(5) != nullptr, "charted des_5 stays a real difficulty", failed, out);
        expectTrue(doc.standaloneDesignerIds().isEmpty(), "charted des_5 is not a standalone", failed, out);
        expectEqual(doc.designerForSlot(5), "Alice", "designerForSlot reads the charted designer", failed, out);
        const QString round = doc.toText();
        expectTrue(hasKeyLine(round, "lv_5"), "round-trip keeps &lv_5", failed, out);
        expectTrue(hasKeyLine(round, "inote_5"), "round-trip keeps &inote_5", failed, out);
    }

    // --- A `&des_N` with no chart becomes a standalone (no phantom diff). ---
    {
        const QString src =
            "&title=Song\n&first=0\n&des_3=Bob\n&lv_5=12\n&inote_5=(120){1}1,\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectTrue(doc.difficulty(3) == nullptr, "chart-less des_3 is NOT a difficulty", failed, out);
        expectTrue(!doc.difficultyIds().contains(3), "des_3 absent from difficultyIds()", failed, out);
        expectTrue(doc.standaloneDesignerIds() == QVector<int>{3}, "des_3 recorded as standalone", failed, out);
        expectEqual(doc.designerForSlot(3), "Bob", "designerForSlot reads the standalone name", failed, out);

        const QString round = doc.toText();
        expectTrue(hasDesLine(round, 3, "Bob"), "round-trip emits bare &des_3=Bob", failed, out);
        expectTrue(!hasKeyLine(round, "lv_3"), "no phantom &lv_3 written", failed, out);
        expectTrue(!hasKeyLine(round, "inote_3"), "no phantom &inote_3 written", failed, out);

        // Re-parse the serialized form: the standalone must survive a round-trip.
        const SimaiDocument reparsed = SimaiDocument::fromText(round);
        expectTrue(reparsed.difficulty(3) == nullptr, "reparsed des_3 still NOT a difficulty", failed, out);
        expectEqual(reparsed.designerForSlot(3), "Bob", "reparsed standalone name preserved", failed, out);
        expectTrue(reparsed.difficulty(5) != nullptr, "reparsed keeps the real difficulty 5", failed, out);
    }

    // --- setDesignerForSlot: existing diff vs chart-less slot. ---
    {
        SimaiDocument doc = SimaiDocument::fromText("&lv_5=12\n&inote_5=(120){1}1,\n");
        doc.setDesignerForSlot(5, "Dora");   // real difficulty → on the difficulty
        doc.setDesignerForSlot(2, "Eve");    // chart-less → standalone
        expectEqual(doc.designerForSlot(5), "Dora", "setDesignerForSlot writes the charted designer", failed, out);
        expectTrue(doc.difficulty(2) == nullptr, "setDesignerForSlot on empty slot makes no difficulty", failed, out);
        expectEqual(doc.designerForSlot(2), "Eve", "setDesignerForSlot stores a standalone name", failed, out);

        // Clearing a standalone removes it entirely.
        doc.setDesignerForSlot(2, QString());
        expectTrue(doc.standaloneDesignerIds().isEmpty(), "clearing a standalone removes it", failed, out);
        expectTrue(!hasKeyLine(doc.toText(), "des_2"), "cleared standalone is not serialized", failed, out);
    }

    // --- perDifficultyDesigners merges real + standalone, sorted by id. ---
    {
        const QString src =
            "&des_2=Two\n&des_4=Four\n&lv_4=10\n&inote_4=(120){1}1,\n&lv_6=11\n&inote_6=(120){1}2,\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        // 2 = standalone, 4 = real (with name), 6 = real (no name).
        const QVector<QPair<int, QString>> slotPairs = doc.perDifficultyDesigners();
        QStringList rendered;
        for (const QPair<int, QString>& slot : slotPairs) {
            rendered << QStringLiteral("%1:%2").arg(slot.first).arg(slot.second);
        }
        expectEqual(rendered.join(','), "2:Two,4:Four,6:", "perDifficultyDesigners merges & sorts", failed, out);
    }

    // --- isUnifiedDesignerTriviallySafe: the load-time judgement. ---
    //
    // Opening a chart asks this whether the project's stored "all difficulties
    // share one designer" preference still matches the file. True restores the
    // mode; false silently lowers the PREFERENCE and leaves the document
    // untouched, which is why a freshly opened chart is never dirty. Every
    // designer-bearing slot counts, chart-less `&des_N` included.
    {
        const auto verdict = [](const char* text) {
            return SimaiDocument::fromText(QString::fromUtf8(text)).isUnifiedDesignerTriviallySafe();
        };
        expectTrue(verdict("&des=X\n&lv_5=12\n&des_5=X\n&inote_5=(120){1}1,\n"
                           "&lv_6=13\n&des_6=X\n&inote_6=(120){1}2,\n"),
                   "every &des_N matching &des satisfies the shared-designer mode", failed, out);
        expectTrue(verdict("&des=X\n&title=Song\n"),
                   "a chart with no difficulties satisfies it", failed, out);
        expectTrue(verdict("&title=Song\n&lv_5=12\n&des_5=\n&inote_5=(120){1}1,\n"),
                   "an all-blank project satisfies it", failed, out);
        expectTrue(!verdict("&des=X\n&lv_5=12\n&des_5=\n&inote_5=(120){1}1,\n"),
                   "a difficulty with no name of its own does not satisfy it", failed, out);
        expectTrue(!verdict("&des=X\n&lv_5=12\n&des_5=X\n&inote_5=(120){1}1,\n"
                            "&lv_6=13\n&des_6=Y\n&inote_6=(120){1}2,\n"),
                   "a second distinct name does not satisfy it", failed, out);
        expectTrue(!verdict("&des=X\n&des_3=Y\n&lv_5=12\n&des_5=X\n&inote_5=(120){1}1,\n"),
                   "a chart-less &des_N that disagrees does not satisfy it either", failed, out);
    }

    // --- A fully-empty difficulty (no name) is NOT swept into standalone. ---
    {
        // An empty &inote_7 with no name is a freshly-added blank difficulty;
        // it must round-trip as a difficulty, not vanish into the standalone map.
        const QString src = "&title=Song\n&first=0\n&lv_7=\n&des_7=\n&inote_7=\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectTrue(doc.difficulty(7) != nullptr, "blank difficulty 7 stays a difficulty", failed, out);
        expectTrue(doc.standaloneDesignerIds().isEmpty(), "blank difficulty is not a standalone", failed, out);
    }

    // --- &clock_count default is materialized exactly once and never stacks. ---
    {
        // Regression: fromText() used to start from createEmpty() (which seeds a
        // default &clock_count=4); a file that also carried its own &clock_count=
        // then accumulated duplicates that grew on every load/save round-trip.
        auto clockLines = [](const QString& text) {
            int n = 0;
            for (const QString& line : text.split('\n')) {
                if (line.startsWith(QStringLiteral("&clock_count="))) {
                    ++n;
                }
            }
            return n;
        };

        const SimaiDocument withClock = SimaiDocument::fromText(
            "&title=T\n&first=0\n&clock_count=6\n&lv_5=12\n&inote_5=(120){1}1,\n");
        expectTrue(clockLines(withClock.toText()) == 1, "existing &clock_count is not duplicated on load", failed, out);
        expectTrue(withClock.toText().contains(QStringLiteral("&clock_count=6")),
                   "the file's own &clock_count value is preserved (not reset to the default)", failed, out);

        const SimaiDocument round = SimaiDocument::fromText(
            SimaiDocument::fromText(withClock.toText()).toText());
        expectTrue(clockLines(round.toText()) == 1, "&clock_count stays single across load/save round-trips", failed, out);

        const SimaiDocument noClock = SimaiDocument::fromText(
            "&title=X\n&first=0\n&lv_5=1\n&inote_5=(120){1}1,\n");
        expectTrue(clockLines(noClock.toText()) == 1, "missing &clock_count is defaulted exactly once", failed, out);

        QVector<SimaiRawField> dupFields;
        dupFields.append(SimaiRawField{QStringLiteral("clock_count"), QStringLiteral("6")});
        dupFields.append(SimaiRawField{QStringLiteral("clock_count"), QStringLiteral("6")});
        SimaiDocument::ensureDefaultClockCount(&dupFields);
        expectTrue(dupFields.size() == 1, "ensureDefaultClockCount collapses duplicate &clock_count entries", failed, out);
    }

    // --- A &key= header may sit behind leading horizontal whitespace, and a
    //     field's value is right-trimmed of trailing blanks / blank lines. ---
    {
        // Spaces/tabs before the `&` are ignored (the key/value never absorb
        // them); the value loses every trailing whitespace char, including
        // trailing blank lines, while interior content (newlines, leading
        // spaces after `=`) is preserved verbatim.
        const QString src =
            "\t&title=Song   \n"
            "   &artist=  Composer\t\n"
            "  &first=0\n"
            "&inote_5=(120){1}1,\n\n   \n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectEqual(doc.title, "Song", "indented &title parsed; trailing spaces trimmed", failed, out);
        expectEqual(doc.artist, "  Composer", "indented &artist parsed; leading value space kept, tail trimmed", failed, out);
        expectEqual(doc.first, "0", "indented &first parsed", failed, out);
        const SimaiDifficultyData* master = doc.difficulty(5);
        expectTrue(master != nullptr, "indented file still produces difficulty 5", failed, out);
        if (master != nullptr) {
            expectEqual(master->chart, "(120){1}1,", "inote value loses trailing blank lines/whitespace", failed, out);
        }

        // A value that spans multiple lines keeps its interior line breaks; only
        // the trailing run of blank lines/whitespace is shaved off.
        const QString multiline = SimaiDocument::fromText(
            "&inote_1=(120){1}1,\n2,\n\t\n  \n").difficulty(1)->chart;
        expectEqual(multiline, "(120){1}1,\n2,", "interior newlines preserved, tail trimmed", failed, out);
    }

    // --- `&first` always serializes as a parseable number, never a bare
    //     `&first=` that crashes strict third-party players. ---
    {
        // New / untouched offset: the model holds an empty `first`. A bare
        // `&first=` is what MajdataPlay's double.Parse chokes on, so we must
        // emit `&first=0` (the lossless encoding of "no offset").
        const SimaiDocument fresh = SimaiDocument::createEmpty();
        expectTrue(fresh.first.isEmpty(), "fresh document has an empty first", failed, out);
        expectTrue(hasLine(fresh.toText(), "&first=0"), "empty first serializes as &first=0", failed, out);
        expectTrue(!hasLine(fresh.toText(), "&first="), "no bare &first= is ever emitted", failed, out);

        // A chart authored elsewhere with NO &first line must not gain a bare
        // `&first=` on open→save; it gains a valid `&first=0` instead.
        const SimaiDocument noFirst = SimaiDocument::fromText("&title=T\n&lv_5=12\n&inote_5=(120){1}1,\n");
        expectTrue(hasLine(noFirst.toText(), "&first=0"), "missing &first is injected as &first=0", failed, out);

        // An explicit value is preserved verbatim (no clobber to 0).
        const SimaiDocument withFirst = SimaiDocument::fromText("&title=T\n&first=-0.123\n&lv_5=12\n&inote_5=(120){1}1,\n");
        expectTrue(hasLine(withFirst.toText(), "&first=-0.123"), "explicit &first value is preserved", failed, out);
    }

    // --- Empty numeric metadata (&wholebpm/&pvstart/&pvlen) is dropped on
    //     save; a populated value round-trips untouched. ---
    {
        const SimaiDocument cleared = SimaiDocument::fromText(
            "&title=T\n&first=0\n&wholebpm=\n&pvstart=\n&pvlen=  \n&lv_5=12\n&inote_5=(120){1}1,\n");
        const QString out2 = cleared.toText();
        expectTrue(!hasKeyLine(out2, "wholebpm"), "empty &wholebpm is dropped on save", failed, out);
        expectTrue(!hasKeyLine(out2, "pvstart"), "empty &pvstart is dropped on save", failed, out);
        expectTrue(!hasKeyLine(out2, "pvlen"), "empty (whitespace) &pvlen is dropped on save", failed, out);

        const SimaiDocument kept = SimaiDocument::fromText(
            "&title=T\n&first=0\n&wholebpm=180\n&pvstart=3.5\n&lv_5=12\n&inote_5=(120){1}1,\n");
        const QString out3 = kept.toText();
        expectTrue(hasLine(out3, "&wholebpm=180"), "populated &wholebpm is preserved", failed, out);
        expectTrue(hasLine(out3, "&pvstart=3.5"), "populated &pvstart is preserved", failed, out);
    }

    // --- Obsolete &miacode_bookmarks= field: reserved, ignored, not persisted. ---
    {
        const QString src =
            "&title=Song\n&first=0\n"
            "&miacode_bookmarks={\"schema\":\"miacode_bookmarks_v2\",\"items\":["
            "{\"d\":5,\"l\":8,\"n\":\"Intro 引入\",\"s\":8.796,\"src\":\"comment\",\"fp\":\"9e6a1d\"},"
            "{\"d\":5,\"l\":24,\"n\":\"Chorus\",\"locked\":true},"
            "{\"d\":9,\"l\":1,\"n\":\"bad difficulty — dropped\"}]}\n"
            "&lv_5=12\n&inote_5=(120){1}1,\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        bool inExtraFields = false;
        for (const SimaiRawField& field : doc.extraFields) {
            inExtraFields = inExtraFields || field.key == QLatin1String("miacode_bookmarks");
        }
        expectTrue(!inExtraFields, "obsolete miacode_bookmarks never lands in extraFields", failed, out);

        const QString round = doc.toText();
        expectTrue(!hasKeyLine(round, "miacode_bookmarks"), "obsolete miacode_bookmarks is removed on save", failed, out);

        // The obsolete key stays out of the "Other &xx Fields" editor.
        const QVector<SimaiRawField> unmanaged = SimaiDocument::parseUnmanagedFields(src);
        bool inUnmanaged = false;
        for (const SimaiRawField& field : unmanaged) {
            inUnmanaged = inUnmanaged || field.key == QLatin1String("miacode_bookmarks");
        }
        expectTrue(!inUnmanaged, "parseUnmanagedFields hides miacode_bookmarks", failed, out);
    }

    // --- Dedicated &clock_count is not shown again in "Other &xx Fields". ---
    {
        const QVector<SimaiRawField> unmanaged = SimaiDocument::parseUnmanagedFields(
            "&title=Song\n&first=0\n&clock_count=6\n&custom=value\n");
        bool hasClockCount = false;
        bool hasCustom = false;
        for (const SimaiRawField& field : unmanaged) {
            hasClockCount = hasClockCount || field.key == QLatin1String("clock_count");
            hasCustom = hasCustom || field.key == QLatin1String("custom");
        }
        expectTrue(!hasClockCount, "parseUnmanagedFields hides dedicated clock_count", failed, out);
        expectTrue(hasCustom, "parseUnmanagedFields keeps unrelated extra fields", failed, out);
    }

    // --- Extra-field validation reports the exact property-looking line. ---
    {
        const QVector<SimaiPropertyIssue> issues =
            SimaiDocument::invalidPropertyLineNumbers(
                "  &missing_equals\r\n&=empty_key\r\n&dummy=value\r\n");
        expectTrue(issues.size() == 2, "invalid extra fields are reported without rejecting valid fields",
                   failed, out);
        if (issues.size() == 2) {
            expectTrue(issues.at(0).line == 1 && issues.at(0).column == 3
                           && issues.at(0).endColumn == 17
                           && issues.at(0).code == QLatin1String("invalid_property"),
                       "indented missing-equals field reports its line and ampersand column",
                       failed, out);
            expectTrue(issues.at(1).line == 2 && issues.at(1).column == 1
                           && issues.at(1).endColumn == 11,
                       "empty-key field reports the full property span", failed, out);
        }
        expectTrue(SimaiDocument::invalidPropertyLineNumbers(
                       "  &dummy=value\r\n  &empty=\r\n").isEmpty(),
                   "indented fields with empty values remain valid", failed, out);
        expectTrue(SimaiDocument::invalidPropertyLineNumbers("plain text").size() == 1,
                   "non-property text is rejected instead of being silently discarded", failed, out);
    }

    // --- Bad/empty obsolete bookmark payload never blocks parsing and is never emitted. ---
    {
        const QString src =
            "&title=Song\n&first=0\n"
            "&miacode_bookmarks={not json at all\n"
            "&lv_5=12\n&inote_5=(120){1}1,\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectEqual(doc.title, "Song", "bad bookmark JSON does not block other fields", failed, out);
        expectTrue(doc.difficulty(5) != nullptr, "bad bookmark JSON keeps difficulty parsing", failed, out);
        expectTrue(!hasKeyLine(doc.toText(), "miacode_bookmarks"), "obsolete bookmark field emits no field on save", failed, out);

        // A bare `&miacode_bookmarks=` is ignored as an obsolete field.
        const SimaiDocument bare = SimaiDocument::fromText("&title=T\n&first=0\n&miacode_bookmarks=\n&lv_5=1\n&inote_5=(120){1}1,\n");
        expectTrue(!hasKeyLine(bare.toText(), "miacode_bookmarks"), "bare obsolete bookmark field is removed on save", failed, out);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int failed = 0;
    runSpecs(out, &failed);
    if (failed != 0) {
        out << "\nSimaiDocument spec failed: " << failed << " case(s)\n";
        return 1;
    }
    out << "\nSimaiDocument spec passed.\n";
    return 0;
}
