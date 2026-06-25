#include "tools/cover_export/CoverStudioWindow.h"

#include "tools/cover_export/CoverCompositionState.h"
#include "tools/cover_export/CoverInspectorPanel.h"
#include "tools/cover_export/CoverLayerListPanel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "tools/video_export/VideoExportController.h"
#include "DialogLocalization.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSize>
#include <QTextEdit>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonObject>

#include <initializer_list>

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

bool focusShouldKeepTransportKeys(QWidget* widget)
{
    return qobject_cast<QLineEdit*>(widget) != nullptr
        || qobject_cast<QTextEdit*>(widget) != nullptr
        || qobject_cast<QPlainTextEdit*>(widget) != nullptr
        || qobject_cast<QComboBox*>(widget) != nullptr
        || qobject_cast<QAbstractSpinBox*>(widget) != nullptr;
}

QString compactToolbarButtonStyle(bool primary = false)
{
    return UiTheme::dialogPushButtonStyleSheet(primary)
        + QStringLiteral("QPushButton { min-width: 0px; min-height: 30px; padding: 0 12px; }");
}

QString zoomButtonStyle()
{
    return UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 0px; min-height: 22px; max-height: 24px;"
                         " padding: 0 7px; border-radius: 6px; font-weight: 500; }");
}

QString formatPreviewZoom(qreal zoom)
{
    return QStringLiteral("%1%").arg(qRound(zoom * 100.0));
}

QJsonObject makeLayer(const QString& key,
                      const QString& kind,
                      qreal nx,
                      qreal ny,
                      qreal sizeFraction,
                      int z,
                      bool visible = true)
{
    QJsonObject layer;
    layer.insert(QStringLiteral("key"), key);
    layer.insert(QStringLiteral("kind"), kind);
    layer.insert(QStringLiteral("nx"), nx);
    layer.insert(QStringLiteral("ny"), ny);
    layer.insert(QStringLiteral("sizeFraction"), sizeFraction);
    layer.insert(QStringLiteral("z"), z);
    layer.insert(QStringLiteral("visible"), visible);
    layer.insert(QStringLiteral("locked"), false);
    layer.insert(QStringLiteral("opacity"), 1.0);
    if (kind == QStringLiteral("chartFrame")) {
        layer.insert(QStringLiteral("label"), QStringLiteral("Chart frame"));
        layer.insert(QStringLiteral("frameSeconds"), 0.0);
        layer.insert(QStringLiteral("frameBgEnabled"), true);
        layer.insert(QStringLiteral("frameBgBrightness"), 0.8);
        layer.insert(QStringLiteral("frameStyle"), QString());
    } else {
        layer.insert(QStringLiteral("label"), QStringLiteral("Difficulty card"));
    }
    return layer;
}

QJsonObject makePresetComposition(std::initializer_list<QJsonObject> layers)
{
    QJsonArray arr;
    for (const QJsonObject& layer : layers) {
        arr.append(layer);
    }
    QJsonObject layout;
    layout.insert(QStringLiteral("layers"), arr);

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("miacode-cover-composition"));
    root.insert(QStringLiteral("version"), miacode::cover_export::CoverCompositionState::kCurrentVersion);
    root.insert(QStringLiteral("layout"), layout);
    return root;
}

