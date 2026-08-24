#include "app/qml_ui/QmlEditorController.h"
#include "app/qml_ui/QmlEditorInputBridge.h"
#include "app/qml_ui/SimaiSyntaxHighlighter.h"
#include "app/qml_ui/QmlEditorNavigationBridge.h"
#include "app/qml_ui/QmlTouchPadAuthoringBridge.h"
#include "editor/BookmarkCommentSyntax.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPoint>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QTest>
#include <QVariant>
#include <QTextStream>

#include <cmath>
#include <memory>

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

bool verifyQmlTextAreaKeyRouting(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls

        TextArea {
            id: editor
            objectName: "editor"
            text: "stable"
            focus: true
            Keys.priority: Keys.BeforeItem
            property int controllerEventCount: 0
            property var controllerKeys: []
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Down || event.key === Qt.Key_Escape) {
                    controllerEventCount += 1
                    controllerKeys = controllerKeys.concat([event.key])
                    event.accepted = true
                }
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/QmlEditorControllerSpec.qml")));
    if (!expect(component.isReady(), QStringLiteral("QML TextArea key-routing harness compiles"), out, failed)) {
        for (const QQmlError& error : component.errors()) {
            out << error.toString() << '\n';
        }
        return false;
    }

    std::unique_ptr<QObject> root(component.create());
    auto* editor = qobject_cast<QQuickItem*>(root.get());
    if (!expect(editor != nullptr, QStringLiteral("QML TextArea key-routing harness creates an item"), out, failed)) {
        return false;
    }
    editor->setFocus(true);
    const QList<int> keys{Qt::Key_Tab, Qt::Key_Return, Qt::Key_Down, Qt::Key_Escape};
    for (const int key : keys) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &event);
    }
    const QVariantList deliveredKeys = root->property("controllerKeys").toList();
    bool deliveredInOrder = deliveredKeys.size() == keys.size();
    for (qsizetype index = 0; deliveredInOrder && index < keys.size(); ++index) {
        deliveredInOrder = deliveredKeys.at(index).toInt() == keys.at(index);
    }
    return expect(root->property("controllerEventCount").toInt() == keys.size()
                      && deliveredInOrder
                      && root->property("text").toString() == QStringLiteral("stable"),
                  QStringLiteral("QML editor key controller receives completion keys before TextArea defaults"),
                  out, failed);
}
// A key the policy refuses must not reach TextArea's raw insertion fallback.
// Qt guards Ctrl-modified characters in QQuickTextControl but not Meta-modified
// ones, so before the suppressFallbackInsert contract a macOS 物理 Control+Z
// (Qt::MetaModifier) typed a literal "z" into the chart.
bool verifyCommandModifiedKeysNeverTypeText(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    miacode::qml_ui::QmlEditorController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);
    QQmlComponent component(&engine);
    // Mirrors SourceEditor.qml's key chain: transaction first, then the
    // suppress-fallback guard, exactly as the visible editor routes it.
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls

        TextArea {
            id: editor
            objectName: "editor"
            text: "abc"
            focus: true
            property int undoRequests: 0
            function applyEditorTransaction(transaction) {
                if (!transaction.consumed)
                    return false
                if (transaction.hasEdit) {
                    editor.remove(transaction.replacementStart, transaction.replacementEnd)
                    editor.insert(transaction.replacementStart, transaction.replacementText)
                    editor.select(transaction.anchor, transaction.position)
                }
                return true
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                if (event.matches(StandardKey.Undo)) {
                    editor.undoRequests += 1
                    event.accepted = true
                    return
                }
                const transaction = editorController.processKeyForQml(
                    editor.text, editor.selectionStart, editor.selectionEnd,
                    event.text, event.key, event.modifiers)
                if (editor.applyEditorTransaction(transaction)) {
                    event.accepted = true
                    return
                }
                if (transaction.suppressFallbackInsert)
                    event.accepted = true
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/QmlEditorFallbackSpec.qml")));
    if (!expect(component.isReady(),
                QStringLiteral("QML command-modifier harness compiles"), out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> root(component.create());
    auto* editor = qobject_cast<QQuickItem*>(root.get());
    if (!expect(editor != nullptr,
                QStringLiteral("QML command-modifier harness creates an item"), out, failed)) {
        return false;
    }
    editor->setFocus(true);

    struct KeyCase {
        QString label;
        int key;
        Qt::KeyboardModifiers modifiers;
        QString text;
    };
    const KeyCase keyCases[] = {
        {QStringLiteral("Meta+Z"), Qt::Key_Z, Qt::MetaModifier, QStringLiteral("z")},
        {QStringLiteral("Meta+C"), Qt::Key_C, Qt::MetaModifier, QStringLiteral("c")},
        {QStringLiteral("Meta+A"), Qt::Key_A, Qt::MetaModifier, QStringLiteral("a")},
        {QStringLiteral("Ctrl+B"), Qt::Key_B, Qt::ControlModifier, QStringLiteral("b")},
    };
    bool unchanged = true;
    QStringList typed;
    for (const KeyCase& keyCase : keyCases) {
        editor->setProperty("text", QStringLiteral("abc"));
        QKeyEvent event(QEvent::KeyPress, keyCase.key, keyCase.modifiers, keyCase.text);
        QCoreApplication::sendEvent(editor, &event);
        const QString after = editor->property("text").toString();
        if (after != QStringLiteral("abc")) {
            unchanged = false;
            typed.append(QStringLiteral("%1->%2").arg(keyCase.label, after));
        }
    }
    if (!typed.isEmpty()) out << "  typed literal text: " << typed.join(QStringLiteral(", ")) << '\n';
    expect(unchanged,
           QStringLiteral("command-modified keys never type a literal character into the chart"),
           out, failed);

    editor->setProperty("text", QStringLiteral("abc"));
    QKeyEvent undoEvent(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier, QStringLiteral("z"));
    QCoreApplication::sendEvent(editor, &undoEvent);
    return expect(root->property("undoRequests").toInt() == 1
                      && editor->property("text").toString() == QStringLiteral("abc"),
                  QStringLiteral("the undo shortcut still reaches the QML undo route"), out, failed);
}
// Loads the real CompletionPopup.qml. Before this contract the delegate asked
// for a bare `index` next to a required modelData, which QQmlDelegateModel
// answers with "index is not defined", so no candidate ever highlighted and
// ↑ ↓ Tab produced no visible feedback; and a vertical ListView reports
// contentWidth -1, which pinned the popup at its minimum width.
bool verifyCompletionPopupPresentsTheSelectedCandidate(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    miacode::qml_ui::QmlEditorController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls
        import MiaCode.UI

        Window {
            id: win
            width: 640; height: 480; visible: true
            property alias popup: pop
            property int acceptedFromPopup: 0
            Item {
                id: fakeEditor
                objectName: "fakeEditor"
                x: 40; y: 300; width: 400; height: 120
                property rect cursorRectangle: Qt.rect(10, 20, 1, 18)
                function acceptCompletionFromPopup() { win.acceptedFromPopup += 1 }
            }
            CompletionPopup {
                id: pop
                editor: fakeEditor
                controller: editorController
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/CompletionPopupSpec.qml")));
    if (!expect(component.isReady(),
                QStringLiteral("real CompletionPopup.qml loads in the spec harness"), out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    if (!expect(window != nullptr, QStringLiteral("CompletionPopup harness creates a window"), out, failed)) {
        return false;
    }
    window->show();
    QTest::qWaitForWindowExposed(window);

    auto* popup = root->property("popup").value<QObject*>();
    if (!expect(popup != nullptr, QStringLiteral("CompletionPopup instance is reachable"), out, failed)) {
        return false;
    }

    controller.setWholeBpm(QStringLiteral("180"));
    controller.processKey(QString(), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft, Qt::NoModifier);
    QCoreApplication::processEvents();
    expect(popup->property("visible").toBool() && controller.completionCandidates().size() > 1,
           QStringLiteral("an open completion session shows the popup"), out, failed);

    const auto highlightedRow = [popup]() {
        auto* content = popup->property("contentItem").value<QQuickItem*>();
        if (content == nullptr) return -1;
        int highlighted = -1;
        int found = 0;
        for (QQuickItem* child : content->childItems()) {
            for (QQuickItem* row : child->childItems()) {
                const QVariant flag = row->property("highlighted");
                if (!flag.isValid()) continue;
                ++found;
                if (flag.toBool()) highlighted = row->property("index").toInt();
            }
        }
        return found == 0 ? -2 : highlighted;
    };
    const int firstHighlight = highlightedRow();
    expect(firstHighlight == controller.completionIndex(),
           QStringLiteral("the popup highlights the controller's current candidate"), out, failed);

    controller.moveCompletionSelection(1);
    QCoreApplication::processEvents();
    expect(highlightedRow() == controller.completionIndex()
               && controller.completionIndex() == 1,
           QStringLiteral("keyboard navigation moves the visible highlight"), out, failed);

    // The candidate list must drive the popup width. A vertical ListView
    // reports contentWidth -1, so the old `implicitWidth: contentWidth` binding
    // collapsed every session onto the same minimum-width popup.
    const qreal squareWidth = popup->property("candidatesWidth").toReal();
    const qreal popupWidth = popup->property("width").toReal();
    const qreal leftPadding = popup->property("leftPadding").toReal();
    const qreal rightPadding = popup->property("rightPadding").toReal();
    expect(squareWidth > 0.0
               && qFuzzyCompare(popupWidth + 1.0,
                                qMax(120.0, std::ceil(squareWidth) + leftPadding + rightPadding + 24.0) + 1.0),
           QStringLiteral("popup width is measured from its candidates, not a -1 content width"),
           out, failed);
    controller.closeCompletion();
    // Hold candidates carry the whole "[8:1]" token, one glyph wider than the
    // "8:1]" duration tokens, so a wider session must widen the measurement.
    controller.processKey(QString(), 0, 0, QStringLiteral("h"), Qt::Key_H, Qt::NoModifier);
    QCoreApplication::processEvents();
    const qreal holdWidth = popup->property("candidatesWidth").toReal();
    expect(holdWidth > squareWidth,
           QStringLiteral("a wider candidate session widens the measured popup width"), out, failed);
    controller.closeCompletion();
    controller.processKey(QString(), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft, Qt::NoModifier);
    QCoreApplication::processEvents();

    // Anchoring: below the caret normally, flipped above when the list would
    // otherwise hang off the bottom of the overlay.
    auto* fakeEditor = window->findChild<QQuickItem*>(QStringLiteral("fakeEditor"));
    if (!expect(fakeEditor != nullptr, QStringLiteral("popup anchor harness exposes its editor"), out, failed)) {
        return false;
    }
    fakeEditor->setY(40);
    fakeEditor->setProperty("cursorRectangle", QVariant::fromValue(QRectF(10, 20, 1, 18)));
    QCoreApplication::processEvents();
    const qreal anchoredBelow = popup->property("y").toReal();
    expect(qFuzzyCompare(anchoredBelow + 1.0, 40.0 + 20.0 + 18.0 + 1.0),
           QStringLiteral("the popup anchors under the caret it belongs to"), out, failed);

    fakeEditor->setY(window->height() - 30);
    QCoreApplication::processEvents();
    const qreal anchoredFlipped = popup->property("y").toReal();
    const qreal flippedHeight = popup->property("height").toReal();
    expect(anchoredFlipped >= 0.0 && anchoredFlipped + flippedHeight <= window->height(),
           QStringLiteral("a caret near the bottom flips the popup above it instead of off-screen"),
           out, failed);
    controller.closeCompletion();
    return true;
}
// Loads the real SourceEditor.qml against the real controller. TextArea takes
// the mouse grab on press, so the TapHandler the editor used to carry never
// completed a tap inside it and the body context menu was dead on the desktop.
bool verifyEditorPointerRoutes(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    miacode::qml_ui::QmlEditorController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("sharedController"), &controller);
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls
        import MiaCode.UI

        Window {
            id: win
            width: 720; height: 480; visible: true

            // Narrow stand-in for QmlDocumentModel: only the members
            // SourceEditor actually consumes, so the real editor component runs
            // unmodified against a deterministic document.
            QtObject {
                id: session
                property string chartText: "1,2,3,4,\n5,6,7,8,\n"
                property string metadataSourceText: ""
                property int currentDifficultyId: 3
                property real documentRevision: 42
                property bool validationPending: false
                property real validationRevision: 42
                property var syntaxIssues: []
                signal chartTextChanged2()
                signal metadataSourceChanged()
                signal documentStateChanged()
                signal documentReplaced()
                signal qmlEditorNavigationRequested(int difficultyId, real revision, int line,
                                                    int column, int endLine, int endColumn,
                                                    bool selectToken, bool focusEditor, bool centerView)
                signal qmlTouchPadAuthoringRequested(string pad, bool useBacktickSeparator,
                                                     int difficultyId, real revision,
                                                     int anchor, int position)
                signal qmlEditorFollowDecorationChanged(bool active, int difficultyId, real revision,
                                                        int startLine, int startColumn, int endLine,
                                                        int endColumn, int cursorLine, int cursorColumn,
                                                        bool ensureVisible)
                function chartPosition(line, column) {
                    const lines = session.chartText.split("\n")
                    let offset = 0
                    for (let i = 0; i < Math.min(line - 1, lines.length); ++i)
                        offset += lines[i].length + 1
                    return offset + Math.max(0, column - 1)
                }
                function setQmlEditorNavigationReadiness(a, b, c, d) {}
                function setQmlEditorInteraction(a, b, c, d, e, f) {}
                function setQmlTouchPadAuthoringCtrlHold(a) {}
                function setTouchPadAuthoringPreviewAnchor(a, b, c, d) { return true }
                function publishEditorCaret(a, b, c, d) { return true }
                function selectDifficulty(a) {}
                property int seekRequests: 0
                property int seekLine: -1
                property int seekColumn: -1
                property int seekDifficultyId: -1
                property real seekRevision: -1
                function seekPreviewToEditorLocation(difficultyId, revision, line, column) {
                    session.seekRequests += 1
                    session.seekDifficultyId = difficultyId
                    session.seekRevision = revision
                    session.seekLine = line
                    session.seekColumn = column
                    return true
                }
            }

            QtObject {
                id: view
                property int editorCursorLine: 1
                property int editorCursorColumn: 1
            }

            property alias editor: sourceEditor
            property alias documentStub: session
            SourceEditor {
                id: sourceEditor
                objectName: "sourceEditor"
                anchors.fill: parent
                navigationVisible: true
                viewState: view
                documentSession: session
                editorController: sharedController
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/SourceEditorContextMenuSpec.qml")));
    if (!expect(component.isReady(),
                QStringLiteral("real SourceEditor.qml loads in the spec harness"), out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    if (!expect(window != nullptr, QStringLiteral("SourceEditor harness creates a window"), out, failed)) {
        return false;
    }
    window->show();
    QTest::qWaitForWindowExposed(window);
    QCoreApplication::processEvents();

    QObject* menu = window->findChild<QObject*>(QStringLiteral("editorContextMenu"));
    if (!expect(menu != nullptr, QStringLiteral("the editor exposes a context menu"), out, failed)) {
        return false;
    }
    expect(!menu->property("visible").toBool(),
           QStringLiteral("the editor context menu starts closed"), out, failed);

    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, QPoint(200, 60));
    QCoreApplication::processEvents();
    const bool openedByMouse = menu->property("visible").toBool();
    expect(openedByMouse, QStringLiteral("a right-click in the editor body opens the context menu"),
           out, failed);
    QMetaObject::invokeMethod(menu, "close");
    QCoreApplication::processEvents();

    auto* editorItem = window->findChild<QQuickItem*>(QStringLiteral("sourceEditor"));
    if (!expect(editorItem != nullptr, QStringLiteral("SourceEditor item is reachable"), out, failed)) {
        return false;
    }
    QMetaObject::invokeMethod(editorItem, "openContextMenuAtCaret");
    QCoreApplication::processEvents();
    expect(menu->property("visible").toBool(),
           QStringLiteral("the keyboard context-menu route opens the same menu at the caret"),
           out, failed);
    QMetaObject::invokeMethod(menu, "close");
    QCoreApplication::processEvents();

    // A Ctrl/Command click must seek the preview to the clicked token, not
    // merely move the timeline cursor. v1 does this from an event filter on the
    // hidden widget viewport, which the visible QML editor never reached.
    auto* session = root->property("documentStub").value<QObject*>();
    if (!expect(session != nullptr, QStringLiteral("preview seek harness exposes its document stub"),
                out, failed)) {
        return false;
    }
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(80, 30));
    QCoreApplication::processEvents();
    expect(session->property("seekRequests").toInt() == 0,
           QStringLiteral("a plain click in the editor does not seek the preview"), out, failed);

    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 30));
    QCoreApplication::processEvents();
    const int caretLine = editorItem->property("viewState").value<QObject*>() != nullptr
        ? editorItem->property("viewState").value<QObject*>()->property("editorCursorLine").toInt()
        : -1;
    const int caretColumn = editorItem->property("viewState").value<QObject*>() != nullptr
        ? editorItem->property("viewState").value<QObject*>()->property("editorCursorColumn").toInt()
        : -1;
    expect(session->property("seekRequests").toInt() == 1
               && session->property("seekDifficultyId").toInt() == 3
               && session->property("seekRevision").toInt() == 42
               && session->property("seekLine").toInt() == caretLine
               && session->property("seekColumn").toInt() == caretColumn,
           QStringLiteral("a Ctrl/Command click seeks the preview to the caret it just placed"),
           out, failed);

    // Paused preview follow: a decoration, not a caret move. Before this
    // channel existed the paused branch only painted the hidden widget, so a
    // seek while paused produced nothing at all in the visible QML editor.
    const auto sendDecoration = [session](bool active, int difficultyId, double revision,
                                          int startLine, int startColumn, int endLine,
                                          int endColumn, int cursorLine, int cursorColumn,
                                          bool ensureVisible) {
        QMetaObject::invokeMethod(
            session, "qmlEditorFollowDecorationChanged", Q_ARG(bool, active),
            Q_ARG(int, difficultyId), Q_ARG(double, revision), Q_ARG(int, startLine),
            Q_ARG(int, startColumn), Q_ARG(int, endLine), Q_ARG(int, endColumn),
            Q_ARG(int, cursorLine), Q_ARG(int, cursorColumn), Q_ARG(bool, ensureVisible));
        QCoreApplication::processEvents();
    };
    sendDecoration(true, 3, 42, 2, 3, 2, 5, 2, 3, false);
    const bool decorated = editorItem->property("followDecorationActive").toBool();
    // Line 2 starts at offset 9 ("1,2,3,4,\n"), so column 3 is offset 11 and the
    // inclusive end column 5 is the exclusive offset 14.
    expect(decorated
               && editorItem->property("followDecorationStart").toInt() == 11
               && editorItem->property("followDecorationEnd").toInt() == 14
               && editorItem->property("followDecorationCursor").toInt() == 11,
           QStringLiteral("a paused follow decoration reaches the visible QML editor"), out, failed);

    sendDecoration(true, 3, 41, 1, 1, 1, 2, 1, 1, false);
    expect(!editorItem->property("followDecorationActive").toBool(),
           QStringLiteral("a stale-revision follow decoration is dropped, not painted"), out, failed);

    sendDecoration(true, 3, 42, 2, 3, 2, 5, 2, 3, false);
    sendDecoration(false, -1, 0, 1, 1, 1, 1, 1, 1, false);
    return expect(!editorItem->property("followDecorationActive").toBool(),
                  QStringLiteral("clearing preview follow removes the decoration"), out, failed);
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;
    // MiaCode.UI's C++ elements are registered by the app module, which a spec
    // executable does not link; register them for the mirrored import path.
    qmlRegisterType<SimaiSyntaxHighlighter>("MiaCode.UI", 1, 0, "SimaiSyntaxHighlighter");
    qmlRegisterType<miacode::qml_ui::QmlEditorInputBridge>(
        "MiaCode.UI", 1, 0, "QmlEditorInputBridge");
    verifyQmlTextAreaKeyRouting(out, &failed);
    verifyCommandModifiedKeysNeverTypeText(out, &failed);
    verifyCompletionPopupPresentsTheSelectedCandidate(out, &failed);
    verifyEditorPointerRoutes(out, &failed);
    miacode::qml_ui::QmlEditorController controller;
    const auto opening = controller.processKey(QString(), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft, Qt::NoModifier);
    expect(opening.consumed && opening.transaction.replacementText == QStringLiteral("[]") && opening.transaction.position == 1, QStringLiteral("policy key input produces one QML edit transaction"), out, &failed);
    expect(controller.completionActive() && controller.completionIndex() == 0 && controller.completionCandidates().contains(QStringLiteral("8:1]")), QStringLiteral("opening bracket publishes a completion session"), out, &failed);
    controller.moveCompletionSelection(1);
    expect(controller.completionIndex() == 1, QStringLiteral("completion navigation updates the selected index"), out, &failed);
    controller.selectCompletionIndex(0);
    const auto filter = controller.processKey(opening.transaction.text, 1, 1, QStringLiteral("8"), Qt::Key_8, Qt::NoModifier);
    expect(filter.transaction.text == QStringLiteral("[8]") && controller.completionCandidates() == QStringList{QStringLiteral("8:1]")} && controller.completionIndex() == 0, QStringLiteral("post-edit completion filter keeps a valid selection index"), out, &failed);
    const auto accepted = controller.processKey(filter.transaction.text, 2, 2, QString(), Qt::Key_Tab, Qt::NoModifier);
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
    const auto touchTransaction = controller.touchPadAuthoringForQml(
        QStringLiteral("1,,"), 2, 2, QStringLiteral("A1"), false);
    expect(touchTransaction.value(QStringLiteral("consumed")).toBool()
               && touchTransaction.value(QStringLiteral("hasEdit")).toBool()
               && touchTransaction.value(QStringLiteral("undoGroup")).toBool()
               && touchTransaction.value(QStringLiteral("replacementText")).toString()
                      == QStringLiteral("A1")
               && touchTransaction.value(QStringLiteral("touchTokenStart")).toInt() == 2,
           QStringLiteral("Ctrl touch authoring is returned as one editor transaction with its token anchor"), out, &failed);
    const miacode::qml_ui::QmlEditorNavigationReadiness decorationReadiness{3, 42, true, false};
    const miacode::qml_ui::QmlEditorFollowDecoration liveDecoration{
        true, 3, 42, 2, 3, 2, 5, 2, 3, true};
    int deliveredDecorations = 0;
    bool lastDecorationActive = false;
    const auto routeDecoration = [&deliveredDecorations, &lastDecorationActive](
                                     const miacode::qml_ui::QmlEditorFollowDecoration& decoration,
                                     const miacode::qml_ui::QmlEditorNavigationReadiness& readiness) {
        return miacode::qml_ui::routeQmlEditorFollowDecoration(
            decoration, readiness, 3, 42,
            [&deliveredDecorations, &lastDecorationActive](
                const miacode::qml_ui::QmlEditorFollowDecoration& routed) {
                ++deliveredDecorations;
                lastDecorationActive = routed.active;
            });
    };
    expect(routeDecoration(liveDecoration, decorationReadiness) && lastDecorationActive
               && !routeDecoration(liveDecoration, {3, 42, false, false}) && !lastDecorationActive
               && !routeDecoration(liveDecoration, {3, 42, true, true}) && !lastDecorationActive
               && !routeDecoration({true, 3, 41, 2, 3, 2, 5, 2, 3, true}, decorationReadiness)
               && !routeDecoration({true, 4, 42, 2, 3, 2, 5, 2, 3, true}, decorationReadiness)
               && deliveredDecorations == 5,
           QStringLiteral("a follow decoration always reaches QML but only paints on a visible, current chart source"),
           out, &failed);

    const miacode::qml_ui::QmlEditorNavigationRequest navigationRequest{
        3, 42, 7, 4, 8, 9, true, false, true};
    expect(navigationRequest.accepts(3, 42)
               && !navigationRequest.accepts(4, 42)
               && !navigationRequest.accepts(3, 41)
               && !miacode::qml_ui::QmlEditorNavigationRequest{
                   3, 42, 8, 4, 7, 9, true, false, true}.accepts(3, 42),
           QStringLiteral("reverse editor navigation rejects a different difficulty, stale revision, or backwards range"), out, &failed);
    int deliveredNavigationRequests = 0;
    const auto routeNavigation = [&navigationRequest, &deliveredNavigationRequests](
                                     const miacode::qml_ui::QmlEditorNavigationReadiness& readiness) {
        return miacode::qml_ui::routeQmlEditorNavigation(
            navigationRequest, readiness, 3, 42,
            [&deliveredNavigationRequests](const miacode::qml_ui::QmlEditorNavigationRequest&) {
                ++deliveredNavigationRequests;
            });
    };
    expect(!routeNavigation({3, 42, false, false})
               && deliveredNavigationRequests == 0
               && !routeNavigation({3, 42, true, true})
               && deliveredNavigationRequests == 0
               && routeNavigation({3, 42, true, false})
               && deliveredNavigationRequests == 1,
           QStringLiteral("reverse navigation acknowledges only a visible chart source, never hidden or metadata editors"),
           out, &failed);
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
               && source.contains(QStringLiteral("recordQmlTransaction"))
               && source.contains(QStringLiteral("editorContextMenu"))
               && source.contains(QStringLiteral("centerCursorInView"))
               && source.contains(QStringLiteral("Keys.priority: Keys.BeforeItem"))
               && source.contains(QStringLiteral("transaction.suppressFallbackInsert"))
               && source.contains(QStringLiteral("setQmlTouchPadAuthoringCtrlHold"))
               && source.contains(QStringLiteral("setQmlEditorNavigationReadiness"))
               && source.contains(QStringLiteral("onQmlEditorNavigationRequested"))
               && source.contains(QStringLiteral("onQmlTouchPadAuthoringRequested"))
               && source.contains(QStringLiteral("touchPadAuthoringForQml"))
               && source.count(QStringLiteral("onJumpRequested: root.jumpToLine(line)")) == 1,
           QStringLiteral("SourceEditor routes keyboard completion, context actions, reverse navigation, and revision-safe touch editing"), out, &failed);
    QFile difficultyList(QStringLiteral("src/app/qml_ui/sidebar/DifficultyList.qml"));
    expect(difficultyList.open(QIODevice::ReadOnly), QStringLiteral("DifficultyList QML is available to bookmark sidebar wiring test"), out, &failed);
    const QString sidebarSource = QString::fromUtf8(difficultyList.readAll());
    expect(sidebarSource.contains(QStringLiteral("bookmarkGeneration"))
               && sidebarSource.contains(QStringLiteral("bookmarksForDifficulty"))
               && sidebarSource.contains(QStringLiteral("navigateToBookmark")),
           QStringLiteral("bookmarks are grouped under their difficulty in the sidebar instead of overlaying the editor"), out, &failed);
    QFile documentModelSource(QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    QFile mainWindowHeader(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    expect(documentModelSource.open(QIODevice::ReadOnly) && mainWindowHeader.open(QIODevice::ReadOnly),
           QStringLiteral("QML touch-pad ownership boundaries are available to wiring test"), out, &failed);
    const QString documentSource = QString::fromUtf8(documentModelSource.readAll());
    const QString mainWindowSource = QString::fromUtf8(mainWindowHeader.readAll());
    expect(documentSource.contains(QStringLiteral("setQmlTouchPadAuthoringCtrlHold(active && context.accepts())"))
               && mainWindowSource.contains(
                   QStringLiteral("void setQmlTouchPadAuthoringCtrlHold(bool active);")),
           QStringLiteral("QML Ctrl authoring arms preview through a public narrow MainWindow bridge"), out, &failed);
    QFile followSyncSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.TimelinePreviewFollowSync.cpp"));
    expect(followSyncSource.open(QIODevice::ReadOnly),
           QStringLiteral("preview follow synchronization is available to bridge wiring test"), out, &failed);
    const QString followSource = QString::fromUtf8(followSyncSource.readAll());
    expect(followSource.contains(QStringLiteral("requestQmlEditorNavigation("))
               && followSource.contains(QStringLiteral("cached_qml_navigation")),
           QStringLiteral("preview follow replays cached bindings through the revision-safe QML editor sink"), out, &failed);
    return failed == 0 ? 0 : 1;
}
