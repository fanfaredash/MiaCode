#include "app/qml_ui/QmlEditorController.h"
#include "app/qml_ui/QmlEditorInputBridge.h"
#include "app/qml_ui/EditorTextStyle.h"
#include "app/qml_ui/SimaiSyntaxHighlighter.h"
#include "app/v2/ChartWorkspace.h"
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
#include <QVariantList>
#include <QVariantMap>
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
// QML warnings are the only place a binding loop is reported, so the spec has
// to observe the message stream rather than a return value.
QStringList* qmlWarnings()
{
    static QStringList messages;
    return &messages;
}

void captureQmlMessage(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type == QtWarningMsg) qmlWarnings()->append(message);
}

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

QString applyRestoredTransaction(QString current, const QVariantMap& transaction)
{
    if (!transaction.value(QStringLiteral("hasEdit")).toBool()) return current;
    const int start = transaction.value(QStringLiteral("replacementStart")).toInt();
    const int end = transaction.value(QStringLiteral("replacementEnd")).toInt();
    current.replace(start, end - start,
                    transaction.value(QStringLiteral("replacementText")).toString());
    return current;
}

bool verifyWorkspaceSavePointFollowsQmlUndo(
    miacode::qml_ui::QmlEditorController& controller,
    QTextStream& out, int* failed)
{
    using miacode::v2::ChartWorkspaceDocumentField;
    const QString openedChart = QStringLiteral("(120){4}1,");
    const QString editedChart = QStringLiteral("(120){4}2,");
    const QString source = QStringLiteral(
        "&title=opened\n&lv_5=12\n&inote_5=(120){4}1,\n");
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(source, QStringLiteral("chart.txt"), 5);

    controller.clearAllHistory();
    controller.recordQmlTransaction(
        openedChart, editedChart, 0, 0, editedChart.size(), editedChart.size());
    workspace.replaceActiveDifficultyChart(editedChart);
    QString restored = applyRestoredTransaction(
        editedChart, controller.undoQmlTransaction());
    workspace.replaceActiveDifficultyChart(restored);
    bool ok = expect(!workspace.snapshot().dirty && restored == openedChart,
                     QStringLiteral("QML undo back to opened text restores the workspace save point"),
                     out, failed);

    workspace.replaceActiveDifficultyChart(editedChart);
    workspace.markSaved();
    const QString postSaveEdit = QStringLiteral("(120){4}3,");
    controller.clearAllHistory();
    controller.recordQmlTransaction(
        editedChart, postSaveEdit, 0, 0, postSaveEdit.size(), postSaveEdit.size());
    workspace.replaceActiveDifficultyChart(postSaveEdit);
    restored = applyRestoredTransaction(
        postSaveEdit, controller.undoQmlTransaction());
    workspace.replaceActiveDifficultyChart(restored);
    ok &= expect(!workspace.snapshot().dirty && restored == editedChart,
                 QStringLiteral("QML undo back to post-save text restores the new workspace save point"),
                 out, failed);

    workspace.updateDocumentField(
        ChartWorkspaceDocumentField::Title, QStringLiteral("metadata-dirty"));
    controller.clearAllHistory();
    controller.recordQmlTransaction(
        editedChart, postSaveEdit, 0, 0, postSaveEdit.size(), postSaveEdit.size());
    workspace.replaceActiveDifficultyChart(postSaveEdit);
    restored = applyRestoredTransaction(
        postSaveEdit, controller.undoQmlTransaction());
    workspace.replaceActiveDifficultyChart(restored);
    ok &= expect(workspace.snapshot().dirty && restored == editedChart,
                 QStringLiteral("QML chart undo cannot clear an unrelated metadata mutation"),
                 out, failed);
    return ok;
}
// Backspace and Delete arrive from a real QML TextArea with control characters
// in QKeyEvent::text(). The smart policy may consume empty-pair Backspace, but
// ordinary deletion must remain unaccepted so QQuickTextControl performs it.
bool verifyDeletionControlCharactersUseNativeTextArea(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    miacode::qml_ui::QmlEditorController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls

        TextArea {
            id: editor
            objectName: "editor"
            focus: true
            property int policyEditCount: 0
            function applyEditorTransaction(transaction) {
                if (!transaction.consumed)
                    return false
                if (transaction.hasEdit) {
                    editor.remove(transaction.replacementStart, transaction.replacementEnd)
                    editor.insert(transaction.replacementStart, transaction.replacementText)
                    editor.select(transaction.anchor, transaction.position)
                    policyEditCount += 1
                }
                return true
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                const transaction = editorController.processKeyForQml(
                    editor.text, editor.selectionStart, editor.selectionEnd,
                    event.text, event.key, event.modifiers)
                if (editor.applyEditorTransaction(transaction))
                    event.accepted = true
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/QmlEditorDeletionSpec.qml")));
    if (!expect(component.isReady(), QStringLiteral("QML deletion-key harness compiles"), out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> root(component.create());
    auto* editor = qobject_cast<QQuickItem*>(root.get());
    if (!expect(editor != nullptr, QStringLiteral("QML deletion-key harness creates a TextArea"),
                out, failed)) {
        return false;
    }
    editor->setFocus(true);

    const auto resetEditor = [editor, root = root.get()](const QString& text, int caret) {
        root->setProperty("text", text);
        root->setProperty("policyEditCount", 0);
        return QMetaObject::invokeMethod(editor, "select", Q_ARG(int, caret), Q_ARG(int, caret));
    };
    const auto sendKey = [editor](int key, const QString& text) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(editor, &event);
        QCoreApplication::processEvents();
    };

    bool selectionReady = resetEditor(QStringLiteral("ab"), 1);
    sendKey(Qt::Key_Backspace, QStringLiteral("\b"));
    const bool nativeBackspace = root->property("text").toString() == QStringLiteral("b")
        && root->property("policyEditCount").toInt() == 0;

    selectionReady = resetEditor(QStringLiteral("ab"), 1) && selectionReady;
    sendKey(Qt::Key_Delete, QStringLiteral("\x7f"));
    const bool nativeDelete = root->property("text").toString() == QStringLiteral("a")
        && root->property("policyEditCount").toInt() == 0;

    selectionReady = resetEditor(QStringLiteral("[]"), 1) && selectionReady;
    sendKey(Qt::Key_Backspace, QStringLiteral("\b"));
    const bool pairedBackspace = root->property("text").toString().isEmpty()
        && root->property("policyEditCount").toInt() == 1;

    return expect(selectionReady && nativeBackspace && nativeDelete && pairedBackspace,
                  QStringLiteral("real QML Backspace/Delete keep native deletion while empty pairs delete together"),
                  out, failed);
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
                function chartPosition(line, column) {
                    const lines = session.chartText.split("\n")
                    let offset = 0
                    for (let i = 0; i < Math.min(line - 1, lines.length); ++i)
                        offset += lines[i].length + 1
                    return offset + Math.max(0, column - 1)
                }
                function selectDifficulty(a) {}
                function logEditorDocumentState(a, b, c, d, e) {}
                property int seekRequests: 0
                property int seekLine: -1
                property int seekColumn: -1
                property int seekDifficultyId: -1
                property real seekRevision: -1
            }

            QtObject {
                id: sync
                property bool followActive: false
                property int followDifficultyId: -1
                property real followRevision: 0
                property int followStart: 0
                property int followEnd: 0
                property int followCaret: 0
                property bool followReveal: false
                property bool followPlaybackActive: false
                signal navigationRequested(real sequence, int difficultyId, real revision,
                                           int start, int end, bool focusEditor, bool reveal)
                signal navigationFinished(real sequence, bool applied)
                signal followChanged()
                signal touchPadAuthoringRequested(string pad, bool useBacktickSeparator,
                                                  int difficultyId, real revision,
                                                  int anchor, int position)
                function setEditorReadiness(a, b, c, d) {}
                function setEditorContext(a, b, c, d, e, f, g, h, i) {}
                function acknowledgeNavigation(a, b) {}
                function setTouchPadControlHold(a) {}
                function setTouchPadPreviewAnchor(a, b, c, d) { return true }
                function beginPointerInteraction(a, b) { return true }
                function requestNavigation(a, b, c, d, e, f) { return 1 }
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
                syncController: sync
            }
            property alias syncStub: sync
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

    // Undo history belongs to a view. It must survive a controller-sourced text
    // sync (a backend push is not the user's edit), survive a switch to another
    // difficulty and back, and go only when the whole document is replaced.
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(80, 30));
    QCoreApplication::processEvents();
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_9, Qt::NoModifier, QStringLiteral("9"));
    auto* typedTarget = window->activeFocusItem();
    QCoreApplication::sendEvent(typedTarget != nullptr ? static_cast<QObject*>(typedTarget)
                                                       : static_cast<QObject*>(editorItem),
                                &typed);
    QCoreApplication::processEvents();
    if (!expect(controller.canUndo(),
                QStringLiteral("typing into the QML editor records an undo step"), out, failed)) {
        return false;
    }
    session->setProperty("chartText", QStringLiteral("1,2,3,4,\n5,6,7,8,\n9,"));
    QCoreApplication::processEvents();
    expect(controller.canUndo(),
           QStringLiteral("a controller-sourced text sync keeps the undo history"), out, failed);
    session->setProperty("currentDifficultyId", 4);
    session->setProperty("chartText", QStringLiteral("1,1,1,1,"));
    QCoreApplication::processEvents();
    expect(!controller.canUndo(),
           QStringLiteral("another difficulty starts on its own empty history"), out, failed);
    session->setProperty("currentDifficultyId", 3);
    session->setProperty("chartText", QStringLiteral("1,2,3,4,\n5,6,7,8,\n9,"));
    QCoreApplication::processEvents();
    // The one that used to fail: switching away and back used to throw the
    // history out in both directions.
    expect(controller.canUndo(),
           QStringLiteral("switching back finds that difficulty's history intact"), out, failed);
    controller.dropHistoryScope(QStringLiteral("difficulty:3"));
    expect(!controller.canUndo(),
           QStringLiteral("closing a tab takes its history with it"), out, failed);

    // Rehighlighting is a formatting pass, not an edit. setDiagnostics used to
    // rehighlight synchronously from inside a binding evaluation, and TextEdit
    // reports that as textChanged, which the editor wrote straight back to the
    // document the binding depends on — Qt reported it as a binding loop on
    // SimaiSyntaxHighlighter.diagnostics in the desktop capture.
    qmlWarnings()->clear();
    session->setProperty("syntaxIssues", QVariantList{
        QVariantMap{{QStringLiteral("line"), 1}, {QStringLiteral("column"), 1},
                    {QStringLiteral("endColumn"), 2}, {QStringLiteral("severity"), QStringLiteral("error")}}});
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const QStringList loops = qmlWarnings()->filter(QStringLiteral("Binding loop"));
    if (!loops.isEmpty()) out << "  " << loops.join(QStringLiteral(" | ")) << '\n';
    expect(loops.isEmpty(),
           QStringLiteral("publishing diagnostics does not loop through the document"), out, failed);

    // Replacing the document must reach the visible editor. The desktop report
    // was that after switching charts the editor still showed the outgoing
    // document's text while the timeline and the preview media had already
    // moved to the incoming one.
    session->setProperty("currentDifficultyId", 5);
    session->setProperty("chartText", QStringLiteral("9,9,9,9,\n"));
    QMetaObject::invokeMethod(session, "documentReplaced");
    QCoreApplication::processEvents();
    auto* replacedArea = editorItem->findChild<QQuickItem*>(QStringLiteral("sourceArea"));
    const QString shownAfterReplace = replacedArea != nullptr
        ? replacedArea->property("text").toString()
        : QStringLiteral("<no editor>");
    if (shownAfterReplace != QStringLiteral("9,9,9,9,\n")) {
        out << "  editor still shows: [" << shownAfterReplace << "]\n";
    }
    expect(shownAfterReplace == QStringLiteral("9,9,9,9,\n"),
           QStringLiteral("replacing the document refreshes the visible editor text"), out, failed);
    session->setProperty("currentDifficultyId", 3);
    session->setProperty("chartText", QStringLiteral("1,2,3,4,\n5,6,7,8,\n"));
    QCoreApplication::processEvents();

    // Paused preview follow: a decoration, not a caret move. Before this
    // channel existed the paused branch only painted the hidden widget, so a
    // seek while paused produced nothing at all in the visible QML editor.
    auto* sync = root->property("syncStub").value<QObject*>();
    const auto sendDecoration = [session, sync](bool active, int difficultyId, double revision,
                                                int startLine, int startColumn, int endLine,
                                                int endColumn, int cursorLine, int cursorColumn,
                                                bool ensureVisible) {
        const QString text = session->property("chartText").toString();
        const auto positionFor = [&text](int line, int column) {
            int position = 0;
            for (int currentLine = 1; currentLine < line; ++currentLine) {
                const int newline = text.indexOf(QLatin1Char('\n'), position);
                position = newline < 0 ? text.size() : newline + 1;
            }
            return qBound(position, position + qMax(0, column - 1), text.size());
        };
        sync->setProperty("followActive", active);
        sync->setProperty("followDifficultyId", difficultyId);
        sync->setProperty("followRevision", revision);
        sync->setProperty("followStart", positionFor(startLine, startColumn));
        sync->setProperty("followEnd", positionFor(endLine, endColumn + 1));
        sync->setProperty("followCaret", positionFor(cursorLine, cursorColumn));
        sync->setProperty("followReveal", ensureVisible);
        QMetaObject::invokeMethod(sync, "followChanged");
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
// Reproduces the desktop IME corruption. Applying the commit from inside the
// event filter mutates the document while QQuickTextEdit still holds a preedit,
// which makes QQuickTextControl commit that preedit through the platform input
// context, which re-delivers the SAME commit into the filter. The 2026-08-24
// capture recorded 632 events and 631 applied commits at depth 630 for a single
// keystroke. The bridge must apply one platform commit exactly once.
bool verifyReentrantImeCommitIsAppliedOnce(QTextStream& out, int* failed)
{
    InputMethodReceiver target;
    miacode::qml_ui::QmlEditorInputBridge bridge;
    bridge.setTarget(&target);

    int applied = 0;
    int redeliveries = 0;
    // Stands in for commitPreedit() -> QInputMethod::commit() -> the platform
    // re-issuing its marked text while the transaction is still running. Capped
    // so an unguarded bridge fails the assertion instead of exhausting the stack.
    constexpr int kMaxRedeliveries = 40;
    QObject::connect(&bridge, &miacode::qml_ui::QmlEditorInputBridge::imeCommitted,
                     [&applied, &redeliveries, &target](const QString& value) {
                         ++applied;
                         if (redeliveries >= kMaxRedeliveries) return;
                         ++redeliveries;
                         QInputMethodEvent redelivered;
                         redelivered.setCommitString(value);
                         QCoreApplication::sendEvent(&target, &redelivered);
                     });

    QInputMethodEvent preedit(QStringLiteral("a"), {});
    QCoreApplication::sendEvent(&target, &preedit);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("a"));
    QCoreApplication::sendEvent(&target, &commit);

    if (applied != 1) {
        out << "  applied=" << applied << " redeliveries=" << redeliveries << '\n';
    }
    expect(applied == 1,
           QStringLiteral("a re-entrant platform re-delivery applies the IME commit exactly once"),
           out, failed);

    // The re-delivered events must still reach the target with their commit
    // stripped, so nothing inserts the character behind the transaction's back.
    expect(target.commit.isEmpty(),
           QStringLiteral("a dropped re-entrant commit is stripped before the editor sees it"),
           out, failed);

    // A later, non-re-entrant commit is a real one and must still be applied.
    QInputMethodEvent secondCommit;
    secondCommit.setCommitString(QStringLiteral("b"));
    QCoreApplication::sendEvent(&target, &secondCommit);
    return expect(applied == 2,
                  QStringLiteral("a subsequent standalone commit is still applied"), out, failed);
}
// The visible editor tab set must always contain the active difficulty.
// resetEditorTabs(0) empties it, and syncDifficultyEditors only ever filters —
// it can never put a tab back — so any path that observes a momentarily
// unset difficulty leaves the editor with nothing to show and no route to
// recover. The document projection is delivered on a queued connection, and
// the desktop capture shows two documentReplaced 6 ms apart for one open, so
// that observation window is real.
bool verifyEditorTabsRecoverTheActiveDifficulty(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import MiaCode.UI
        ViewState { }
    )QML", QUrl(QStringLiteral("qrc:/ViewStateSpec.qml")));
    if (!expect(component.isReady(), QStringLiteral("real ViewState.qml loads in the spec harness"),
                out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> state(component.create());
    if (!expect(state != nullptr, QStringLiteral("ViewState harness instantiates"), out, failed)) {
        return false;
    }

    QMetaObject::invokeMethod(state.get(), "resetEditorTabs", Q_ARG(QVariant, 5));
    expect(state->property("openEditorTabs").toStringList() == QStringList{QStringLiteral("difficulty:5")}
               && state->property("activeEditorKey").toString() == QStringLiteral("difficulty:5"),
           QStringLiteral("resetting tabs for a difficulty opens its editor"), out, failed);

    // The race window: a replacement observed while the active difficulty is
    // momentarily unset.
    QMetaObject::invokeMethod(state.get(), "resetEditorTabs", Q_ARG(QVariant, 0));
    expect(state->property("openEditorTabs").toStringList().isEmpty(),
           QStringLiteral("an unset difficulty leaves no editor tab"), out, failed);

    const QVariantList difficulties{QVariantMap{{QStringLiteral("id"), 5}}};
    QMetaObject::invokeMethod(state.get(), "syncDifficultyEditors",
                              Q_ARG(QVariant, QVariant::fromValue(difficulties)),
                              Q_ARG(QVariant, 5));
    const QStringList recovered = state->property("openEditorTabs").toStringList();
    if (recovered.isEmpty()) out << "  editor tabs stayed empty; the editor has nothing to show\n";
    return expect(recovered.contains(QStringLiteral("difficulty:5"))
                      && state->property("activeEditorKey").toString()
                             == QStringLiteral("difficulty:5"),
                  QStringLiteral("syncing difficulties restores the active difficulty's editor"),
                  out, failed);
}

bool verifyEditorTabsCanBeDraggedToSwap(QTextStream& out, int* failed)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls
        import MiaCode.UI

        Window {
            width: 480
            height: 34
            visible: true

            QtObject {
                id: documentSession
                property var difficulties: [
                    { id: 5, label: "BASIC", designer: "" },
                    { id: 6, label: "ADVANCED", designer: "" },
                    { id: 7, label: "EXPERT", designer: "" }
                ]
                property var dirtyEditorKeys: []
                property string currentFilePath: ""
                property string currentFileName: ""
                function saveDifficultySection(difficultyId) { return true }
                function revertDifficultyChart(difficultyId) { return true }
            }

            QtObject { id: commands }

            ViewState {
                id: state
                objectName: "tabDragViewState"
                Component.onCompleted: {
                    openMetadataEditor()
                    openDifficultyEditor(5)
                    openDifficultyEditor(6)
                }
            }

            EditorTabBar {
                objectName: "editorTabBar"
                anchors.fill: parent
                viewState: state
                documentSession: documentSession
                commands: commands
            }
        }
    )QML", QUrl(QStringLiteral("qrc:/EditorTabBarDragSpec.qml")));
    if (!expect(component.isReady(),
                QStringLiteral("real EditorTabBar.qml loads in the drag harness"), out, failed)) {
        for (const QQmlError& error : component.errors()) out << error.toString() << '\n';
        return false;
    }
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    if (!expect(window != nullptr,
                QStringLiteral("editor-tab drag harness creates a window"), out, failed)) {
        return false;
    }
    window->show();
    Q_UNUSED(QTest::qWaitForWindowExposed(window));
    QCoreApplication::processEvents();

    QObject* state = window->findChild<QObject*>(QStringLiteral("tabDragViewState"));
    if (!expect(state != nullptr,
                QStringLiteral("editor-tab drag harness exposes its real view state"), out, failed)) {
        return false;
    }
    if (!expect(state->property("openEditorTabs").toStringList()
                    == QStringList{QStringLiteral("metadata"),
                                   QStringLiteral("difficulty:5"),
                                   QStringLiteral("difficulty:6")},
                QStringLiteral("the drag harness starts with metadata and two ordered difficulty tabs"), out, failed)) {
        return false;
    }

    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(80, 17));
    QTest::mouseMove(window, QPoint(240, 17), 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(240, 17));
    QCoreApplication::processEvents();

    return expect(state->property("openEditorTabs").toStringList()
                      == QStringList{QStringLiteral("difficulty:5"),
                                     QStringLiteral("metadata"),
                                     QStringLiteral("difficulty:6")}
                      && state->property("activeEditorKey").toString()
                          == QStringLiteral("difficulty:6"),
                  QStringLiteral("dragging metadata onto a difficulty swaps only their order"),
                  out, failed);
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qInstallMessageHandler(captureQmlMessage);
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;
    // MiaCode.UI's C++ elements are registered by the app module, which a spec
    // executable does not link; register them for the mirrored import path.
    qmlRegisterType<SimaiSyntaxHighlighter>("MiaCode.UI", 1, 0, "SimaiSyntaxHighlighter");
    qmlRegisterType<EditorTextStyle>("MiaCode.UI", 1, 0, "EditorTextStyle");
    qmlRegisterType<miacode::qml_ui::QmlEditorInputBridge>(
        "MiaCode.UI", 1, 0, "QmlEditorInputBridge");
    verifyQmlTextAreaKeyRouting(out, &failed);
    verifyCommandModifiedKeysNeverTypeText(out, &failed);
    verifyDeletionControlCharactersUseNativeTextArea(out, &failed);
    verifyReentrantImeCommitIsAppliedOnce(out, &failed);
    verifyCompletionPopupPresentsTheSelectedCandidate(out, &failed);
    verifyEditorPointerRoutes(out, &failed);
    verifyEditorTabsRecoverTheActiveDifficulty(out, &failed);
    verifyEditorTabsCanBeDraggedToSwap(out, &failed);
    miacode::qml_ui::QmlEditorController controller;
    verifyWorkspaceSavePointFollowsQmlUndo(controller, out, &failed);
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
    controller.clearAllHistory();
    controller.recordQmlTransaction(QStringLiteral("foo foo"), QStringLiteral("bar bar"),
                                    0, 0, 7, 7);
    const auto restored = controller.undoQmlTransaction();
    const auto repeated = controller.redoQmlTransaction();
    expect(restored.value(QStringLiteral("replacementText")).toString() == QStringLiteral("foo foo")
               && restored.value(QStringLiteral("anchor")).toInt() == 0
               && repeated.value(QStringLiteral("replacementText")).toString() == QStringLiteral("bar bar")
               && repeated.value(QStringLiteral("position")).toInt() == 7,
           QStringLiteral("controller restores a complete Replace All snapshot in one undo/redo step"), out, &failed);

    // An undo step must land on the text it restored: replace only the region
    // that actually differs, and select that region so the user sees what
    // changed. Replaying the whole document left the caret parked at a stale
    // offset with nothing selected.
    controller.clearAllHistory();
    controller.recordQmlTransaction(QStringLiteral("1,2,3,4,"), QStringLiteral("1,2,9,4,"), 4, 4, 5, 5);
    const auto narrowUndo = controller.undoQmlTransaction();
    expect(narrowUndo.value(QStringLiteral("replacementStart")).toInt() == 4
               && narrowUndo.value(QStringLiteral("replacementEnd")).toInt() == 5
               && narrowUndo.value(QStringLiteral("replacementText")).toString() == QStringLiteral("3")
               && narrowUndo.value(QStringLiteral("anchor")).toInt() == 4
               && narrowUndo.value(QStringLiteral("position")).toInt() == 5,
           QStringLiteral("undo replaces and selects only the region the edit changed"), out, &failed);
    const auto narrowRedo = controller.redoQmlTransaction();
    expect(narrowRedo.value(QStringLiteral("replacementStart")).toInt() == 4
               && narrowRedo.value(QStringLiteral("replacementEnd")).toInt() == 5
               && narrowRedo.value(QStringLiteral("replacementText")).toString() == QStringLiteral("9")
               && narrowRedo.value(QStringLiteral("anchor")).toInt() == 4
               && narrowRedo.value(QStringLiteral("position")).toInt() == 5,
           QStringLiteral("redo replaces and selects only the region it reapplied"), out, &failed);

    // Undoing an insertion restores nothing, so the caret collapses at the
    // point the inserted text used to start rather than selecting a neighbour.
    controller.clearAllHistory();
    controller.recordQmlTransaction(QStringLiteral("1,,"), QStringLiteral("1,A1,"), 2, 2, 4, 4);
    const auto insertionUndo = controller.undoQmlTransaction();
    expect(insertionUndo.value(QStringLiteral("replacementStart")).toInt() == 2
               && insertionUndo.value(QStringLiteral("replacementEnd")).toInt() == 4
               && insertionUndo.value(QStringLiteral("replacementText")).toString().isEmpty()
               && insertionUndo.value(QStringLiteral("anchor")).toInt() == 2
               && insertionUndo.value(QStringLiteral("position")).toInt() == 2,
           QStringLiteral("undoing an insertion collapses the caret where it began"), out, &failed);

    controller.setDocumentContext(3, 42);
    expect(controller.acceptsCaret(3, 42, false) && !controller.acceptsCaret(3, 41, false)
               && !controller.acceptsTouchAuthoring(3, 42, true, false)
               && controller.acceptsTouchAuthoring(3, 42, false, true),
           QStringLiteral("caret and Ctrl touch authoring reject stale revisions and IME composition"), out, &failed);
    const auto touchTransaction = controller.touchPadAuthoringForQml(
        QStringLiteral("1,,"), 2, 2, QStringLiteral("A1"), false);
    expect(touchTransaction.value(QStringLiteral("consumed")).toBool()
               && touchTransaction.value(QStringLiteral("hasEdit")).toBool()
               && touchTransaction.value(QStringLiteral("undoGroup")).toBool()
               && touchTransaction.value(QStringLiteral("replacementText")).toString()
                      == QStringLiteral("A1")
               && touchTransaction.value(QStringLiteral("touchTokenStart")).toInt() == 2,
           QStringLiteral("Ctrl touch authoring is returned as one editor transaction with its token anchor"), out, &failed);
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
    const auto meterComment = miacode::editor::parseBookmarkComment(QStringLiteral("1,2, || 4/4"));
    const auto emptyComment = miacode::editor::parseBookmarkComment(QStringLiteral("1,2, ||"));
    expect(meterComment.has_value() && meterComment->control
               && emptyComment.has_value() && emptyComment->control
               && !miacode::editor::parseBookmarkComment(QStringLiteral("1,2, || [intro]"))->control
               && !miacode::editor::parseBookmarkComment(QStringLiteral("1,2, ||| block")).has_value(),
           QStringLiteral("a bare meter or empty comment is control data, and ||| is not a bookmark marker at all"), out, &failed);
    expect(miacode::editor::parseBookmarkComment(QStringLiteral("1, || intro, 4 measures"))->title
               == QStringLiteral("intro"),
           QStringLiteral("an unlabelled comment names its bookmark by its first token, as the Widgets outline did"), out, &failed);
    const auto filteredBookmarks =
        controller.bookmarksForQml(QStringLiteral("1,2, || 4/4\n3,4, || [verse]\n5,6, ||"));
    expect(filteredBookmarks.size() == 1
               && filteredBookmarks.first().toMap().value(QStringLiteral("line")).toInt() == 2,
           QStringLiteral("bookmark listings skip control comments instead of showing 拍号 as sections"), out, &failed);

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
               && source.contains(QStringLiteral("syncController.setEditorContext"))
               && source.contains(QStringLiteral("recordQmlTransaction"))
               && source.contains(QStringLiteral("editorContextMenu"))
               && source.contains(QStringLiteral("centerCursorInView"))
               && source.contains(QStringLiteral("Keys.priority: Keys.BeforeItem"))
               && source.contains(QStringLiteral("transaction.suppressFallbackInsert"))
               && source.contains(QStringLiteral("syncController.setTouchPadControlHold"))
               && source.contains(QStringLiteral("syncController.setEditorReadiness"))
               && source.contains(QStringLiteral("onNavigationRequested"))
               && source.contains(QStringLiteral("onTouchPadAuthoringRequested"))
               && source.contains(QStringLiteral("touchPadAuthoringForQml"))
               && source.count(QStringLiteral("onJumpRequested: root.jumpToLine(line)")) == 1,
           QStringLiteral("SourceEditor routes keyboard completion, context actions, reverse navigation, and revision-safe touch editing"), out, &failed);
    QFile difficultyList(QStringLiteral("src/app/qml_ui/sidebar/DifficultyList.qml"));
    expect(difficultyList.open(QIODevice::ReadOnly), QStringLiteral("DifficultyList QML is available to bookmark sidebar wiring test"), out, &failed);
    const QString sidebarSource = QString::fromUtf8(difficultyList.readAll());
    expect(sidebarSource.contains(QStringLiteral("bookmarkGeneration"))
               && sidebarSource.contains(QStringLiteral("bookmarksForDifficulty"))
               && sidebarSource.contains(QStringLiteral("navigateToBookmark"))
               && sidebarSource.contains(QStringLiteral("DifficultySwatch"))
               && sidebarSource.contains(QStringLiteral("difficultyId: difficultyGroup.modelData.id"))
               && sidebarSource.contains(QStringLiteral("setBookmarkGroupExpanded")),
           QStringLiteral("bookmarks are grouped under their difficulty in the sidebar, foldable and colour-coded, instead of overlaying the editor"), out, &failed);
    // The difficulty row is the fold control. A click means fold only when the
    // row is already the one being edited, which is what `foldsBookmarks`
    // decides; the earlier design put a separate chevron button on the right
    // and read backwards, so its return would be a regression, not a variant.
    // The scope an edit is recorded under must be read, not bound. The session
    // updates its state and then emits chartTextChanged before
    // currentDifficultyChanged, so a binding on currentDifficultyId is still
    // holding the outgoing difficulty when the incoming text arrives — naming
    // the scope from one recorded every later edit into the difficulty being
    // left, and Ctrl+Z replayed another difficulty's edits. Only the real
    // QmlDocumentModel emits in that order, so this pins the call rather than
    // reproducing the race.
    QFile sourceEditorFile(QStringLiteral("src/app/qml_ui/editor/SourceEditor.qml"));
    expect(sourceEditorFile.open(QIODevice::ReadOnly),
           QStringLiteral("SourceEditor QML is available to the history scope test"), out, &failed);
    const QString sourceEditorSource = QString::fromUtf8(sourceEditorFile.readAll());
    expect(sourceEditorSource.contains(QStringLiteral("function currentHistoryScopeId()"))
               && sourceEditorSource.contains(
                      QStringLiteral("setHistoryScope(root.currentHistoryScopeId())"))
               && !sourceEditorSource.contains(QStringLiteral("property string historyScopeId")),
           QStringLiteral("the undo scope is read when it is needed, never bound"), out, &failed);

    expect(sidebarSource.contains(QStringLiteral("foldsBookmarks"))
               && sidebarSource.contains(QStringLiteral("readonly property bool foldsBookmarks: activeEditor"))
               && !sidebarSource.contains(QStringLiteral("id: foldButton")),
           QStringLiteral("the difficulty row folds its own bookmarks, with no separate fold button"), out, &failed);
    QFile followSyncSource(
        QStringLiteral("src/app/runtime/playback/FollowSync.cpp"));
    expect(followSyncSource.open(QIODevice::ReadOnly),
           QStringLiteral("preview follow synchronization source is available"), out, &failed);
    const QString followSource = QString::fromUtf8(followSyncSource.readAll());
    expect(followSource.contains(QStringLiteral("EditorFollowState"))
               && followSource.contains(QStringLiteral("publishFollow"))
               // The follow identity must be the workspace revision the editor
               // compares against, not the unrelated timeline counter. Stage
               // 4.9d-4b-2c moved the coordinator's read of that revision
               // behind PlaybackDocumentPort::appliedWorkspaceRevision() (the
               // field itself stays on Session); the invariant pinned here —
               // follow uses the applied workspace revision — is unchanged.
               && followSource.contains(QStringLiteral("appliedWorkspaceRevision()"))
               && !followSource.contains(QStringLiteral("documentValidationSnapshot()"))
               && source.contains(QStringLiteral("navigationAckTimer"))
               && source.contains(QStringLiteral("decorationCenterTimer")),
           QStringLiteral("editor synchronization uses value projection and item-owned queues"), out, &failed);
    return failed == 0 ? 0 : 1;
}
