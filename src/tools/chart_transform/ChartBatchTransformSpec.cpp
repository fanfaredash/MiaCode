#include "ChartBatchTransform.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

void expectEqual(const QString& actual, const QString& expected, const QString& message, int* failed, QTextStream& err)
{
    if (actual == expected) {
        return;
    }
    err << "[FAIL] " << message << '\n';
    err << "  expected: " << expected << '\n';
    err << "  actual:   " << actual << '\n';
    ++(*failed);
}

void expectTrue(bool condition, const QString& message, int* failed, QTextStream& err)
{
    if (condition) {
        return;
    }
    err << "[FAIL] " << message << '\n';
    ++(*failed);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;

    {
        int changed = 0;
        const QString input = QStringLiteral("12 3h[4:1] A1 C1h[4:1] 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1b/2b 3bh[4:1] A1b C1h[4:1] 1b-5b[8:1]"),
            QStringLiteral("toggle break enables break for notes, touch, and slide track but skips touch-hold"),
            &failed,
            err
        );
        expectTrue(changed == 6, QStringLiteral("toggle break counts all changed objects"), &failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1b/2b 3bh[4:1] A1b C1h[4:1] 1b-5b[8:1]");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1/2 3h[4:1] A1 C1h[4:1] 1-5[8:1]"),
            QStringLiteral("toggle break clears break when all eligible objects already have it"),
            &failed,
            err
        );
        expectTrue(changed == 6, QStringLiteral("toggle break clear counts all cleared objects"), &failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1 2h[4:1] A1 C1h[4:1] 1-5[8:1] 6b");
        const QString output = miacode::chart_transform::toggleExForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1x 2xh[4:1] A1 C1h[4:1] 1x-5[8:1] 6bx"),
            QStringLiteral("toggle EX enables ex for note, hold, and slide head but skips touch and touch-hold"),
            &failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("toggle EX counts eligible note objects that changed"), &failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1x 2xh[4:1] A1 C1h[4:1] 1x-5[8:1] 6bx");
        const QString output = miacode::chart_transform::toggleExForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1 2h[4:1] A1 C1h[4:1] 1-5[8:1] 6b"),
            QStringLiteral("toggle EX clears ex when all eligible objects already have it"),
            &failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("toggle EX clear counts eligible note objects"), &failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("A1 A2h[4:1] C1 1 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleFireworkForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("A1f A2fh[4:1] C1f 1 1-5[8:1]"),
            QStringLiteral("toggle firework enables firework for touch and touch-hold only"),
            &failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("toggle firework counts only touch objects"), &failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("A1f A2fh[4:1] C1f 1 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleFireworkForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("A1 A2h[4:1] C1 1 1-5[8:1]"),
            QStringLiteral("toggle firework clears firework when all eligible touch objects already have it"),
            &failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("toggle firework clear counts only touch objects"), &failed, err);
    }

    if (failed != 0) {
        err << "\nChart batch transform spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "Chart batch transform spec passed.\n";
    return 0;
}
