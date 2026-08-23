#include "app/qml_ui/QmlEditorController.h"
#include "app/qml_ui/QmlEditorInputBridge.h"
#include "app/qml_ui/QmlTouchPadAuthoringBridge.h"
#include "editor/BookmarkCommentSyntax.h"

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

    const auto firstFind = controller.find(QStringLiteral("A aa A"), 0, 0, QStringLiteral("a"), false, true, false);
    expect(firstFind.found && firstFind.start == 0 && firstFind.end == 1,
           QStringLiteral("whole-word find starts at the active selection"), out, &failed);
    const auto previousFind = controller.find(QStringLiteral("A aa A"), 4, 4, QStringLiteral("a"), false, true, true);
    expect(previousFind.found && previousFind.start == 0 && previousFind.end == 1,
           QStringLiteral("reverse find respects whole-word matching"), out, &failed);
    const auto replace = controller.replaceSelection(
        QStringLiteral("foo foo"), 0, 3, QStringLiteral("foo"), QStringLiteral("bar"), true, false);
    expect(replace.consumed && replace.transaction.text == QStringLiteral("bar foo")
               && replace.transaction.undoGroup,
           QStringLiteral("replace selection is a single undo transaction"), out, &failed);
    const auto replaceAll = controller.replaceAll(
        QStringLiteral("foo food foo"), QStringLiteral("foo"), QStringLiteral("bar"), true, true);
    expect(replaceAll.consumed && replaceAll.transaction.text == QStringLiteral("bar food bar")
               && replaceAll.transaction.undoGroup,
           QStringLiteral("replace all honors whole words in one undo transaction"), out, &failed);
    controller.resetQmlHistory(QStringLiteral("foo foo"), 0, 0);
    controller.recordQmlTransaction(QStringLiteral("foo foo"), QStringLiteral("bar bar"),
                                    0, 0, 7, 7);
    const auto restored = controller.undoQmlTransaction();
    const auto repeated = controller.redoQmlTransaction();
    expect(restored.value(QStringLiteral("replacementText")).toString() == QStringLiteral("foo foo")
               && restored.value(QStringLiteral("anchor")).toInt() == 0
               && repeated.value(QStringLiteral("replacementText")).toString() == QStringLiteral("bar bar")
               && repeated.value(QStringLiteral("position")).toInt() == 7,
           QStringLiteral("controller restores a complete Replace All snapshot in one undo/redo step"), out, &failed);

    controller.setDocumentContext(3, 42);
    expect(controller.acceptsCaret(3, 42, false) && !controller.acceptsCaret(3, 41, false)
               && !controller.acceptsTouchAuthoring(3, 42, true, false)
               && controller.acceptsTouchAuthoring(3, 42, false, true),
           QStringLiteral("caret and Ctrl touch authoring reject stale revisions and IME composition"), out, &failed);
    miacode::qml_ui::QmlTouchPadAuthoringContext touchContext{3, 3, 42, 42, true, false};
    QString touchText;
    int touchCaret = 0;
    expect(touchContext.accepts()
               && miacode::qml_ui::applyQmlTouchPadAuthoringEdit(
                   &touchText, &touchCaret, QStringLiteral("A1"), false)
               && touchText == QStringLiteral("A1") && touchCaret == 2
               && !miacode::qml_ui::QmlTouchPadAuthoringContext{3, 3, 42, 41, true, false}.accepts()
               && !miacode::qml_ui::QmlTouchPadAuthoringContext{3, 3, 42, 42, true, true}.accepts(),
           QStringLiteral("QML preview context gates and applies the shared touch-pad edit"), out, &failed);
    const auto rejectedRoute = [](const miacode::qml_ui::QmlTouchPadAuthoringContext& context) {
        return miacode::qml_ui::resolveTouchPadAuthoringRoute(true, context.accepts());
    };
    expect(rejectedRoute({3, 3, 42, 42, false, false})
                   == miacode::qml_ui::TouchPadAuthoringRoute::Reject
               && rejectedRoute({3, 3, 42, 41, true, false})
                   == miacode::qml_ui::TouchPadAuthoringRoute::Reject
               && rejectedRoute({3, 3, 42, 42, true, true})
                   == miacode::qml_ui::TouchPadAuthoringRoute::Reject
               && miacode::qml_ui::resolveTouchPadAuthoringRoute(false, false)
                   == miacode::qml_ui::TouchPadAuthoringRoute::Legacy,
           QStringLiteral("an installed QML handler rejects unfocused, stale, and IME touch edits without legacy fallback"), out, &failed);
    const auto createdBookmark = controller.createBookmarkForQml(QStringLiteral("1,2,"), 1, QStringLiteral("intro"));
    expect(createdBookmark.value(QStringLiteral("hasEdit")).toBool()
               && createdBookmark.value(QStringLiteral("replacementText")).toString().contains(QStringLiteral("[intro]")),
           QStringLiteral("bookmark creation uses one C++ edit transaction"), out, &failed);
    const auto listedBookmarks = controller.bookmarksForQml(QStringLiteral("1,2, || [intro]"));
    expect(listedBookmarks.size() == 1 && listedBookmarks.first().toMap().value(QStringLiteral("line")).toInt() == 1,
           QStringLiteral("bookmark listing delegates comment marker recognition to C++"), out, &failed);
    const auto parsedBookmark = miacode::editor::parseBookmarkComment(QStringLiteral("1,2, || [intro]"));
    expect(parsedBookmark.has_value() && parsedBookmark->title == QStringLiteral("intro")
               && miacode::editor::renameBookmarkComment(QStringLiteral("1,2, || [intro]"), QStringLiteral("verse"))
                      == QStringLiteral("1,2, || [verse]")
               && miacode::editor::removeBookmarkComment(QStringLiteral("1,2, || [intro]")) == QStringLiteral("1,2,"),
           QStringLiteral("bookmark parse and serialization stay in BookmarkCommentSyntax"), out, &failed);

    InputMethodReceiver inputTarget;
    miacode::qml_ui::QmlEditorInputBridge inputBridge;
    inputBridge.setTarget(&inputTarget);
    int commits = 0;
    int compositionChanges = 0;
    bool composing = false;
    QString committedText;
    QObject::connect(&inputBridge, &miacode::qml_ui::QmlEditorInputBridge::imeCommitted,
                     [&commits, &committedText](const QString& value) { ++commits; committedText = value; });
    QObject::connect(&inputBridge, &miacode::qml_ui::QmlEditorInputBridge::imeComposingChanged,
                     [&compositionChanges, &composing](bool value) { ++compositionChanges; composing = value; });
    QInputMethodEvent preeditEvent(QStringLiteral("preedit"), {});
    QCoreApplication::sendEvent(&inputTarget, &preeditEvent);
    expect(commits == 0 && composing && compositionChanges == 1,
           QStringLiteral("IME preedit passes through without a transaction but marks composition"), out, &failed);
    const QList<QInputMethodEvent::Attribute> attributes{
        QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, 1, QVariant())};
    QInputMethodEvent imeCommit(QStringLiteral("候補"), attributes);
    imeCommit.setCommitString(QStringLiteral("【"));
    QCoreApplication::sendEvent(&inputTarget, &imeCommit);
    expect(commits == 1 && committedText == QStringLiteral("【")
               && inputTarget.inputMethodEvents == 2 && inputTarget.commit.isEmpty()
               && inputTarget.preedit == QStringLiteral("候補")
               && inputTarget.attributes == attributes && composing,
           QStringLiteral("IME bridge strips commit once while forwarding composition and attributes"), out, &failed);

    QFile sourceEditor(QStringLiteral("src/app/qml_ui/editor/SourceEditor.qml"));
    expect(sourceEditor.open(QIODevice::ReadOnly), QStringLiteral("SourceEditor QML is available to wiring test"), out, &failed);
    const QString source = QString::fromUtf8(sourceEditor.readAll());
    expect(source.contains(QStringLiteral("processImeCommitForQml"))
               && source.contains(QStringLiteral("processPasteForQml"))
               && source.contains(QStringLiteral("QmlEditorInputBridge"))
               && source.contains(QStringLiteral("editorController.canUndo"))
               && source.contains(QStringLiteral("FindReplaceBar"))
               && source.contains(QStringLiteral("bookmarksForQml"))
               && source.contains(QStringLiteral("publishCaretForQml"))
               && source.contains(QStringLiteral("publishEditorCaret"))
               && source.contains(QStringLiteral("setQmlEditorInteraction"))
               && source.contains(QStringLiteral("recordQmlTransaction")),
           QStringLiteral("SourceEditor routes editor workflow and revision-safe caret through controller"), out, &failed);
    return failed == 0 ? 0 : 1;
}
