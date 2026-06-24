#include "tools/cover_export/CoverStudioWindow.h"

#include "tools/cover_export/CoverFramePickerPanel.h"
#include "tools/cover_export/CoverInspectorPanel.h"
#include "tools/cover_export/CoverLayerListPanel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "tools/video_export/VideoExportController.h"
#include "DialogLocalization.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QDir>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

QPushButton* makeToolbarButton(const QString& text, QWidget* parent, bool primary = false)
{
    auto* button = new QPushButton(text, parent);
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary));
    return button;
}

}  // namespace

CoverStudioWindow::CoverStudioWindow(const VideoExportTask& task,
                                     const QSize& initialSize,
                                     const QString& outputDirectory,
                                     QWidget* parent)
    : QMainWindow(parent)
    , outputDirectory_(outputDirectory)
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    UiNativeWindowTheme::applyToWidget(this);
    setStyleSheet(UiTheme::exportDialogStyleSheet());

    studio_ = new CoverStudioPanel(task, initialSize, this);

    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    rootLayout->setSpacing(8);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);

    auto* resetButton = makeToolbarButton(l10n(QStringLiteral("Reset layout"), QStringLiteral("重置布局")), toolbar);
    auto* saveButton = makeToolbarButton(l10n(QStringLiteral("Save layout…"), QStringLiteral("保存布局…")), toolbar);
    auto* importButton = makeToolbarButton(l10n(QStringLiteral("Import layout…"), QStringLiteral("导入布局…")), toolbar);
    auto* exportButton = makeToolbarButton(l10n(QStringLiteral("Export"), QStringLiteral("导出")), toolbar, true);
    auto* cancelButton = makeToolbarButton(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")), toolbar);

    toolbarLayout->addWidget(resetButton);
    toolbarLayout->addWidget(saveButton);
    toolbarLayout->addWidget(importButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(exportButton);
    toolbarLayout->addWidget(cancelButton);
    rootLayout->addWidget(toolbar);

    auto* middle = new QWidget(root);
    auto* middleLayout = new QHBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(8);

    auto* layersPanel = new CoverLayerListPanel(studio_, middle);
    auto* rightScroll = new QScrollArea(middle);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    auto* rightPanel = new QWidget(rightScroll);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    auto* inspectorPanel = new CoverInspectorPanel(studio_, rightPanel);
    rightLayout->addWidget(inspectorPanel, 0);
    if (QWidget* settingsPanel = studio_->takeSettingsPanel(rightPanel)) {
        rightLayout->addWidget(settingsPanel, 1);
    }
    rightScroll->setWidget(rightPanel);
    layersPanel->setMinimumWidth(180);
    layersPanel->setMaximumWidth(230);
    rightScroll->setMinimumWidth(260);
    rightScroll->setMaximumWidth(320);

    middleLayout->addWidget(layersPanel, 0);
    middleLayout->addWidget(studio_, 1);
    middleLayout->addWidget(rightScroll, 0);
    rootLayout->addWidget(middle, 1);

    auto* framePanel = new CoverFramePickerPanel(studio_, root);
    framePanel->setFixedHeight(58);
    rootLayout->addWidget(framePanel);

    setCentralWidget(root);

    connect(studio_, &CoverStudioPanel::exportRequested, this, &CoverStudioWindow::exportNow);
    connect(studio_, &CoverStudioPanel::cancelRequested, this, &CoverStudioWindow::close);
    connect(resetButton, &QPushButton::clicked, studio_, &CoverStudioPanel::resetLayout);
    connect(saveButton, &QPushButton::clicked, studio_, &CoverStudioPanel::saveLayout);
    connect(importButton, &QPushButton::clicked, studio_, &CoverStudioPanel::importLayout);
    connect(exportButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestExport);
    connect(cancelButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestCancel);

    fitToScreen(parent);
}

void CoverStudioWindow::fitToScreen(QWidget* parent)
{
    QScreen* screen = nullptr;
    if (parent != nullptr && parent->windowHandle() != nullptr) {
        screen = parent->windowHandle()->screen();
    }
    if (screen == nullptr && parent != nullptr) {
        screen = QGuiApplication::screenAt(parent->frameGeometry().center());
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

    const QRect available = screen != nullptr
        ? screen->availableGeometry()
        : QRect(QPoint(0, 0), QSize(1280, 800));
    const QSize ideal(1180, 820);
    const QSize maxSize(qMax(640, static_cast<int>(available.width() * 0.9)),
                        qMax(520, static_cast<int>(available.height() * 0.9)));
    const QSize minimum(qMin(available.width(), qMin(960, qMax(640, available.width() - 80))),
                        qMin(available.height(), qMin(620, qMax(520, available.height() - 80))));
    const QSize target(qMin(ideal.width(), maxSize.width()),
                       qMin(ideal.height(), maxSize.height()));

    setMinimumSize(minimum);
    resize(target.expandedTo(minimum).boundedTo(available.size()));

    QRect frame(QPoint(0, 0), size());
    frame.moveCenter(available.center());
    if (frame.left() < available.left()) frame.moveLeft(available.left());
    if (frame.top() < available.top()) frame.moveTop(available.top());
    if (frame.right() > available.right()) frame.moveRight(available.right());
    if (frame.bottom() > available.bottom()) frame.moveBottom(available.bottom());
    move(frame.topLeft());
}

void CoverStudioWindow::exportNow()
{
    const CoverExportResult result = studio_->exportCover(outputDirectory_);
    const QString title = l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面"));
    if (result.success) {
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            this,
            title,
            (UiText::isChineseUi() ? QStringLiteral("封面已导出：\n%1") : QStringLiteral("Cover exported:\n%1"))
                .arg(QDir::toNativeSeparators(result.outputPath)));
        statusBar()->showMessage(QDir::toNativeSeparators(result.outputPath), 5000);
    } else {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            title,
            (UiText::isChineseUi() ? QStringLiteral("封面导出失败：\n%1") : QStringLiteral("Cover export failed:\n%1"))
                .arg(result.errorMessage));
    }
}

}  // namespace miacode::cover_export
