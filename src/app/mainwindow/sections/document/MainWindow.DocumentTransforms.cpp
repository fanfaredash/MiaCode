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
    const miacode::chart_transform::ChartNormalizationOptions& initialOptions,
    const std::function<void(const miacode::chart_transform::ChartNormalizationOptions&)>& optionsChanged)
{
    NormalizeDialogResult result;
    result.options = initialOptions;
    result.options.startAtNewMeasure = true;

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner));
    dialog.setWindowTitle(UiText::text(QStringLiteral("dialog.normalize.title")));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
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

    const auto createDialogComboBox = [&dialog]() {
        return miacode::ui::createDialogComboBox(&dialog, 12);
    };
    const auto setComboToBool = [](QComboBox* combo, bool value) {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    const auto comboBoolValue = [](const QComboBox* combo, bool fallback) {
        if (combo == nullptr || combo->currentIndex() < 0) {
            return fallback;
        }
        const QVariant value = combo->itemData(combo->currentIndex());
        return value.isValid() ? value.toBool() : fallback;
    };

    auto* optionsGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.normalize.options")), &dialog);
    auto* optionsForm = new QFormLayout(optionsGroup);
    optionsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    optionsForm->setHorizontalSpacing(10);
    optionsForm->setVerticalSpacing(8);
    optionsForm->setContentsMargins(10, 8, 10, 8);
    rootLayout->addWidget(optionsGroup);

    auto* reduceTo384Combo = createDialogComboBox();
    reduceTo384Combo->addItem(UiText::text(QStringLiteral("preferences.on")), true);
    reduceTo384Combo->addItem(UiText::text(QStringLiteral("preferences.off")), false);
    setComboToBool(reduceTo384Combo, initialOptions.reduceTo384Grid);
    optionsForm->addRow(
        UiText::text(QStringLiteral("document.snap_approximately_to_384_grid")),
        reduceTo384Combo);

    auto* sectioningCombo = createDialogComboBox();
    sectioningCombo->addItem(
        UiText::text(QStringLiteral("document.chart_section_every_4_measures")),
        true);
    sectioningCombo->addItem(
        UiText::text(QStringLiteral("document.chart_section_none")),
        false);
    setComboToBool(sectioningCombo, initialOptions.splitEveryFourMeasures);
    optionsForm->addRow(UiText::text(QStringLiteral("document.chart_sectioning")), sectioningCombo);

    const auto publishOptionsChanged = [reduceTo384Combo, sectioningCombo, comboBoolValue, optionsChanged]() {
        if (!optionsChanged) {
            return;
        }
        optionsChanged(miacode::chart_transform::ChartNormalizationOptions{
            true,
            comboBoolValue(reduceTo384Combo, false),
            comboBoolValue(sectioningCombo, true)});
    };
    QObject::connect(
        reduceTo384Combo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        &dialog,
        [publishOptionsChanged](int) { publishOptionsChanged(); });
    QObject::connect(
        sectioningCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        &dialog,
        [publishOptionsChanged](int) { publishOptionsChanged(); });

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return result;
    }

    result.accepted = true;
    result.options.startAtNewMeasure = true;
    result.options.reduceTo384Grid = comboBoolValue(reduceTo384Combo, initialOptions.reduceTo384Grid);
    result.options.splitEveryFourMeasures =
        comboBoolValue(sectioningCombo, initialOptions.splitEveryFourMeasures);
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

void MainWindow::DocumentSection::onNormalizeWholeChart()
{
    MC_OP("MainWindow::DocumentSection::onNormalizeWholeChart");
    if (!owner_.hasActiveDifficulty()) {
        _mc_op_.fail(QStringLiteral("no active difficulty"));
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
        dialogDescription = UiText::text(QStringLiteral("document.selection_full_chart"));
    } else {
        const auto [startLine, startCol] = lineColForPosition(editor->document(), begin);
        const auto [endLine, endCol] = lineColForPosition(editor->document(), qMax(begin, finish - 1));
        dialogDescription = UiText::text(QStringLiteral("document.selection_l_1c_2_l")).arg(startLine)
                  .arg(startCol)
                  .arg(endLine)
                  .arg(endCol);
    }

    miacode::chart_transform::ChartNormalizationOptions options;
    options.startAtNewMeasure = true;
    options.reduceTo384Grid = state_.chartNormalizeReduceTo384Grid_;
    options.splitEveryFourMeasures = state_.chartNormalizeSplitEveryFourMeasures_;
    const NormalizeDialogResult dialogResult =
        showNormalizeSelectionDialog(
            owner_,
            dialogDescription,
            options,
            [this](const miacode::chart_transform::ChartNormalizationOptions& changedOptions) {
                if (state_.chartNormalizeReduceTo384Grid_ == changedOptions.reduceTo384Grid
                    && state_.chartNormalizeSplitEveryFourMeasures_
                        == changedOptions.splitEveryFourMeasures) {
                    return;
                }
                state_.chartNormalizeStartAtNewMeasure_ = true;
                state_.chartNormalizeReduceTo384Grid_ = changedOptions.reduceTo384Grid;
                state_.chartNormalizeSplitEveryFourMeasures_ =
                    changedOptions.splitEveryFourMeasures;
                owner_.savePortableState();
            });
    if (!dialogResult.accepted) {
        return;
    }

    state_.chartNormalizeStartAtNewMeasure_ = true;
    state_.chartNormalizeReduceTo384Grid_ = dialogResult.options.reduceTo384Grid;
    state_.chartNormalizeSplitEveryFourMeasures_ = dialogResult.options.splitEveryFourMeasures;

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
            UiText::text(QStringLiteral("dialog.normalize.title")),
            normalized.errorMessage.isEmpty()
                ? UiText::text(QStringLiteral("dialog.normalize.failed"))
                : normalized.errorMessage
        );
        return;
    }

    const QString replacement = composeNormalizedSelectionReplacement(original, begin, finish, normalized.text);
    if (replacement == original.mid(begin, finish - begin)) {
        owner_.statusBar()->showMessage(
            UiText::text(QStringLiteral("status.normalize.already_normalized"))
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
    if (owner_.editorSection_ != nullptr) {
        owner_.editorSection_->syncBookmarksFromEditorText();
    }

    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("status.normalize.applied"))
            .arg(normalized.measureLineCount)
    );
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
