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

    // --- A fully-empty difficulty (no name) is NOT swept into standalone. ---
    {
        // An empty &inote_7 with no name is a freshly-added blank difficulty;
        // it must round-trip as a difficulty, not vanish into the standalone map.
        const QString src = "&title=Song\n&first=0\n&lv_7=\n&des_7=\n&inote_7=\n";
        const SimaiDocument doc = SimaiDocument::fromText(src);
        expectTrue(doc.difficulty(7) != nullptr, "blank difficulty 7 stays a difficulty", failed, out);
        expectTrue(doc.standaloneDesignerIds().isEmpty(), "blank difficulty is not a standalone", failed, out);
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
