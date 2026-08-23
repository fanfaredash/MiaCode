#include <QFile>
#include <QTextStream>

#include "app/qml_ui/QmlDocumentProjection.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyNonActiveDifficultyDeletionPolicy(QTextStream& err)
{
    QFile source(QStringLiteral(
        "src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp"));
    if (!require(source.open(QIODevice::ReadOnly | QIODevice::Text),
                 QStringLiteral("DocumentUi source is readable for deletion-policy regression"), err)) {
        return false;
    }
    const QString contents = QString::fromUtf8(source.readAll());
    return require(
        contents.contains(QStringLiteral(
            "if (deletingActiveDifficulty) {\n"
            "        owner_.invalidateDocumentValidationRevision();\n"
            "    } else {\n"
            "        emit owner_.documentValidationChanged();\n"
            "    }")),
        QStringLiteral("non-active difficulty deletion preserves the current validation revision"),
        err);
}

miacode::qml_ui::DocumentValidationProjectionInput currentInput()
{
    miacode::qml_ui::DocumentValidationProjectionInput input;
    input.difficultyId = 5;
    input.chartTextSignature = QStringLiteral("chart@42");
    input.timingSignature = QStringLiteral("4/4");
    input.timelineRevision = 42;
    return input;
}

miacode::qml_ui::DocumentValidationProjectionCache matchingCache()
{
    miacode::qml_ui::DocumentValidationProjectionCache cache;
    cache.difficultyId = 5;
    cache.chartTextSignature = QStringLiteral("chart@42");
    cache.timingSignature = QStringLiteral("4/4");
    cache.validationRevision = 42;
    cache.ok = false;
    cache.errorCount = 1;
    cache.warningCount = 1;
    cache.parsedNoteCount = 7;
    cache.issues = {
        {2, 3, 5, miacode::qml_ui::DocumentValidationIssueSeverity::Error,
         QStringLiteral("bad token")},
        {4, 1, 2, miacode::qml_ui::DocumentValidationIssueSeverity::Warning,
         QStringLiteral("compatibility warning")},
    };
    return cache;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= verifyNonActiveDifficultyDeletionPolicy(err);

    const miacode::qml_ui::DocumentValidationProjection projection =
        miacode::qml_ui::projectDocumentValidation(currentInput(), matchingCache());
    ok &= require(projection.available && !projection.pending && projection.revision == 42,
                  QStringLiteral("matching cache is available at the current revision"), err);
    ok &= require(!projection.ok && projection.errorCount == 1 && projection.warningCount == 1
                      && projection.parsedNoteCount == 7,
                  QStringLiteral("matching cache projects validation counts"), err);
    ok &= require(projection.issues.size() == 2
                      && projection.issues.at(0).line == 2
                      && projection.issues.at(0).column == 3
                      && projection.issues.at(0).endColumn == 5
                      && projection.issues.at(0).severity
                          == miacode::qml_ui::DocumentValidationIssueSeverity::Error
                      && projection.issues.at(0).message == QLatin1String("bad token"),
                  QStringLiteral("matching cache projects issue locations, severity, and message"), err);

    auto lateA = matchingCache();
    lateA.chartTextSignature = QStringLiteral("chart@41");
    lateA.validationRevision = 41;
    const miacode::qml_ui::DocumentValidationProjection staleProjection =
        miacode::qml_ui::projectDocumentValidation(currentInput(), lateA);
    ok &= require(!staleProjection.available && staleProjection.pending
                      && staleProjection.revision == 42 && staleProjection.errorCount == 0
                      && staleProjection.warningCount == 0 && staleProjection.parsedNoteCount == 0
                      && staleProjection.issues.isEmpty(),
                  QStringLiteral("late A revision 41 cannot overwrite current B revision 42"), err);

    for (const miacode::qml_ui::DocumentValidationProjectionCache mismatchedCache : {
             [&] { auto cache = matchingCache(); cache.difficultyId = 4; return cache; }(),
             [&] { auto cache = matchingCache(); cache.chartTextSignature = QStringLiteral("chart@41"); return cache; }(),
             [&] { auto cache = matchingCache(); cache.timingSignature = QStringLiteral("3/4"); return cache; }(),
         }) {
        const miacode::qml_ui::DocumentValidationProjection mismatchProjection =
            miacode::qml_ui::projectDocumentValidation(currentInput(), mismatchedCache);
        ok &= require(!mismatchProjection.available && mismatchProjection.pending
                          && mismatchProjection.errorCount == 0
                          && mismatchProjection.warningCount == 0
                          && mismatchProjection.parsedNoteCount == 0
                          && mismatchProjection.issues.isEmpty(),
                      QStringLiteral("difficulty, text, and timing mismatches expose no stale diagnostics"), err);
    }

    auto noDifficulty = currentInput();
    noDifficulty.difficultyId = 0;
    const miacode::qml_ui::DocumentValidationProjection noDifficultyProjection =
        miacode::qml_ui::projectDocumentValidation(noDifficulty, matchingCache());
    ok &= require(!noDifficultyProjection.available && !noDifficultyProjection.pending,
                  QStringLiteral("no active difficulty is unavailable without a pending validation"), err);

    if (!ok) {
        return 1;
    }
    out << "Qml document projection checks passed." << Qt::endl;
    return 0;
}
