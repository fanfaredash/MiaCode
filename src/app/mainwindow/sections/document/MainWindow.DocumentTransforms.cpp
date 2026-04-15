#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "UiText.h"
#include "UiTheme.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

struct NormalizeDialogResult {
    bool accepted = false;
    miacode::chart_transform::ChartNormalizationOptions options;
};

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

NormalizeDialogResult showNormalizeSelectionDialog(
    MainWindow& owner,
    const QString& descriptionText,
    const miacode::chart_transform::ChartNormalizationOptions& initialOptions)
{
    NormalizeDialogResult result;
    result.options = initialOptions;

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner));
    dialog.setWindowTitle(uiText("dialog.normalize.title", QStringLiteral("Format Chart")));
    dialog.setModal(true);
    dialog.setMinimumWidth(300);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    UiDialogs::prepareDialogWindow(&dialog, &owner);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    auto* summaryRow = new QWidget(&dialog);
    auto* summaryLayout = new QHBoxLayout(summaryRow);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(10);

    auto* iconLabel = new QLabel(summaryRow);
    iconLabel->setPixmap(dialog.style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(28, 28));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    summaryLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* hintLabel = new QLabel(descriptionText, summaryRow);
    hintLabel->setWordWrap(false);
    summaryLayout->addWidget(hintLabel, 1);
    rootLayout->addWidget(summaryRow);

    auto* startAtNewMeasureCheck = new QCheckBox(
        UiText::isChineseUi()
            ? QStringLiteral("选区起点视作小节线开始")
            : QStringLiteral("Treat selection start as measure boundary"),
        &dialog);
    startAtNewMeasureCheck->setChecked(initialOptions.startAtNewMeasure);
    rootLayout->addWidget(startAtNewMeasureCheck);

    auto* reduceTo384Check = new QCheckBox(
        UiText::isChineseUi()
            ? QStringLiteral("统一近似至384分音")
            : QStringLiteral("Snap approximately to 384 grid"),
        &dialog);
    reduceTo384Check->setChecked(initialOptions.reduceTo384Grid);
    rootLayout->addWidget(reduceTo384Check);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return result;
    }

    result.accepted = true;
    result.options.startAtNewMeasure = startAtNewMeasureCheck->isChecked();
    result.options.reduceTo384Grid = reduceTo384Check->isChecked();
    return result;
}

bool selectionStartsAtLineStart(const QString& text, int selectionStart)
{
    return selectionStart <= 0 || text.at(selectionStart - 1) == QLatin1Char('\n');
}

bool selectionEndsAtLineBoundary(const QString& text, int selectionEnd)
{
    return selectionEnd >= text.size()
        || (selectionEnd < text.size() && text.at(selectionEnd) == QLatin1Char('\n'))
        || (selectionEnd > 0 && text.at(selectionEnd - 1) == QLatin1Char('\n'));
}

