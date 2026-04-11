#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "UiText.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

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
    if (!applyBatchTransform(uiText("action.transform.mirror_lr", "Mirror Left/Right"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorLeftRight, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.mirror_lr", "Mirror Left/Right applied."));
}

void MainWindow::DocumentSection::onMirrorUpDown()
{
    if (!applyBatchTransform(uiText("action.transform.mirror_ud", "Mirror Up/Down"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorUpDown, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.mirror_ud", "Mirror Up/Down applied."));
}

void MainWindow::DocumentSection::onRotate180()
{
    if (!applyBatchTransform(uiText("action.transform.rotate_180", "Rotate 180"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate180, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.rotate_180", "Rotate 180 applied."));
}

void MainWindow::DocumentSection::onRotate45CounterClockwise()
{
    if (!applyBatchTransform(uiText("action.transform.rotate_ccw_45", "Rotate -45"), [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45CounterClockwise, changedCount);
    })) {
        return;
    }
    owner_.statusBar()->showMessage(uiText("status.transform.rotate_ccw_45", "Rotate -45 applied."));
}

void MainWindow::DocumentSection::onRotate45Clockwise()
{
    if (!applyBatchTransform(uiText("action.transform.rotate_cw_45", "Rotate +45"), [this](const QString& text, int* changedCount) {
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

    const auto normalized = miacode::chart_transform::normalizeChartText(
        owner_.activeChartText(),
        owner_.currentTimingMetadata()
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

    if (normalized.text == owner_.activeChartText()) {
        owner_.statusBar()->showMessage(
            uiText(
                "status.normalize.already_normalized",
                QStringLiteral("Format Chart: already normalized."))
        );
        return;
    }

    if (!applyBatchTransform(
            uiText("action.normalize_chart", QStringLiteral("Format Chart")),
            [normalized](const QString&, int* changedCount) {
                if (changedCount != nullptr) {
                    *changedCount = normalized.changedCount;
                }
                return normalized.text;
            })) {
        return;
    }

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
