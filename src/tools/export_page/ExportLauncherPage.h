#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class MainWindow;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace miacode::ui {
class FlowLayout;
}

namespace miacode::export_page {

// Central-area "Export" hub page wired into MainWindow::editorStack_ — the
// phase-2 hybrid form (E-C) of the export-page migration. Fixed-frame layout: a difficulty badge row + an
// underline-style HORIZONTAL sub-nav row pinned on top (a left nav column
// did not fit: the quick-shell workspace surface can be ~700 logical px
// total and the embedded 6-tab panel needs the full content width), then
// the sub-page stack filling the rest. The page itself NEVER scrolls and
// horizontal scrolling is forbidden everywhere — only a settings tab's
// content scrolls (vertically, inside the embedded panel) when the window
// is genuinely too short. Four sub-pages:
//   视频导出 — IN-PAGE: hosts MainWindow::ExportSection's embedded
//              VideoExportDialog panel (6-tab settings filling the height +
//              a pinned 开始导出 footer; no in-panel transport — the
//              preview-area transport is the only seek/progress surface).
//   封面导出 — dialog launcher pane (composer dialog unchanged).
//   批量导出 — dialog launcher pane (queue dialog unchanged).
//   打包 ZIP — in-page action pane (onPackAsZip, existing progress popup).
// All panes are action-button-only (descriptions/mode chips removed by
// product decision 2026-06-12).
//
// While this page is current, MainWindow keeps activeDifficultyId_ == 0,
// so NOTHING here may gate on hasActiveDifficulty() — availability is
// derived from the document itself.
class ExportLauncherPage : public QWidget
{
    Q_OBJECT

public:
    explicit ExportLauncherPage(MainWindow* owner, QWidget* parent = nullptr);

    // (Re)apply the theme-aware stylesheet. Called on construction and again
    // whenever the app theme changes (light/dark).
    void applyThemeStyles();

    // Rebuild the difficulty badge row + pane states from the document.
    // Keeps the current badge/nav selection when still valid; safe to call
    // repeatedly (cheap; the embedded video panel is only rebuilt when its
    // target difficulty actually changed).
    void refreshFromDocument();

    // Called from switchToExportField with the difficulty that was active
    // BEFORE the switch reset it to 0. Seeds the badge default (decision D4):
    // previous active difficulty → kept page selection →
    // projectLastOpenedDifficultyId_ → first existing difficulty.
    void onPageEntered(int previousActiveDifficultyId);

    // Called unconditionally from every page-leave path (same idempotent
    // pattern as LatencyDetectionPage::onPageLeft): tears down the embedded
    // video panel (range-preview stop + live-preview restore + export-preview
    // session end). A running export worker keeps rendering; its inline
    // progress stays on the preview transport.
    void onPageLeft();

    int selectedDifficultyId() const { return selectedDifficultyId_; }
    int menuActionDifficultyId() const;

private:
    enum SubPage {
        SubPageVideo = 0,
        SubPageCover = 1,
        SubPageBatch = 2,
        SubPageZip = 3,
    };

    struct LauncherCard {
        QFrame* frame = nullptr;
        QPushButton* actionButton = nullptr;
        QLabel* disabledReasonLabel = nullptr;
    };

    void buildUi();
    LauncherCard makePane(QWidget* parent, const QString& buttonText);
    void rebuildDifficultyBadges();
    void setSelectedDifficulty(int difficultyId);
    void updatePaneStates();
    void setCardEnabled(LauncherCard& card, bool enabled, const QString& disabledReason);
    // Create / keep / destroy the embedded video panel to match the current
    // sub-page + badge selection. Only creates while the page session is
    // active (between onPageEntered and onPageLeft).
    void syncEmbeddedVideoPanel();
    int resolveDefaultDifficultyId(int previousActiveDifficultyId) const;
    bool difficultyExists(int difficultyId) const;
    bool difficultyHasChartBody(int difficultyId) const;
    bool documentHasChartBody() const;
    bool documentHasPackableContent() const;

    void setCurrentSubPage(int subPage);
    void onExportCoverClicked();
    void onBatchExportClicked();
    void onPackAsZipClicked();

    QPointer<MainWindow> owner_;

    QWidget* badgeRowHost_ = nullptr;
    miacode::ui::FlowLayout* badgeRowLayout_ = nullptr;
    QList<QToolButton*> badgeButtons_;
    int selectedDifficultyId_ = 0;
    bool pageSessionActive_ = false;

    QList<QToolButton*> subNavButtons_;
    int currentSubPage_ = SubPageVideo;
    QStackedWidget* subPageStack_ = nullptr;

    // 视频导出 sub-page: host for the embedded panel + the greyed-out reason.
    QWidget* videoPanelHost_ = nullptr;
    QVBoxLayout* videoPanelHostLayout_ = nullptr;
    QLabel* videoUnavailableLabel_ = nullptr;
    QPointer<QWidget> embeddedVideoPanel_;

    LauncherCard coverCard_;
    LauncherCard batchCard_;
    LauncherCard zipCard_;
};

}  // namespace miacode::export_page