QString composeNormalizedSelectionReplacement(
    const QString& original,
    int selectionStart,
    int selectionEnd,
    const QString& normalizedText)
{
    QString replacement = normalizedText;
    if (!selectionStartsAtLineStart(original, selectionStart)) {
        replacement.prepend(QStringLiteral("\n\n"));
    }
    if (!selectionEndsAtLineBoundary(original, selectionEnd)) {
        replacement.append(QLatin1Char('\n'));
    }
    return replacement;
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
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(uiText("action.transform.mirror_lr", "Mirror Left/Right"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorLeftRight, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.mirror_lr", "Mirror Left/Right applied."));
}

void MainWindow::DocumentSection::onMirrorUpDown()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(uiText("action.transform.mirror_ud", "Mirror Up/Down"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorUpDown, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.mirror_ud", "Mirror Up/Down applied."));
}

void MainWindow::DocumentSection::onRotate180()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(uiText("action.transform.rotate_180", "Rotate 180"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate180, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.rotate_180", "Rotate 180 applied."));
}

void MainWindow::DocumentSection::onRotate45CounterClockwise()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(uiText("action.transform.rotate_ccw_45", "Rotate -45"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45CounterClockwise, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.rotate_ccw_45", "Rotate -45 applied."));
}

void MainWindow::DocumentSection::onRotate45Clockwise()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!applySelectionBatchTransform(uiText("action.transform.rotate_cw_45", "Rotate +45"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45Clockwise, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.rotate_cw_45", "Rotate +45 applied."));
}

void MainWindow::DocumentSection::onNormalizeWholeChart()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    const bool hadSelection = oldCursor.hasSelection();
    const QString original = owner_.editorText();
    const int startPos = hadSelection ? oldCursor.selectionStart() : 0;
    const int endPos = hadSelection ? oldCursor.selectionEnd() : original.size();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    const bool wholeTextSelected = begin == 0 && finish == original.size();

    QString dialogDescription;
    if (wholeTextSelected) {
        dialogDescription = UiText::isChineseUi()
            ? QStringLiteral("选中范围：全文")
            : QStringLiteral("Selection: full chart");
    } else {
        const auto [startLine, startCol] = lineColForPosition(editor->document(), begin);
        const auto [endLine, endCol] = lineColForPosition(editor->document(), qMax(begin, finish - 1));
        dialogDescription = UiText::isChineseUi()
            ? QStringLiteral("选中范围：%1行%2列 ~ %3行%4列")
                  .arg(startLine)
                  .arg(startCol)
                  .arg(endLine)
                  .arg(endCol)
            : QStringLiteral("Selection: L%1C%2 ~ L%3C%4")
                  .arg(startLine)
                  .arg(startCol)
                  .arg(endLine)
                  .arg(endCol);
    }

    miacode::chart_transform::ChartNormalizationOptions options;
    options.startAtNewMeasure = state_.chartNormalizeStartAtNewMeasure_;
    options.reduceTo384Grid = state_.chartNormalizeReduceTo384Grid_;
    const NormalizeDialogResult dialogResult = showNormalizeSelectionDialog(owner_, dialogDescription, options);
    if (!dialogResult.accepted) {
        return;
    }

    state_.chartNormalizeStartAtNewMeasure_ = dialogResult.options.startAtNewMeasure;
    state_.chartNormalizeReduceTo384Grid_ = dialogResult.options.reduceTo384Grid;
    owner_.savePortableState();

    if (begin < 0 || finish < begin || finish > original.size()) {
        owner_.statusBar()->showMessage(QStringLiteral("Format Chart: invalid selection range."));
        return;
    }

    const auto normalized = miacode::chart_transform::normalizeChartSelectionText(
        original,
        begin,
        finish,
        owner_.currentTimingMetadata(),
        dialogResult.options
    );
    if (!normalized.ok) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            uiText("dialog.normalize.title", QStringLiteral("Format Chart")),
            normalized.errorMessage.isEmpty()
                ? uiText(
                      "dialog.normalize.failed",
                      QStringLiteral("Failed to normalize the current chart."))
                : normalized.errorMessage
        );
        return;
    }

    const QString replacement = composeNormalizedSelectionReplacement(original, begin, finish, normalized.text);
    if (replacement == original.mid(begin, finish - begin)) {
        owner_.statusBar()->showMessage(
            uiText(
                "status.normalize.already_normalized",
                QStringLiteral("Format Chart: already normalized."))
        );
        return;
    }

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.setPosition(begin);
    editCursor.setPosition(finish, QTextCursor::KeepAnchor);
    editCursor.insertText(replacement);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    if (!hadSelection) {
        restoredCursor.setPosition(qBound(0, oldCursor.position(), maxPos));
    } else {
        const int transformedEnd = begin + replacement.size();
        const bool forwardSelection = oldCursor.position() >= oldCursor.anchor();
        const int restoredAnchor = qBound(0, forwardSelection ? begin : transformedEnd, maxPos);
        const int restoredPosition = qBound(0, forwardSelection ? transformedEnd : begin, maxPos);
        restoredCursor.setPosition(restoredAnchor);
        restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    }
    editor->setTextCursor(restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();

    owner_.statusBar()->showMessage(
        uiText(
            "status.normalize.applied",
            QStringLiteral("Format Chart applied: %1 measure line(s)."))
            .arg(normalized.measureLineCount)
    );
}

void MainWindow::DocumentSection::onToggleBreakSelection()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Break", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleBreakForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onToggleExSelection()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle EX", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleExForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onToggleFireworkSelection()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Firework", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleFireworkForSelection(text, changedCount);
    });
}

void MainWindow::DocumentSection::onRandomRotateSelection()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Random Rotate", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::randomRotateForSelection(text, changedCount);
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
    documentSection_->onNormalizeWholeChart();
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
