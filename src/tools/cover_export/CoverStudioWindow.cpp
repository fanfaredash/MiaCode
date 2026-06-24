#include "tools/cover_export/CoverStudioWindow.h"

#include "tools/cover_export/CoverCompositionState.h"
#include "tools/cover_export/CoverFramePickerPanel.h"
#include "tools/cover_export/CoverInspectorPanel.h"
#include "tools/cover_export/CoverLayerListPanel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "tools/video_export/VideoExportController.h"
#include "DialogLocalization.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QStringList>
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
    // exportDialogStyleSheet only colours QCheckBox TEXT — no ::indicator rule, so
    // the box itself is near-invisible in dark mode. Append the dark-aware indicator
    // rules (explicit border + checkmark SVG + accent fill); they cascade to every
    // checkbox in the window subtree (inspector / list / reparented option groups).
    setStyleSheet(UiTheme::exportDialogStyleSheet() + UiTheme::darkAwareCheckBoxStyleSheet());

    studio_ = new CoverStudioPanel(task, initialSize, this);

    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    rootLayout->setSpacing(8);

    auto* toolbar = new QWidget(root);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);

    // 布局 ▾ — the whole-composition file operations collapsed into one structured
    // menu (reset / save / import / recent), replacing the old three loose buttons.
    auto* layoutButton = makeToolbarButton(l10n(QStringLiteral("Layout ▾"), QStringLiteral("布局 ▾")), toolbar);
    layoutButton->setToolTip(l10n(QStringLiteral("Reset / save / import / recent layouts"),
                                  QStringLiteral("重置 / 保存 / 导入 / 最近布局")));
    auto* layoutMenu = new QMenu(layoutButton);
    UiTheme::styleRoundedMenu(*layoutMenu);

    auto* resetAction = layoutMenu->addAction(
        l10n(QStringLiteral("Reset to default…"), QStringLiteral("重置为默认布局…")));
    resetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    layoutMenu->addSeparator();
    auto* saveAction = layoutMenu->addAction(
        l10n(QStringLiteral("Save layout to file…"), QStringLiteral("保存布局到文件…")));
    saveAction->setShortcut(QKeySequence::Save);
    auto* importAction = layoutMenu->addAction(
        l10n(QStringLiteral("Import layout file…"), QStringLiteral("导入布局文件…")));
    importAction->setShortcut(QKeySequence::Open);
    auto* recentMenu = layoutMenu->addMenu(l10n(QStringLiteral("Open recent"), QStringLiteral("打开最近")));
    UiTheme::styleRoundedMenu(*recentMenu);
    recentMenu->setToolTipsVisible(true);
    layoutButton->setMenu(layoutMenu);
    // Make the menu accelerators fire at window scope (they double as the in-menu
    // shortcut hints). Single-key shortcuts that fight the embedded native Quick
    // window are deferred to the P4 keymap; these Ctrl-combos are safe here.
    addAction(resetAction);
    addAction(saveAction);
    addAction(importAction);

    auto* exportButton = makeToolbarButton(l10n(QStringLiteral("Export"), QStringLiteral("导出")), toolbar, true);
    exportButton->setToolTip(l10n(QStringLiteral("Render and save the cover image"),
                                  QStringLiteral("渲染并保存封面图片")));
    auto* cancelButton = makeToolbarButton(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")), toolbar);
    cancelButton->setToolTip(l10n(QStringLiteral("Close without exporting (Esc)"),
                                  QStringLiteral("关闭而不导出（Esc）")));

    toolbarLayout->addWidget(layoutButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(exportButton);
    toolbarLayout->addWidget(cancelButton);
    rootLayout->addWidget(toolbar);

    connect(resetAction, &QAction::triggered, this, &CoverStudioWindow::confirmAndReset);
    connect(saveAction, &QAction::triggered, studio_, &CoverStudioPanel::saveLayout);
    connect(importAction, &QAction::triggered, studio_, &CoverStudioPanel::importLayout);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu] { rebuildRecentMenu(recentMenu); });

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
    // §3 inspector column, top → bottom: 画板（通用）/ 图层（选中层通用）/ 图层专属。
    // The canvas + card-options groups are owned by the panel and reparented here;
    // the card-options group is polymorphic (shown only when the card is selected),
    // and the inspector hosts the chart-frame-specific options in the same region.
    if (QWidget* canvasGroup = studio_->canvasOptionsGroup(rightPanel)) {
        rightLayout->addWidget(canvasGroup, 0);
    }
    auto* inspectorPanel = new CoverInspectorPanel(studio_, rightPanel);
    rightLayout->addWidget(inspectorPanel, 0);
    if (QWidget* cardOptions = studio_->cardOptionsGroup(rightPanel)) {
        rightLayout->addWidget(cardOptions, 0);
    }
    rightLayout->addStretch(1);
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
    connect(exportButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestExport);
    connect(cancelButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestCancel);

    fitToScreen(parent);
}

void CoverStudioWindow::confirmAndReset()
{
    const QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        l10n(QStringLiteral("Reset layout"), QStringLiteral("重置布局")),
        l10n(QStringLiteral("Reset discards all current layers and positions. Continue?"),
             QStringLiteral("重置将丢弃当前所有图层与位置，继续？")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply == QMessageBox::Yes && studio_ != nullptr) {
        studio_->resetLayout();
    }
}

void CoverStudioWindow::rebuildRecentMenu(QMenu* menu)
{
    if (menu == nullptr) {
        return;
    }
    menu->clear();
    const QStringList recent = miacode::cover_export::CoverCompositionState::loadRecentFiles();
    if (recent.isEmpty()) {
        QAction* empty = menu->addAction(l10n(QStringLiteral("(No recent files)"),
                                              QStringLiteral("（无最近文件）")));
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : recent) {
        const bool exists = QFileInfo::exists(path);
        QAction* action = menu->addAction(QDir::toNativeSeparators(path));
        action->setEnabled(exists);
        if (!exists) {
            action->setToolTip(l10n(QStringLiteral("File not found"), QStringLiteral("文件不存在")));
        }
        connect(action, &QAction::triggered, this, [this, path] {
            if (studio_ != nullptr) studio_->importLayoutFromPath(path);
        });
    }
    menu->addSeparator();
    QAction* clear = menu->addAction(l10n(QStringLiteral("Clear recent"), QStringLiteral("清除最近")));
    connect(clear, &QAction::triggered, this, [] {
        miacode::cover_export::CoverCompositionState::clearRecentFiles();
    });
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
