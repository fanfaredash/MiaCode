#include "MainWindow.DocumentSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
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

QString MainWindow::resolveInitialOpenDirectory() const
{
    return documentSection_->resolveInitialOpenDirectory();
}

void MainWindow::setLastOpenDirectory(const QString& pathOrDir)
{
    documentSection_->setLastOpenDirectory(pathOrDir);
}

void MainWindow::onNormalizeWholeChart()
{
    // Normalize is an editor operation on the live selection, so the QML editor
    // owns it. The menu action and the chart.normalize shortcut both arrive
    // here and are forwarded to whoever is showing the editor.
    emit normalizeWholeChartRequested();
}
