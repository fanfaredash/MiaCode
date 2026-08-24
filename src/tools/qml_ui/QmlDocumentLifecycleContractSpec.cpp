#include <QFile>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined as the absolute repository root"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString sourceFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT "/") + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool containsAfter(const QString& source, const QString& first, const QString& second)
{
    const int firstPosition = source.indexOf(first);
    return firstPosition >= 0 && source.indexOf(second, firstPosition + first.size()) >= 0;
}

bool verifyBackendReplacementPublishesOneQmlRefresh(QTextStream& err)
{
    const QString mainWindow = sourceFile(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    const QString documentUi = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainView = sourceFile(QStringLiteral("src/app/qml_ui/layout/MainView.qml"));

    return require(mainWindow.contains(QStringLiteral("void documentReplaced();")),
                   QStringLiteral("MainWindow exposes a backend document replacement notification"), err)
        && require(containsAfter(documentUi,
                                 QStringLiteral("void MainWindow::DocumentSection::loadDocument"),
                                 QStringLiteral("emit owner_.documentReplaced();")),
                   QStringLiteral("every loadDocument path publishes the backend replacement after loading"), err)
        && require(containsAfter(documentModel,
                                 QStringLiteral("&MainWindow::documentReplaced"),
                                 QStringLiteral("clearMetadataSourceRejection();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("Qt::QueuedConnection"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("emitDocumentStateChanged();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("emit documentReplaced();")),
                   QStringLiteral("backend replacements wait for the completed transaction, clear rejected source state, and publish the complete QML document snapshot"), err)
        && require(containsAfter(mainView,
                                 QStringLiteral("function onDocumentReplaced()"),
                                 QStringLiteral("state.resetEditorTabs(root.documentSession.currentDifficultyId)")),
                   QStringLiteral("a QML replacement resets active editors so chart text and bookmarks are rederived"), err);
}

bool verifyNewDocumentPublishesFinalFileIdentity(QTextStream& err)
{
    const QString fileFlow = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentFileFlow.cpp"));
    const int newDocument = fileFlow.indexOf(QStringLiteral("const SimaiDocument newDocument"));
    const int end = fileFlow.indexOf(QStringLiteral("namespace {"), newDocument);
    const QString createFlow = newDocument >= 0
        ? fileFlow.mid(newDocument, (end >= 0 ? end : fileFlow.size()) - newDocument)
        : QString();
    return require(containsAfter(createFlow,
                                 QStringLiteral("owner_.setCurrentFilePath(targetPath)"),
                                 QStringLiteral("loadDocument(newDocument)")),
                   QStringLiteral("new-document replacement publishes after the final file identity is installed"), err);
}

bool verifyV2ValidationSchedulesMatchingMuriRefresh(QTextStream& err)
{
    const QString validationRuntime = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/validation/MainWindow.ValidationRuntime.cpp"));
    const int validationEntry = validationRuntime.indexOf(
        QStringLiteral("bool MainWindow::validateActiveDocument()"));
    const QString entry = validationEntry >= 0 ? validationRuntime.mid(validationEntry) : QString();
    const QString analysisFlow = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.TimelineAnalysisFlow.cpp"));

    return require(containsAfter(entry,
                                 QStringLiteral("timelineSection_->scheduleTimelineRefresh();"),
                                 QStringLiteral("return validationSection_->runValidateSimaiSilently(false);")),
                   QStringLiteral("v2 validation schedules a fresh timeline slow refresh before publishing validation"), err)
        && require(containsAfter(analysisFlow,
                                 QStringLiteral("guard->state_.validationCacheByDifficulty_[result.difficultyId]"),
                                 QStringLiteral("guard->state_.muriAnalysisReportTimelineRevision_ = result.revision;"))
                       && containsAfter(analysisFlow,
                                        QStringLiteral("guard->state_.muriAnalysisReportTimelineRevision_ = result.revision;"),
                                        QStringLiteral("guard->state_.muriStaticReferencesTimelineRevision_ = result.revision;")),
                   QStringLiteral("the scheduled slow result publishes validation, Muri report, and static references at one revision"), err);
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = verifyBackendReplacementPublishesOneQmlRefresh(err)
        && verifyNewDocumentPublishesFinalFileIdentity(err)
        && verifyV2ValidationSchedulesMatchingMuriRefresh(err);
    if (ok) {
        out << "qml_document_lifecycle_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
