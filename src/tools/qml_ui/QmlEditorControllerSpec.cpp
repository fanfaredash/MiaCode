#include "app/qml_ui/QmlEditorController.h"
#include "app/qml_ui/QmlEditorInputBridge.h"

#include <QCoreApplication>
#include <QFile>
#include <QInputMethodEvent>
#include <QTextStream>

namespace {

class InputMethodReceiver final : public QObject
{
public:
    int inputMethodEvents = 0;
    QString commit;
    QString preedit;
    QList<QInputMethodEvent::Attribute> attributes;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::InputMethod) {
            ++inputMethodEvents;
            auto* input = static_cast<QInputMethodEvent*>(event);
            commit = input->commitString();
            preedit = input->preeditString();
            attributes = input->attributes();
        }
        return QObject::event(event);
    }
};
bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    if (!condition) ++*failed;
    return condition;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;
    miacode::qml_ui::QmlEditorController controller;
    const auto opening = controller.processKey(QString(), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft, Qt::NoModifier);
    expect(opening.consumed && opening.transaction.replacementText == QStringLiteral("[]") && opening.transaction.position == 1, QStringLiteral("policy key input produces one QML edit transaction"), out, &failed);
    expect(controller.completionActive() && controller.completionIndex() == 0 && controller.completionCandidates().contains(QStringLiteral("8:1]")), QStringLiteral("opening bracket publishes a completion session"), out, &failed);
    controller.moveCompletionSelection(1);
    expect(controller.completionIndex() == 1, QStringLiteral("completion navigation updates the selected index"), out, &failed);
    controller.selectCompletionIndex(0);
    const auto filter = controller.processKey(opening.transaction.text, 1, 1, QStringLiteral("8"), Qt::Key_8, Qt::NoModifier);
    expect(filter.transaction.text == QStringLiteral("[8]") && controller.completionCandidates() == QStringList{QStringLiteral("8:1]")} && controller.completionIndex() == 0, QStringLiteral("post-edit completion filter keeps a valid selection index"), out, &failed);
    const auto accepted = controller.acceptCompletion(filter.transaction.text, 2, 2);
    expect(accepted.consumed && accepted.transaction.replacementStart == 1 && accepted.transaction.replacementEnd == 3 && accepted.transaction.replacementText == QStringLiteral("8:1]") && accepted.transaction.text == QStringLiteral("[8:1]"), QStringLiteral("accept replaces filter and existing closing glyph without newline"), out, &failed);
    expect(!controller.completionActive(), QStringLiteral("accept closes the completion session"), out, &failed);
    const auto paste = controller.processPaste(QString(), 0, 0, QStringLiteral("【"));
    expect(paste.consumed && paste.transaction.text == QStringLiteral("[") && !controller.completionActive(), QStringLiteral("paste follows policy without opening completion"), out, &failed);
    const auto preedit = controller.processImeCommit(QStringLiteral("abc"), 3, 3, QString());
    expect(!preedit.consumed && !preedit.transaction.hasEdit, QStringLiteral("uncommitted IME preedit does not change document"), out, &failed);
    const auto commit = controller.processImeCommit(QString(), 0, 0, QStringLiteral("【"));
    expect(commit.consumed && commit.transaction.text == QStringLiteral("[]") && controller.completionActive(), QStringLiteral("committed IME is one policy transaction"), out, &failed);
    controller.setAutoCompletionEnabled(false);
    const auto disabled = controller.processKey(QString(), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft, Qt::NoModifier);
    expect(disabled.transaction.text == QStringLiteral("[") && !controller.completionActive(), QStringLiteral("read-only smart input preference controls policy"), out, &failed);
    controller.setAutoCompletionEnabled(true);
    controller.setWholeBpm(QStringLiteral("180"));
    controller.processKey(QString(), 0, 0, QStringLiteral("("), 0, Qt::NoModifier);
    expect(controller.wholeBpm() == QStringLiteral("180") && controller.completionCandidates() == QStringList{QStringLiteral("180)")}, QStringLiteral("current whole BPM reaches completion catalog"), out, &failed);
    const auto undo = controller.processKey(QStringLiteral("x"), 1, 1, QString(), Qt::Key_Z, Qt::ControlModifier);
    const auto redo = controller.processKey(QStringLiteral("x"), 1, 1, QString(), Qt::Key_Y, Qt::ControlModifier);
    expect(!undo.consumed && !redo.consumed, QStringLiteral("undo and redo remain native TextArea operations"), out, &failed);
    controller.setUndoAvailability(true, false);
    expect(controller.canUndo() && !controller.canRedo(), QStringLiteral("controller exposes native undo availability to QML"), out, &failed);

    InputMethodReceiver inputTarget;
    miacode::qml_ui::QmlEditorInputBridge inputBridge;
    inputBridge.setTarget(&inputTarget);
    int commits = 0;
    QString committedText;
    QObject::connect(&inputBridge, &miacode::qml_ui::QmlEditorInputBridge::imeCommitted,
                     [&commits, &committedText](const QString& value) { ++commits; committedText = value; });
    QInputMethodEvent preeditEvent(QStringLiteral("preedit"), {});
    QCoreApplication::sendEvent(&inputTarget, &preeditEvent);
    expect(commits == 0, QStringLiteral("IME preedit passes through without a transaction"), out, &failed);
    const QList<QInputMethodEvent::Attribute> attributes{
        QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, 1, QVariant())};
    QInputMethodEvent imeCommit(QStringLiteral("候補"), attributes);
    imeCommit.setCommitString(QStringLiteral("【"));
    QCoreApplication::sendEvent(&inputTarget, &imeCommit);
    expect(commits == 1 && committedText == QStringLiteral("【")
               && inputTarget.inputMethodEvents == 2 && inputTarget.commit.isEmpty()
               && inputTarget.preedit == QStringLiteral("候補")
               && inputTarget.attributes == attributes,
           QStringLiteral("IME bridge strips commit once while forwarding preedit and attributes"), out, &failed);

    QFile sourceEditor(QStringLiteral("src/app/qml_ui/editor/SourceEditor.qml"));
    expect(sourceEditor.open(QIODevice::ReadOnly), QStringLiteral("SourceEditor QML is available to wiring test"), out, &failed);
    const QString source = QString::fromUtf8(sourceEditor.readAll());
    expect(source.contains(QStringLiteral("processImeCommitForQml"))
               && source.contains(QStringLiteral("processPasteForQml"))
               && source.contains(QStringLiteral("QmlEditorInputBridge"))
               && source.contains(QStringLiteral("editorController.canUndo")),
           QStringLiteral("SourceEditor routes committed IME, paste and undo state through controller"), out, &failed);
    return failed == 0 ? 0 : 1;
}
