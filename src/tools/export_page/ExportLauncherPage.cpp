#include "ExportLauncherPage.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "mainwindow/sections/export/MainWindow.ExportSection.h"
#include "FlowLayout.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/UiHangWatchdog.h"

#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace miacode::export_page {

namespace {

constexpr qint64 kEmbeddedPanelSyncSlowMs = 80;

QString exportPageWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 size=%3x%4 visible=%5")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0);
}

void appendEmbeddedPanelDiag(
    const QString& action,
    qint64 elapsedMs,
    const QString& detail = QString(),
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString payload = QStringLiteral("action=%1 elapsed_ms=%2").arg(action).arg(elapsedMs);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" %1").arg(detail.trimmed());
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("export_page/embedded_video_panel"),
        payload,
        /*force=*/false,
        level);
}

}  // namespace

ExportLauncherPage::ExportLauncherPage(MainWindow* owner, QWidget* parent)
    : QWidget(parent)
    , owner_(owner)
{
    setObjectName(QStringLiteral("ExportLauncherPage"));
    // Required for the `#ExportLauncherPage { background }` rule in
    // exportLauncherPageStyleSheet() to actually paint — a plain QWidget stays
    // transparent otherwise and reveals whatever ancestor paints behind it,
    // which on a theme switch could be a stale/light surface.
    setAttribute(Qt::WA_StyledBackground, true);
    // Match the other editorStack_ pages (welcome / metadata / chart). Their
    // QSizePolicy::Ignored horizontal policy makes the page ignore its own
    // (large) sizeHint and fill the stack flush-left. Without it the embedded
    // 6-tab panel's wide sizeHint drove this page off to the middle of the
    // workspace surface (the 2026-06-11 "导出页居中错位" report).
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    buildUi();
    refreshFromDocument();
}

void ExportLauncherPage::applyThemeStyles()
{
    // Paints the page's own dark canvas (#ExportLauncherPage) so every sub-page
    // — incl. the 批量导出 / 打包ZIP / 封面导出 panes — reads the theme instead of
    // revealing a stale ancestor. The embedded video panel is re-themed in place
    // by MainWindow::applyUiTheme() forwarding to its applyThemeStyles().
    setStyleSheet(UiTheme::exportLauncherPageStyleSheet());
}

