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
#include <QDesktopServices>
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
#include <QUrl>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonObject>

#include <initializer_list>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::localized(en, zh);
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
            UiText::text(QStringLiteral("cover.centered_card_default")),
            makePresetComposition({
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, true),
            }),
        },
        CoverUserPreset{
            UiText::text(QStringLiteral("cover.card_chart_frame")),
            makePresetComposition({
                makeLayer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.32, 0.5, 0.82, 0, true),
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.64, 0.5, 0.78, 1, true),
            }),
        },
        CoverUserPreset{
            UiText::text(QStringLiteral("cover.dual_chart_frame_collage")),
            makePresetComposition({
                makeLayer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, false),
                makeLayer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.30, 0.40, 0.56, 1, true),
                makeLayer(QStringLiteral("chartFrame2"), QStringLiteral("chartFrame"), 0.66, 0.60, 0.56, 2, true),
            }),
        },
        CoverUserPreset{
            UiText::text(QStringLiteral("cover.pure_chart_frame")),
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
    setWindowTitle(UiText::text(QStringLiteral("cover.export_cover")));
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
    auto* layoutButton = makeToolbarButton(UiText::text(QStringLiteral("cover.layout")), toolbar);
    layoutButton->setStyleSheet(compactToolbarButtonStyle());
    layoutButton->setToolTip(UiText::text(QStringLiteral("cover.reset_save_import_recent_layouts")));
    auto* layoutMenu = new QMenu(layoutButton);
    UiTheme::styleRoundedMenu(*layoutMenu);

    auto* resetAction = layoutMenu->addAction(
        UiText::text(QStringLiteral("cover.reset_to_default")));
    resetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    layoutMenu->addSeparator();
    auto* presetMenu = layoutMenu->addMenu(UiText::text(QStringLiteral("cover.apply_preset")));
    UiTheme::styleRoundedMenu(*presetMenu);
    presetMenu->setToolTipsVisible(true);
    auto* savePresetAction = layoutMenu->addAction(
        UiText::text(QStringLiteral("cover.save_current_as_preset")));
    auto* managePresetsAction = layoutMenu->addAction(
        UiText::text(QStringLiteral("cover.manage_presets")));
    layoutMenu->addSeparator();
    auto* saveAction = layoutMenu->addAction(
        UiText::text(QStringLiteral("cover.save_layout_to_file")));
    saveAction->setShortcut(QKeySequence::Save);
    auto* importAction = layoutMenu->addAction(
        UiText::text(QStringLiteral("cover.import_layout_file")));
    importAction->setShortcut(QKeySequence::Open);
    auto* recentMenu = layoutMenu->addMenu(UiText::text(QStringLiteral("cover.open_recent")));
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
    zoomOutButton->setToolTip(UiText::text(QStringLiteral("cover.zoom_canvas_out_ctrl")));
    zoomOutButton->setAccessibleName(UiText::text(QStringLiteral("cover.zoom_canvas_out")));
    auto* zoomResetButton = makeToolbarButton(formatPreviewZoom(studio_->previewZoom()), toolbar);
    zoomResetButton->setStyleSheet(zoomButtonStyle());
    zoomResetButton->setFixedSize(62, 24);
    zoomResetButton->setToolTip(UiText::text(QStringLiteral("cover.reset_canvas_zoom_ctrl_0")));
    zoomResetButton->setAccessibleName(UiText::text(QStringLiteral("cover.reset_canvas_zoom")));
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
    zoomInButton->setToolTip(UiText::text(QStringLiteral("cover.zoom_canvas_in_ctrl")));
    zoomInButton->setAccessibleName(UiText::text(QStringLiteral("cover.zoom_canvas_in")));

    auto* exportButton = makeToolbarButton(UiText::text(QStringLiteral("cover.export")), toolbar, true);
    exportButton->setStyleSheet(compactToolbarButtonStyle(true));
    exportButton->setToolTip(UiText::text(QStringLiteral("cover.render_and_save_the_cover")));
    auto* cancelButton = makeToolbarButton(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")), toolbar);
    cancelButton->setStyleSheet(compactToolbarButtonStyle());
    cancelButton->setToolTip(UiText::text(QStringLiteral("cover.close_without_exporting_esc")));

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
        UiText::text(QStringLiteral("cover.reset_layout")),
        UiText::text(QStringLiteral("cover.reset_discards_all_current_layers")),
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
        UiText::text(QStringLiteral("cover.save_preset")),
        UiText::text(QStringLiteral("cover.preset_name")),
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
    dialog.setWindowTitle(UiText::text(QStringLiteral("cover.manage_presets_2")));
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
            UiText::text(QStringLiteral("cover.rename_preset")),
            UiText::text(QStringLiteral("cover.preset_name")),
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
            UiText::text(QStringLiteral("cover.delete_preset")),
            UiText::text(QStringLiteral("cover.delete_this_preset")),
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
            action->setToolTip(UiText::text(QStringLiteral("cover.this_preset_needs_a_renderable")));
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
        QAction* empty = menu->addAction(UiText::text(QStringLiteral("cover.no_recent_files")));
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : recent) {
        const bool exists = QFileInfo::exists(path);
        QAction* action = menu->addAction(QDir::toNativeSeparators(path));
        action->setEnabled(exists);
        if (!exists) {
            action->setToolTip(UiText::text(QStringLiteral("cover.file_not_found")));
        }
        connect(action, &QAction::triggered, this, [this, path] {
            if (studio_ != nullptr) studio_->importLayoutFromPath(path);
        });
    }
    menu->addSeparator();
    QAction* clear = menu->addAction(UiText::text(QStringLiteral("cover.clear_recent")));
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
    const QString title = UiText::text(QStringLiteral("cover.export_cover"));
    if (result.success) {
        const QFileInfo resolvedOutputInfo(result.outputPath);
        const QString resolvedOutputName = resolvedOutputInfo.fileName().trimmed().isEmpty()
            ? QDir::toNativeSeparators(result.outputPath)
            : resolvedOutputInfo.fileName();
        QMessageBox dialog(
            QMessageBox::Information,
            title,
            QStringLiteral("%1\n\n%2")
                .arg(UiText::text(QStringLiteral("cover.cover_export_completed")))
                .arg(resolvedOutputName),
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(this)
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        UiDialogs::configureDialogPreviewShortcuts(&dialog);
        UiDialogs::applyDetachedParentBehavior(&dialog, this);
        QPushButton* openButton = dialog.addButton(
            UiText::text(QStringLiteral("cover.open")),
            QMessageBox::AcceptRole
        );
        dialog.addButton(
            UiText::text(QStringLiteral("cover.close")),
            QMessageBox::RejectRole
        );
        dialog.setDefaultButton(openButton);
        UiDialogs::centerDialogOnAnchor(&dialog, this);
        dialog.exec();
        if (dialog.clickedButton() == openButton) {
            const QString outputDir = resolvedOutputInfo.absoluteDir().absolutePath();
            if (!outputDir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
            }
        }
        statusBar()->showMessage(QDir::toNativeSeparators(result.outputPath), 5000);
    } else {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            title,
            (UiText::text(QStringLiteral("cover.cover_export_failed_1")))
                .arg(result.errorMessage));
    }
}

}  // namespace miacode::cover_export
