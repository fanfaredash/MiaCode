#include "MainWindow.DocumentSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/OperationLog.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"

#include <functional>

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

std::pair<int, int> lineColForPosition(const QTextDocument* document, int position)
{
    if (document == nullptr) {
        return {1, 1};
    }
    QTextCursor cursor(const_cast<QTextDocument*>(document));
    const int maxPosition = qMax(0, document->characterCount() - 1);
    cursor.setPosition(qBound(0, position, maxPosition));
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

}  // namespace

QString MainWindow::DocumentSection::resolveInitialOpenDirectory() const
{
    if (!state_.lastOpenDir_.isEmpty() && QDir(state_.lastOpenDir_).exists()) {
        return state_.lastOpenDir_;
    }
    if (!state_.currentFilePath_.isEmpty()) {
        const QString currentDir = QFileInfo(state_.currentFilePath_).absolutePath();
        if (QDir(currentDir).exists()) {
            return currentDir;
        }
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QDir(appDir).exists()) {
        return appDir;
    }
    return QDir::currentPath();
}

void MainWindow::DocumentSection::setLastOpenDirectory(const QString& pathOrDir)
{
    if (pathOrDir.isEmpty()) {
        return;
    }

    QString dirCandidate;
    const QFileInfo info(pathOrDir);
    if (info.isDir()) {
        dirCandidate = info.absoluteFilePath();
    } else {
        dirCandidate = info.absolutePath();
    }
    dirCandidate = QDir::cleanPath(dirCandidate);
    if (!QDir(dirCandidate).exists()) {
        return;
    }
    if (state_.lastOpenDir_ == dirCandidate) {
        return;
    }
    state_.lastOpenDir_ = dirCandidate;
    owner_.savePortableState();
}

QString MainWindow::DocumentSection::transformChartText(const QString& input, ChartTransformOp op, int* changedCount) const
{
    miacode::chart_transform::ChartTransformOp sharedOp = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
    switch (op) {
    case ChartTransformOp::MirrorLeftRight:
        sharedOp = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
        break;
    case ChartTransformOp::MirrorUpDown:
        sharedOp = miacode::chart_transform::ChartTransformOp::MirrorUpDown;
        break;
    case ChartTransformOp::Rotate180:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate180;
        break;
    case ChartTransformOp::Rotate45CounterClockwise:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate45CounterClockwise;
        break;
    case ChartTransformOp::Rotate45Clockwise:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate45Clockwise;
        break;
    }
    return miacode::chart_transform::transformChartText(input, sharedOp, changedCount);
}