void ExportLauncherPage::buildUi()
{
    applyThemeStyles();

    // Fixed-frame page (2026-06-12 redesign): badge row + underline sub-nav
    // pinned on top, the sub-page stack filling the rest. The page itself
    // never scrolls and horizontal scrolling is forbidden — content must
    // compress into the available width (the embedded panel drops the
    // dialog's 560px minimum and its tab pages scroll vertically only).
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 10, 16, 12);
    outer->setSpacing(8);

    // -------- Difficulty badge row (decision D4; hint text removed) --------
    // Wrapping flow layout: with up to 7 difficulties the pills fold to a second
    // row instead of clipping off the right edge (the page forbids horizontal
    // scrolling, and the content column is bounded by the kWorkspaceContentMinWidth
    // design budget). hSpacing/vSpacing = 8 to match the former row spacing.
    badgeRowHost_ = new QWidget(this);
    badgeRowLayout_ = new miacode::ui::FlowLayout(badgeRowHost_, 0, 8, 8);
    // Let the outer QVBoxLayout grant the host its wrapped (height-for-width)
    // height so a second row pushes the sub-nav down instead of being clipped.
    QSizePolicy badgeHostPolicy = badgeRowHost_->sizePolicy();
    badgeHostPolicy.setHeightForWidth(true);
    badgeRowHost_->setSizePolicy(badgeHostPolicy);
    outer->addWidget(badgeRowHost_);

    // -------- Underline sub-nav row --------
    // A left nav column was tried first and did not fit: the embedded 6-tab
    // panel needs the full content width on narrow workspace surfaces.
    auto* subNavRow = new QHBoxLayout();
    subNavRow->setContentsMargins(0, 2, 0, 0);
    subNavRow->setSpacing(18);
    const QStringList subNavLabels{
        UiText::text(QStringLiteral("export_page.export_video")),
        UiText::text(QStringLiteral("export_page.export_cover")),
        UiText::text(QStringLiteral("export_page.batch_export")),
        UiText::text(QStringLiteral("export_page.pack_as_zip")),
    };
    for (int subPage = 0; subPage < subNavLabels.size(); ++subPage) {
        auto* navButton = new QToolButton(this);
        navButton->setProperty("role", "subNavTab");
        navButton->setText(subNavLabels.at(subPage));
        navButton->setFont(miacode::mainwindow::shared::uiAccentFont(11, QFont::DemiBold));
        navButton->setCursor(Qt::PointingHandCursor);
        navButton->setFocusPolicy(Qt::NoFocus);
        navButton->setCheckable(true);
        navButton->setChecked(subPage == currentSubPage_);
        connect(navButton, &QToolButton::clicked, this, [this, subPage]() {
            setCurrentSubPage(subPage);
        });
        subNavRow->addWidget(navButton, 0);
        subNavButtons_.append(navButton);
    }
    subNavRow->addStretch(1);
    outer->addLayout(subNavRow);

    // Full-width hairline closing the fixed header block. Plain styled
    // QWidget — QFrame::HLine is a known-rejected divider (its content rect
    // collapses under QSS and paints nothing).
    auto* headerRule = new QWidget(this);
    headerRule->setObjectName(QStringLiteral("ExportHeaderRule"));
    headerRule->setAttribute(Qt::WA_StyledBackground, true);
    headerRule->setFixedHeight(1);
    outer->addWidget(headerRule);

    subPageStack_ = new QStackedWidget(this);

    // [0] 视频导出 — embedded panel host (the panel itself is created on
    // demand by ExportSection::createEmbeddedVideoExportPanel and inserted
    // with stretch 1: its tab area absorbs the remaining height and its
    // Start-Export footer pins to the page bottom).
    videoPanelHost_ = new QWidget(subPageStack_);
    videoPanelHostLayout_ = new QVBoxLayout(videoPanelHost_);
    videoPanelHostLayout_->setContentsMargins(0, 4, 0, 0);
    videoPanelHostLayout_->setSpacing(8);
    videoUnavailableLabel_ = new QLabel(videoPanelHost_);
    videoUnavailableLabel_->setProperty("role", "disabledReason");
    videoUnavailableLabel_->setWordWrap(true);
    videoUnavailableLabel_->hide();
    videoPanelHostLayout_->addWidget(videoUnavailableLabel_, 0, Qt::AlignTop);
    // Zero-stretch trailing spacer: it soaks up the height while the panel is
    // absent, but yields everything to the stretch-1 panel once inserted.
    videoPanelHostLayout_->addStretch(0);
    subPageStack_->addWidget(videoPanelHost_);

    // [1] 封面 — dialog launcher (the composer remains modal).
    coverCard_ = makePane(
        subPageStack_,
        UiText::text(QStringLiteral("export_page.open_composer")));
    connect(coverCard_.actionButton, &QPushButton::clicked,
            this, &ExportLauncherPage::onExportCoverClicked);
    subPageStack_->addWidget(coverCard_.frame);

    // [2] 批量导出 — embedded settings panel, matching the video page's
    // fixed-frame host. It intentionally owns a separate panel instance: a
    // badge change updates only its preview audition, preserving queue state.
    batchPanelHost_ = new QWidget(subPageStack_);
    batchPanelHostLayout_ = new QVBoxLayout(batchPanelHost_);
    batchPanelHostLayout_->setContentsMargins(0, 4, 0, 0);
    batchPanelHostLayout_->setSpacing(8);
    batchUnavailableLabel_ = new QLabel(batchPanelHost_);
    batchUnavailableLabel_->setProperty("role", "disabledReason");
    batchUnavailableLabel_->setWordWrap(true);
    batchUnavailableLabel_->hide();
    batchPanelHostLayout_->addWidget(batchUnavailableLabel_, 0, Qt::AlignTop);
    batchPanelHostLayout_->addStretch(0);
    subPageStack_->addWidget(batchPanelHost_);

    zipCard_ = makePane(
        subPageStack_,
        UiText::text(QStringLiteral("export_page.pack_now")));
    connect(zipCard_.actionButton, &QPushButton::clicked,
            this, &ExportLauncherPage::onPackAsZipClicked);
    subPageStack_->addWidget(zipCard_.frame);

    outer->addWidget(subPageStack_, 1);
}