QList<miacode::cover_export::CoverUserPreset> builtInPresets()
{
    using miacode::cover_export::CoverUserPreset;
    return {
        CoverUserPreset{
            l10n(QStringLiteral("Centered card (default)"), QStringLiteral("卡片居中（默认）")),
            makePresetComposition({
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, true),
            }),
        },
        CoverUserPreset{
            l10n(QStringLiteral("Card + chart frame"), QStringLiteral("卡片 + 谱面帧")),
            makePresetComposition({
                makeLayer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.32, 0.5, 0.82, 0, true),
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.64, 0.5, 0.78, 1, true),
            }),
        },
        CoverUserPreset{
            l10n(QStringLiteral("Dual chart-frame collage"), QStringLiteral("双谱面帧拼贴")),
            makePresetComposition({
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, false),
                makeLayer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.30, 0.40, 0.56, 1, true),
                makeLayer(QStringLiteral("chartFrame2"), QStringLiteral("chartFrame"), 0.66, 0.60, 0.56, 2, true),
            }),
        },
        CoverUserPreset{
            l10n(QStringLiteral("Pure chart frame"), QStringLiteral("纯谱面帧（无卡片）")),
            makePresetComposition({
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, false),
                makeLayer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.5, 0.5, 0.92, 1, true),
            }),
        },
    };
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
    layoutButton->setStyleSheet(compactToolbarButtonStyle());
    layoutButton->setToolTip(l10n(QStringLiteral("Reset / save / import / recent layouts"),
                                  QStringLiteral("重置 / 保存 / 导入 / 最近布局")));
    auto* layoutMenu = new QMenu(layoutButton);
    UiTheme::styleRoundedMenu(*layoutMenu);

    auto* resetAction = layoutMenu->addAction(
        l10n(QStringLiteral("Reset to default…"), QStringLiteral("重置为默认布局…")));
    resetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    layoutMenu->addSeparator();
    auto* presetMenu = layoutMenu->addMenu(l10n(QStringLiteral("Apply preset"), QStringLiteral("应用预设")));
    UiTheme::styleRoundedMenu(*presetMenu);
    presetMenu->setToolTipsVisible(true);
    auto* savePresetAction = layoutMenu->addAction(
        l10n(QStringLiteral("Save current as preset..."), QStringLiteral("保存当前为预设...")));
    auto* managePresetsAction = layoutMenu->addAction(
        l10n(QStringLiteral("Manage presets..."), QStringLiteral("管理预设...")));
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

    auto* zoomOutButton = makeToolbarButton(QStringLiteral("−"), toolbar);
    zoomOutButton->setStyleSheet(zoomButtonStyle());
    zoomOutButton->setFixedSize(28, 24);
    zoomOutButton->setToolTip(l10n(QStringLiteral("Zoom canvas out (Ctrl+-)"),
                                   QStringLiteral("缩小画布视图（Ctrl+-）")));
    zoomOutButton->setAccessibleName(l10n(QStringLiteral("Zoom canvas out"),
                                          QStringLiteral("缩小画布视图")));
    auto* zoomResetButton = makeToolbarButton(formatPreviewZoom(studio_->previewZoom()), toolbar);
    zoomResetButton->setStyleSheet(zoomButtonStyle());
    zoomResetButton->setFixedSize(62, 24);
    zoomResetButton->setToolTip(l10n(QStringLiteral("Reset canvas zoom (Ctrl+0)"),
                                     QStringLiteral("还原画布缩放（Ctrl+0）")));
    zoomResetButton->setAccessibleName(l10n(QStringLiteral("Reset canvas zoom"),
                                            QStringLiteral("还原画布缩放")));
    auto* zoomInButton = makeToolbarButton(QStringLiteral("+"), toolbar);
    zoomInButton->setStyleSheet(zoomButtonStyle());
    zoomInButton->setFixedSize(28, 24);
    auto* zoomOutAction = new QAction(this);
    zoomOutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    addAction(zoomOutAction);
    auto* zoomInAction = new QAction(this);
    zoomInAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl++")),
                                QKeySequence(QStringLiteral("Ctrl+="))});
    addAction(zoomInAction);
    auto* zoomResetAction = new QAction(this);
    zoomResetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    addAction(zoomResetAction);
    zoomInButton->setToolTip(l10n(QStringLiteral("Zoom canvas in (Ctrl++)"),
                                  QStringLiteral("放大画布视图（Ctrl++）")));
    zoomInButton->setAccessibleName(l10n(QStringLiteral("Zoom canvas in"),
                                         QStringLiteral("放大画布视图")));

    auto* exportButton = makeToolbarButton(l10n(QStringLiteral("Export"), QStringLiteral("导出")), toolbar, true);
    exportButton->setStyleSheet(compactToolbarButtonStyle(true));
    exportButton->setToolTip(l10n(QStringLiteral("Render and save the cover image"),
                                  QStringLiteral("渲染并保存封面图片")));
    auto* cancelButton = makeToolbarButton(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")), toolbar);
    cancelButton->setStyleSheet(compactToolbarButtonStyle());
    cancelButton->setToolTip(l10n(QStringLiteral("Close without exporting (Esc)"),
                                  QStringLiteral("关闭而不导出（Esc）")));

    toolbarLayout->addWidget(layoutButton);
    toolbarLayout->addWidget(zoomOutButton);
    toolbarLayout->addWidget(zoomResetButton);
    toolbarLayout->addWidget(zoomInButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(exportButton);
    toolbarLayout->addWidget(cancelButton);
    rootLayout->addWidget(toolbar);

    connect(resetAction, &QAction::triggered, this, &CoverStudioWindow::confirmAndReset);
    connect(saveAction, &QAction::triggered, studio_, &CoverStudioPanel::saveLayout);
    connect(importAction, &QAction::triggered, studio_, &CoverStudioPanel::importLayout);
    connect(presetMenu, &QMenu::aboutToShow, this, [this, presetMenu] { rebuildPresetMenu(presetMenu); });
    connect(savePresetAction, &QAction::triggered, this, &CoverStudioWindow::saveCurrentAsPreset);
    connect(managePresetsAction, &QAction::triggered, this, &CoverStudioWindow::managePresets);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu] { rebuildRecentMenu(recentMenu); });
    connect(zoomOutButton, &QPushButton::clicked, studio_, &CoverStudioPanel::zoomPreviewOut);
    connect(zoomResetButton, &QPushButton::clicked, studio_, &CoverStudioPanel::resetPreviewZoom);
    connect(zoomInButton, &QPushButton::clicked, studio_, &CoverStudioPanel::zoomPreviewIn);
    connect(zoomOutAction, &QAction::triggered, studio_, &CoverStudioPanel::zoomPreviewOut);
    connect(zoomResetAction, &QAction::triggered, studio_, &CoverStudioPanel::resetPreviewZoom);
    connect(zoomInAction, &QAction::triggered, studio_, &CoverStudioPanel::zoomPreviewIn);
    const QPointer<QPushButton> zoomResetGuard(zoomResetButton);
    connect(studio_, &CoverStudioPanel::previewZoomChanged, this, [zoomResetGuard](qreal zoom) {
        if (zoomResetGuard != nullptr) {
            zoomResetGuard->setText(formatPreviewZoom(zoom));
        }
    });

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

    setCentralWidget(root);

    connect(studio_, &CoverStudioPanel::exportRequested, this, &CoverStudioWindow::exportNow);
    connect(studio_, &CoverStudioPanel::cancelRequested, this, &CoverStudioWindow::close);
    connect(exportButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestExport);
    connect(cancelButton, &QPushButton::clicked, studio_, &CoverStudioPanel::requestCancel);

    qApp->installEventFilter(this);
    fitToScreen(parent);
}