void MainWindow::DocumentSection::onMirrorLeftRight()
{
    MC_OP("MainWindow::DocumentSection::onMirrorLeftRight");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(UiText::text(QStringLiteral("action.transform.mirror_lr")), [this](const QString& text, int* changedCount) {
        return miacode::chart_transform::transformChartSelectionText(text, miacode::chart_transform::ChartTransformOp::MirrorLeftRight, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.transform.mirror_lr")));
}

void MainWindow::DocumentSection::onMirrorUpDown()
{
    MC_OP("MainWindow::DocumentSection::onMirrorUpDown");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(UiText::text(QStringLiteral("action.transform.mirror_ud")), [this](const QString& text, int* changedCount) {
        return miacode::chart_transform::transformChartSelectionText(text, miacode::chart_transform::ChartTransformOp::MirrorUpDown, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.transform.mirror_ud")));
}

void MainWindow::DocumentSection::onRotate180()
{
    MC_OP("MainWindow::DocumentSection::onRotate180");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(UiText::text(QStringLiteral("action.transform.rotate_180")), [this](const QString& text, int* changedCount) {
        return miacode::chart_transform::transformChartSelectionText(text, miacode::chart_transform::ChartTransformOp::Rotate180, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.transform.rotate_180")));
}

void MainWindow::DocumentSection::onRotate45CounterClockwise()
{
    MC_OP("MainWindow::DocumentSection::onRotate45CounterClockwise");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(UiText::text(QStringLiteral("action.transform.rotate_ccw_45")), [this](const QString& text, int* changedCount) {
        return miacode::chart_transform::transformChartSelectionText(text, miacode::chart_transform::ChartTransformOp::Rotate45CounterClockwise, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.transform.rotate_ccw_45")));
}

void MainWindow::DocumentSection::onRotate45Clockwise()
{
    MC_OP("MainWindow::DocumentSection::onRotate45Clockwise");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(UiText::text(QStringLiteral("action.transform.rotate_cw_45")), [this](const QString& text, int* changedCount) {
        return miacode::chart_transform::transformChartSelectionText(text, miacode::chart_transform::ChartTransformOp::Rotate45Clockwise, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.transform.rotate_cw_45")));
}

void MainWindow::DocumentSection::onToggleBreakSelection()
{
    MC_OP("MainWindow::DocumentSection::onToggleBreakSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Break", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleBreakForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onToggleExSelection()
{
    MC_OP("MainWindow::DocumentSection::onToggleExSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle EX", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleExForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onToggleFireworkSelection()
{
    MC_OP("MainWindow::DocumentSection::onToggleFireworkSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Firework", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleFireworkForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onRandomRotateSelection()
{
    MC_OP("MainWindow::DocumentSection::onRandomRotateSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Random Rotate", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::randomRotateForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onClearCompleteElementsSelection()
{
    MC_OP("MainWindow::DocumentSection::onClearCompleteElementsSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        owner_.statusBar()->showMessage(QStringLiteral("一键清空: editor unavailable."));
        return;
    }

    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        owner_.statusBar()->showMessage(QStringLiteral("一键清空: no selection."));
        return;
    }

    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    const QString original = owner_.editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        owner_.statusBar()->showMessage(QStringLiteral("一键清空: invalid selection range."));
        return;
    }

    int changed = 0;
    const QString transformedFull =
        miacode::editor::clearCompleteElementsInSelection(original, begin, finish, &changed);
    if (transformedFull == original) {
        owner_.statusBar()->showMessage(QStringLiteral("一键清空: no note index changed."));
        return;
    }

    const int unchangedSuffixLength = original.size() - finish;
    const int transformedSelectionEnd = transformedFull.size() - unchangedSuffixLength;
    const QString transformedSelection =
        transformedFull.mid(begin, transformedSelectionEnd - begin);

    const bool forwardSelection = oldCursor.hasSelection()
        ? (oldCursor.position() >= oldCursor.anchor())
        : true;
    const int originalAnchor = forwardSelection ? begin : finish;
    const int originalPosition = forwardSelection ? finish : begin;

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.setPosition(begin);
    editCursor.setPosition(finish, QTextCursor::KeepAnchor);
    editCursor.insertText(transformedSelection);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int transformedEnd = begin + transformedSelection.size();
    const int restoredAnchor = qBound(0, forwardSelection ? begin : transformedEnd, maxPos);
    const int restoredPosition = qBound(0, forwardSelection ? transformedEnd : begin, maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    recordChartSelectionTransformUndoEntry(originalAnchor, originalPosition, restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QStringLiteral("一键清空 applied on selection: %1 replacement(s).").arg(changed));
}

void MainWindow::DocumentSection::onRaiseSubdivisionSelection()
{
    MC_OP("MainWindow::DocumentSection::onRaiseSubdivisionSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform(
        UiText::text(QStringLiteral("document.subdivision_plus_1")),
        [](const QString& text, const QString& suffixContext, int* changedCount) {
            return miacode::chart_transform::raiseSubdivisionForSelection(text, suffixContext, changedCount);
        });
}

void MainWindow::DocumentSection::onLowerSubdivisionSelection()
{
    MC_OP("MainWindow::DocumentSection::onLowerSubdivisionSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform(
        UiText::text(QStringLiteral("document.subdivision_minus_1")),
        [](const QString& text, const QString& suffixContext, int* changedCount) {
            return miacode::chart_transform::lowerSubdivisionForSelection(text, suffixContext, changedCount);
        });
}

void MainWindow::DocumentSection::onRaiseSubdivisionHalfStepSelection()
{
    MC_OP("MainWindow::DocumentSection::onRaiseSubdivisionHalfStepSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform(
        UiText::text(QStringLiteral("document.subdivision_plus_half")),
        [](const QString& text, const QString& suffixContext, int* changedCount) {
            return miacode::chart_transform::raiseSubdivisionHalfStepForSelection(text, suffixContext, changedCount);
        });
}

void MainWindow::DocumentSection::onLowerSubdivisionHalfStepSelection()
{
    MC_OP("MainWindow::DocumentSection::onLowerSubdivisionHalfStepSelection");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform(
        UiText::text(QStringLiteral("document.subdivision_minus_half")),
        [](const QString& text, const QString& suffixContext, int* changedCount) {
            return miacode::chart_transform::lowerSubdivisionHalfStepForSelection(text, suffixContext, changedCount);
        });
}

QString MainWindow::resolveInitialOpenDirectory() const
{
    return documentSection_->resolveInitialOpenDirectory();
}

void MainWindow::setLastOpenDirectory(const QString& pathOrDir)
{
    documentSection_->setLastOpenDirectory(pathOrDir);
}

QString MainWindow::transformChartText(const QString& input, ChartTransformOp op, int* changedCount) const
{
    return documentSection_->transformChartText(input, op, changedCount);
}

void MainWindow::onMirrorLeftRight()
{
    documentSection_->onMirrorLeftRight();
}

void MainWindow::onMirrorUpDown()
{
    documentSection_->onMirrorUpDown();
}

void MainWindow::onRotate180()
{
    documentSection_->onRotate180();
}

void MainWindow::onRotate45CounterClockwise()
{
    documentSection_->onRotate45CounterClockwise();
}

void MainWindow::onRotate45Clockwise()
{
    documentSection_->onRotate45Clockwise();
}

void MainWindow::onNormalizeWholeChart()
{
    // Normalize is an editor operation on the live selection, so the QML editor
    // owns it. The menu action and the chart.normalize shortcut both arrive
    // here and are forwarded to whoever is showing the editor.
    emit normalizeWholeChartRequested();
}

void MainWindow::onToggleBreakSelection()
{
    documentSection_->onToggleBreakSelection();
}

void MainWindow::onToggleExSelection()
{
    documentSection_->onToggleExSelection();
}

void MainWindow::onToggleFireworkSelection()
{
    documentSection_->onToggleFireworkSelection();
}

void MainWindow::onRandomRotateSelection()
{
    documentSection_->onRandomRotateSelection();
}

void MainWindow::onClearCompleteElementsSelection()
{
    documentSection_->onClearCompleteElementsSelection();
}

void MainWindow::onRaiseSubdivisionSelection()
{
    documentSection_->onRaiseSubdivisionSelection();
}

void MainWindow::onLowerSubdivisionSelection()
{
    documentSection_->onLowerSubdivisionSelection();
}

void MainWindow::onRaiseSubdivisionHalfStepSelection()
{
    documentSection_->onRaiseSubdivisionHalfStepSelection();
}

void MainWindow::onLowerSubdivisionHalfStepSelection()
{
    documentSection_->onLowerSubdivisionHalfStepSelection();
}

bool MainWindow::triggerShortcutCommand(const QString& id)
{
    // The id set is ShortcutRegistry's own, so a binding the user edits in
    // Preferences reaches v2 and v1 identically. Preview commands are not here:
    // QuickShellController already exposes them to QML directly.
    using Handler = void (MainWindow::*)();
    static const QHash<QString, Handler> kHandlers{
        {QStringLiteral("transform.mirror_lr"), &MainWindow::onMirrorLeftRight},
        {QStringLiteral("transform.mirror_ud"), &MainWindow::onMirrorUpDown},
        {QStringLiteral("transform.rotate_180"), &MainWindow::onRotate180},
        {QStringLiteral("transform.rotate_ccw_45"), &MainWindow::onRotate45CounterClockwise},
        {QStringLiteral("transform.rotate_cw_45"), &MainWindow::onRotate45Clockwise},
        {QStringLiteral("transform.subdivision_up"), &MainWindow::onRaiseSubdivisionSelection},
        {QStringLiteral("transform.subdivision_down"), &MainWindow::onLowerSubdivisionSelection},
        {QStringLiteral("transform.subdivision_half_up"), &MainWindow::onRaiseSubdivisionHalfStepSelection},
        {QStringLiteral("transform.subdivision_half_down"), &MainWindow::onLowerSubdivisionHalfStepSelection},
        {QStringLiteral("transform.toggle_break"), &MainWindow::onToggleBreakSelection},
        {QStringLiteral("transform.toggle_ex"), &MainWindow::onToggleExSelection},
        {QStringLiteral("transform.toggle_firework"), &MainWindow::onToggleFireworkSelection},
        {QStringLiteral("transform.random_rotate"), &MainWindow::onRandomRotateSelection},
        {QStringLiteral("transform.clear_complete_elements"), &MainWindow::onClearCompleteElementsSelection},
        {QStringLiteral("chart.normalize"), &MainWindow::onNormalizeWholeChart},
    };
    const auto handler = kHandlers.constFind(id);
    if (handler == kHandlers.cend()) {
        return false;
    }
    (this->*(*handler))();
    return true;
}