ExportLauncherPage::LauncherCard ExportLauncherPage::makePane(QWidget* parent, const QString& buttonText)
{
    LauncherCard card;
    card.frame = new QFrame(parent);
    auto* layout = new QVBoxLayout(card.frame);
    layout->setContentsMargins(0, 10, 0, 0);
    layout->setSpacing(8);

    card.disabledReasonLabel = new QLabel(QString(), card.frame);
    card.disabledReasonLabel->setProperty("role", "disabledReason");
    card.disabledReasonLabel->setWordWrap(true);
    card.disabledReasonLabel->hide();
    layout->addWidget(card.disabledReasonLabel);

    auto* buttonRow = new QHBoxLayout();
    card.actionButton = new QPushButton(buttonText, card.frame);
    // role lets exportLauncherPageStyleSheet() theme the 封面/批量/打包ZIP pane
    // action buttons (otherwise unstyled QPushButtons that read as native/light
    // chrome and don't follow a theme switch).
    card.actionButton->setProperty("role", "paneAction");
    card.actionButton->setCursor(Qt::PointingHandCursor);
    buttonRow->addWidget(card.actionButton, 0, Qt::AlignLeft);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);
    layout->addStretch(1);
    return card;
}

bool ExportLauncherPage::difficultyExists(int difficultyId) const
{
    return !owner_.isNull()
        && SimaiDocument::isDifficultyId(difficultyId)
        && owner_->state_.document_.difficulty(difficultyId) != nullptr;
}

bool ExportLauncherPage::difficultyHasChartBody(int difficultyId) const
{
    if (!difficultyExists(difficultyId)) {
        return false;
    }
    const SimaiDifficultyData* difficulty = owner_->state_.document_.difficulty(difficultyId);
    return difficulty != nullptr && !difficulty->chart.trimmed().isEmpty();
}

bool ExportLauncherPage::documentHasChartBody() const
{
    if (owner_.isNull()) {
        return false;
    }
    const QVector<int> ids = owner_->state_.document_.difficultyIds();
    for (int id : ids) {
        if (difficultyHasChartBody(id)) {
            return true;
        }
    }
    return false;
}

bool ExportLauncherPage::documentHasPackableContent() const
{
    // Mirrors onPackAsZip's own "empty chart" guard, so a greyed card and the
    // slot's warning box can never disagree.
    return !owner_.isNull() && !owner_->state_.document_.toText().isEmpty();
}

int ExportLauncherPage::resolveDefaultDifficultyId(int previousActiveDifficultyId) const
{
    if (difficultyExists(previousActiveDifficultyId)) {
        return previousActiveDifficultyId;
    }
    // Re-entering the page keeps the user's prior badge pick.
    if (difficultyExists(selectedDifficultyId_)) {
        return selectedDifficultyId_;
    }
    if (!owner_.isNull() && difficultyExists(owner_->state_.projectLastOpenedDifficultyId_)) {
        return owner_->state_.projectLastOpenedDifficultyId_;
    }
    if (!owner_.isNull()) {
        const QVector<int> ids = owner_->state_.document_.difficultyIds();
        if (!ids.isEmpty()) {
            return ids.constFirst();
        }
    }
    return 0;
}

void ExportLauncherPage::onPageEntered(int previousActiveDifficultyId)
{
    pageSessionActive_ = true;
    setSelectedDifficulty(resolveDefaultDifficultyId(previousActiveDifficultyId));
    refreshFromDocument();
}