CoverStudioWindow::~CoverStudioWindow()
{
    qApp->removeEventFilter(this);
}

bool CoverStudioWindow::eventFilter(QObject* watched, QEvent* event)
{
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        && studio_ != nullptr && isActiveWindow()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        QWidget* focused = QApplication::focusWidget();
        if (focused != nullptr && this->isAncestorOf(focused) && !focusShouldKeepTransportKeys(focused)) {
            if (studio_->handleFrameTransportShortcut(keyEvent)) {
                return true;
            }
        }
    } else if (studio_ != nullptr
               && (event->type() == QEvent::ApplicationDeactivate
                   || event->type() == QEvent::WindowDeactivate)) {
        studio_->cancelFrameTransportHold();
    }
    return QMainWindow::eventFilter(watched, event);
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

void CoverStudioWindow::saveCurrentAsPreset()
{
    if (studio_ == nullptr) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        l10n(QStringLiteral("Save preset"), QStringLiteral("保存预设")),
        l10n(QStringLiteral("Preset name:"), QStringLiteral("预设名称：")),
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    miacode::cover_export::CoverCompositionState::saveUserPreset(
        name, studio_->exportPresetCompositionJson());
}

void CoverStudioWindow::managePresets()
{
    QDialog dialog(this);
    dialog.setWindowTitle(l10n(QStringLiteral("Manage presets"), QStringLiteral("管理预设")));
    dialog.setModal(true);
    UiNativeWindowTheme::applyToWidget(&dialog);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* list = new QListWidget(&dialog);
    list->setMinimumSize(360, 220);
    layout->addWidget(list);

    auto refill = [list] {
        list->clear();
        for (const miacode::cover_export::CoverUserPreset& preset
             : miacode::cover_export::CoverCompositionState::loadUserPresets()) {
            auto* item = new QListWidgetItem(preset.name, list);
            item->setData(Qt::UserRole, preset.name);
        }
    };
    refill();

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* renameButton = buttons->addButton(
        l10n(QStringLiteral("Rename"), QStringLiteral("重命名")), QDialogButtonBox::ActionRole);
    QPushButton* deleteButton = buttons->addButton(
        l10n(QStringLiteral("Delete"), QStringLiteral("删除")), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    UiDialogs::localizeButtonBox(buttons);

    auto updateButtons = [list, renameButton, deleteButton] {
        const bool hasSelection = list->currentItem() != nullptr;
        renameButton->setEnabled(hasSelection);
        deleteButton->setEnabled(hasSelection);
    };
    updateButtons();
    connect(list, &QListWidget::currentItemChanged, &dialog, [updateButtons](QListWidgetItem*, QListWidgetItem*) {
        updateButtons();
    });
    connect(renameButton, &QPushButton::clicked, &dialog, [&] {
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr) {
            return;
        }
        const QString oldName = item->data(Qt::UserRole).toString();
        bool ok = false;
        const QString newName = QInputDialog::getText(
            &dialog,
            l10n(QStringLiteral("Rename preset"), QStringLiteral("重命名预设")),
            l10n(QStringLiteral("Preset name:"), QStringLiteral("预设名称：")),
            QLineEdit::Normal,
            oldName,
            &ok).trimmed();
        if (ok && !newName.isEmpty()) {
            miacode::cover_export::CoverCompositionState::renameUserPreset(oldName, newName);
            refill();
        }
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [&] {
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr) {
            return;
        }
        const QString name = item->data(Qt::UserRole).toString();
        const QMessageBox::StandardButton reply = QMessageBox::question(
            &dialog,
            l10n(QStringLiteral("Delete preset"), QStringLiteral("删除预设")),
            l10n(QStringLiteral("Delete this preset?"), QStringLiteral("删除这个预设？")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            miacode::cover_export::CoverCompositionState::removeUserPreset(name);
            refill();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    dialog.exec();
}

void CoverStudioWindow::rebuildPresetMenu(QMenu* menu)
{
    if (menu == nullptr) {
        return;
    }
    menu->clear();
    auto addPresetAction = [this, menu](const miacode::cover_export::CoverUserPreset& preset) {
        QAction* action = menu->addAction(preset.name);
        const QJsonArray layers = preset.composition
            .value(QStringLiteral("layout")).toObject()
            .value(QStringLiteral("layers")).toArray();
        bool needsChartFrame = false;
        for (const QJsonValue& value : layers) {
            const QJsonObject layer = value.toObject();
            if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("chartFrame")
                && layer.value(QStringLiteral("visible")).toBool(true)) {
                needsChartFrame = true;
                break;
            }
        }
        if (needsChartFrame && studio_ != nullptr && !studio_->chartFrameAvailable()) {
            action->setEnabled(false);
            action->setToolTip(l10n(QStringLiteral("This preset needs a renderable chart frame"),
                                    QStringLiteral("该预设需要可渲染的谱面帧")));
        }
        connect(action, &QAction::triggered, this, [this, preset] {
            if (studio_ != nullptr) {
                studio_->applyPresetCompositionJson(preset.composition);
            }
        });
    };

    for (const miacode::cover_export::CoverUserPreset& preset : builtInPresets()) {
        addPresetAction(preset);
    }
    const QList<miacode::cover_export::CoverUserPreset> userPresets =
        miacode::cover_export::CoverCompositionState::loadUserPresets();
    if (!userPresets.isEmpty()) {
        menu->addSeparator();
        for (const miacode::cover_export::CoverUserPreset& preset : userPresets) {
            addPresetAction(preset);
        }
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
