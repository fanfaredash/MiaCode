#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTextStream>

#include <memory>

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

std::unique_ptr<QObject> createReplacementHarness(QQmlEngine& engine, QTextStream& err)
{
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQml
import "." as Mia

QtObject {
    id: root
    property int scheduledRefreshes: 1
    property int validationRevision: 73
    property alias activeEditorKey: editorTabs.activeEditorKey

    property QtObject editorTabs: Mia.ViewState {
        id: editorTabs
        onDifficultyEditorActivationRequested: function(difficultyId) {
            root.scheduledRefreshes += 1
            root.validationRevision += 1
        }
    }

    function replaceDocument(currentDifficultyId) {
        editorTabs.resetEditorTabs(currentDifficultyId)
    }

    function activateEditorForUser(difficultyId) {
        editorTabs.openDifficultyEditor(difficultyId)
    }
}
)", QUrl::fromLocalFile(
        QStringLiteral(MIACODE_SOURCE_ROOT "/src/app/qml_ui/ReplacementSequenceHarness.qml")));
    if (component.status() == QQmlComponent::Error) {
        for (const QQmlError& error : component.errors()) {
            err << "FAIL: replacement harness load: " << error.toString() << Qt::endl;
        }
        return {};
    }
    return std::unique_ptr<QObject>(component.create());
}

bool invokeDifficultyMethod(QObject* object, const char* method, int difficultyId)
{
    return QMetaObject::invokeMethod(
        object, method, Q_ARG(QVariant, QVariant::fromValue(difficultyId)));
}

bool verifyReplacementDoesNotScheduleASecondDifficultySwitch(QTextStream& err)
{
    QQmlEngine engine;
    const std::unique_ptr<QObject> harness = createReplacementHarness(engine, err);
    if (!harness) {
        return false;
    }

    const bool invoked = invokeDifficultyMethod(harness.get(), "replaceDocument", 5);
    const bool replacementIsSilent = invoked
        && harness->property("activeEditorKey").toString() == QLatin1String("difficulty:5")
        && harness->property("scheduledRefreshes").toInt() == 1
        && harness->property("validationRevision").toInt() == 73;

    const bool userActivationInvoked = invokeDifficultyMethod(harness.get(), "activateEditorForUser", 6);
    const bool userActivationStillSelects = userActivationInvoked
        && harness->property("scheduledRefreshes").toInt() == 2
        && harness->property("validationRevision").toInt() == 74;

    return require(replacementIsSilent,
                   QStringLiteral("a backend-selected replacement keeps one scheduled refresh and its original validation revision"), err)
        && require(userActivationStillSelects,
                   QStringLiteral("a user-selected editor tab still performs exactly one difficulty activation"), err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = verifyReplacementDoesNotScheduleASecondDifficultySwitch(err);
    if (ok) {
        out << "qml_document_replacement_sequence_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