void ExportLauncherPage::onPageLeft()
{
    // Idempotent — called unconditionally from every page-leave path, same
    // pattern as LatencyDetectionPage::onPageLeft.
    pageSessionActive_ = false;
    syncEmbeddedVideoPanel();
}

int ExportLauncherPage::menuActionDifficultyId() const
{
    return pageSessionActive_ && difficultyExists(selectedDifficultyId_)
        ? selectedDifficultyId_
        : 0;
}

void ExportLauncherPage::refreshFromDocument()
{
    if (!difficultyExists(selectedDifficultyId_)) {
        selectedDifficultyId_ = resolveDefaultDifficultyId(0);
    }
    rebuildDifficultyBadges();
    updatePaneStates();
    syncEmbeddedVideoPanel();
}

void ExportLauncherPage::rebuildDifficultyBadges()
{
    for (QToolButton* button : badgeButtons_) {
        badgeRowLayout_->removeWidget(button);
        button->deleteLater();
    }
    badgeButtons_.clear();
    if (owner_.isNull() || badgeRowLayout_ == nullptr) {
        return;
    }
    const QVector<int> ids = owner_->state_.document_.difficultyIds();
    for (int id : ids) {
        auto* badge = new QToolButton(badgeRowHost_);
        badge->setProperty("role", "difficultyBadge");
        badge->setProperty("difficultyId", id);
        badge->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        badge->setIcon(miacode::mainwindow::shared::makeDifficultyBadgeIcon(id));
        badge->setIconSize(QSize(14, 14));
        badge->setText(SimaiDocument::difficultyShortName(id));
        badge->setFont(miacode::mainwindow::shared::uiAccentFont(10, QFont::DemiBold));
        badge->setCursor(Qt::PointingHandCursor);
        badge->setCheckable(true);
        badge->setChecked(id == selectedDifficultyId_);
        connect(badge, &QToolButton::clicked, this, [this, id]() {
            setSelectedDifficulty(id);
        });
        badgeRowLayout_->addWidget(badge);
        badgeButtons_.append(badge);
    }
}

void ExportLauncherPage::setSelectedDifficulty(int difficultyId)
{
    selectedDifficultyId_ = difficultyExists(difficultyId) ? difficultyId : 0;
    for (QToolButton* button : badgeButtons_) {
        const QSignalBlocker blocker(button);
        button->setChecked(button->property("difficultyId").toInt() == selectedDifficultyId_);
    }
    updatePaneStates();
    syncEmbeddedVideoPanel();
}

void ExportLauncherPage::setCardEnabled(LauncherCard& card, bool enabled, const QString& disabledReason)
{
    if (card.actionButton != nullptr) {
        card.actionButton->setEnabled(enabled);
    }
    if (card.disabledReasonLabel != nullptr) {
        card.disabledReasonLabel->setText(enabled ? QString() : disabledReason);
        card.disabledReasonLabel->setVisible(!enabled);
    }
}

void ExportLauncherPage::updatePaneStates()
{
    const bool hasChartBody = documentHasChartBody();
    const QString noChartReason = UiText::text(QStringLiteral("export_page.no_difficulty_has_chart_content"));
    setCardEnabled(coverCard_, hasChartBody, noChartReason);
    if (batchUnavailableLabel_ != nullptr && !hasChartBody) {
        batchUnavailableLabel_->setText(noChartReason);
    }
    setCardEnabled(
        zipCard_,
        documentHasPackableContent(),
        UiText::text(QStringLiteral("export.the_chart_is_empty_there")));
}

void ExportLauncherPage::setCurrentSubPage(int subPage)
{
    if (subPage < 0 || (subPageStack_ != nullptr && subPage >= subPageStack_->count())) {
        return;
    }
    currentSubPage_ = subPage;
    for (int i = 0; i < subNavButtons_.size(); ++i) {
        const QSignalBlocker blocker(subNavButtons_.at(i));
        subNavButtons_.at(i)->setChecked(i == currentSubPage_);
    }
    if (subPageStack_ != nullptr) {
        subPageStack_->setCurrentIndex(currentSubPage_);
    }
    syncEmbeddedVideoPanel();
}

void ExportLauncherPage::openBatchExportSubPage()
{
    setCurrentSubPage(SubPageBatch);
}

void ExportLauncherPage::syncEmbeddedVideoPanel()
{
    MC_OP("ExportLauncherPage::syncEmbeddedVideoPanel");
    QElapsedTimer totalTimer;
    totalTimer.start();
    MIACODE_HANG_PHASE(
        "ExportLauncherPage::syncEmbeddedVideoPanel",
        QStringLiteral("sub_page=%1 difficulty=%2 host=%3")
            .arg(static_cast<int>(currentSubPage_))
            .arg(selectedDifficultyId_)
            .arg(exportPageWidgetSummary(videoPanelHost_)));
    if (owner_.isNull() || owner_->exportSection_ == nullptr) {
        return;
    }
    const bool videoSubPageActive = pageSessionActive_ && currentSubPage_ == SubPageVideo;
    const bool batchSubPageActive = pageSessionActive_ && currentSubPage_ == SubPageBatch;
    const bool targetAvailable = difficultyHasChartBody(selectedDifficultyId_);

    if (!targetAvailable) {
        if (!embeddedVideoPanel_.isNull()) {
            embeddedVideoPanel_.clear();
            owner_->exportSection_->destroyEmbeddedVideoExportPanel();
            appendEmbeddedPanelDiag(
                QStringLiteral("destroy_embedded_video_panel"),
                totalTimer.elapsed(),
                QStringLiteral("video_active=%1 target_available=%2 difficulty=%3")
                    .arg(videoSubPageActive ? 1 : 0)
                    .arg(targetAvailable ? 1 : 0)
                    .arg(selectedDifficultyId_));
        }
        if (!embeddedBatchPanel_.isNull()) {
            embeddedBatchPanel_.clear();
            owner_->exportSection_->destroyEmbeddedBatchExportPanel();
        }
        if (videoUnavailableLabel_ != nullptr) {
            const QString reason = difficultyExists(selectedDifficultyId_)
                ? UiText::text(QStringLiteral("export_page.the_selected_difficulty_has_no"))
                : UiText::text(QStringLiteral("export_page.no_difficulty_is_available_to"));
            videoUnavailableLabel_->setText(reason);
            videoUnavailableLabel_->setVisible(videoSubPageActive && !targetAvailable);
        }
        if (batchUnavailableLabel_ != nullptr) {
            const QString reason = difficultyExists(selectedDifficultyId_)
                ? UiText::text(QStringLiteral("export_page.the_selected_difficulty_has_no"))
                : UiText::text(QStringLiteral("export_page.no_difficulty_is_available_to"));
            batchUnavailableLabel_->setText(reason);
            batchUnavailableLabel_->setVisible(batchSubPageActive && !targetAvailable);
        }
        return;
    }

    if (!videoSubPageActive && !embeddedVideoPanel_.isNull()) {
        embeddedVideoPanel_.clear();
        owner_->exportSection_->destroyEmbeddedVideoExportPanel();
    }

    if (!batchSubPageActive) {
        if (!embeddedBatchPanel_.isNull()) {
            embeddedBatchPanel_.clear();
            owner_->exportSection_->destroyEmbeddedBatchExportPanel();
        }
        if (batchUnavailableLabel_ != nullptr) {
            const QString reason = difficultyExists(selectedDifficultyId_)
                ? UiText::text(QStringLiteral("export_page.the_selected_difficulty_has_no"))
                : UiText::text(QStringLiteral("export_page.no_difficulty_is_available_to"));
            batchUnavailableLabel_->setText(reason);
            batchUnavailableLabel_->setVisible(batchSubPageActive && !targetAvailable);
        }
    }

    if (!targetAvailable) {
        return;
    }

    if (videoSubPageActive) {
        if (!embeddedBatchPanel_.isNull()) {
            embeddedBatchPanel_.clear();
            owner_->exportSection_->destroyEmbeddedBatchExportPanel();
        }

        // Keep the existing panel when it already targets the selected difficulty.
        if (!embeddedVideoPanel_.isNull()
            && owner_->state_.embeddedVideoExportDifficultyId_ == selectedDifficultyId_) {
            return;
        }
        if (!embeddedVideoPanel_.isNull()) {
            embeddedVideoPanel_.clear();
        }
        QWidget* panel =
            owner_->exportSection_->createEmbeddedVideoExportPanel(selectedDifficultyId_, videoPanelHost_);
        if (panel == nullptr) {
            if (videoUnavailableLabel_ != nullptr) {
                videoUnavailableLabel_->setText(
                    UiText::text(QStringLiteral("export_page.the_video_export_panel_is")));
                videoUnavailableLabel_->show();
            }
            return;
        }
        if (videoUnavailableLabel_ != nullptr) {
            videoUnavailableLabel_->hide();
        }
        embeddedVideoPanel_ = panel;
        videoPanelHostLayout_->insertWidget(videoPanelHostLayout_->count() - 1, panel, 1);
        panel->show();
        const qint64 elapsedMs = totalTimer.elapsed();
        appendEmbeddedPanelDiag(
            elapsedMs >= kEmbeddedPanelSyncSlowMs
                ? QStringLiteral("sync_embedded_video_panel_slow")
                : QStringLiteral("sync_embedded_video_panel_complete"),
            elapsedMs,
            QStringLiteral("difficulty=%1 panel=\"%2\" host=\"%3\"")
                .arg(selectedDifficultyId_)
                .arg(exportPageWidgetSummary(panel))
                .arg(exportPageWidgetSummary(videoPanelHost_)),
            elapsedMs >= kEmbeddedPanelSyncSlowMs
                ? miacode::debug_log::Level::Warn
                : miacode::debug_log::Level::Info);
        return;
    }

    if (batchSubPageActive) {
        if (!embeddedVideoPanel_.isNull()) {
            embeddedVideoPanel_.clear();
            owner_->exportSection_->destroyEmbeddedVideoExportPanel();
        }
        // The batch panel's selection is seeded once. Badge changes retarget
        // only the audition source and never recreate the settings form.
        if (!embeddedBatchPanel_.isNull()) {
            owner_->exportSection_->updateEmbeddedBatchExportPreviewDifficulty(selectedDifficultyId_);
            return;
        }
        QWidget* panel = owner_->exportSection_->createEmbeddedBatchExportPanel(
            selectedDifficultyId_, batchPanelHost_);
        if (panel == nullptr) {
            if (batchUnavailableLabel_ != nullptr) {
                batchUnavailableLabel_->setText(
                    UiText::text(QStringLiteral("export_page.the_video_export_panel_is")));
                batchUnavailableLabel_->show();
            }
            return;
        }
        if (batchUnavailableLabel_ != nullptr) {
            batchUnavailableLabel_->hide();
        }
        embeddedBatchPanel_ = panel;
        batchPanelHostLayout_->insertWidget(batchPanelHostLayout_->count() - 1, panel, 1);
        panel->show();
    }
}

void ExportLauncherPage::onExportCoverClicked()
{
    if (owner_.isNull() || owner_->exportSection_ == nullptr) {
        return;
    }
    owner_->exportSection_->onExportCover(selectedDifficultyId_);
}

void ExportLauncherPage::onBatchExportClicked()
{
    if (owner_.isNull() || owner_->exportSection_ == nullptr) {
        return;
    }
    owner_->exportSection_->onBatchExportPreviewVideo(selectedDifficultyId_);
}

void ExportLauncherPage::onPackAsZipClicked()
{
    if (owner_.isNull() || owner_->exportSection_ == nullptr) {
        return;
    }
    owner_->exportSection_->onPackAsZip();
}

}  // namespace miacode::export_page
