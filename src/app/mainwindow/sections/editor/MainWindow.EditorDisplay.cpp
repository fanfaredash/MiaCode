#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewSfxAssets.h"
#include "common/TimelineThemeConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {
constexpr int kBookmarkManagerVisibleRows = 5;
constexpr int kBookmarkAutoTitleMaxChars = 20;
constexpr int kBookmarkContextChars = 48;

QString normalizedBookmarkCommentText(QString text)
{
    text = text.trimmed();
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text;
}

QString bookmarkFingerprintForText(const QString& text)
{
    const QByteArray bytes = normalizedBookmarkCommentText(text).toUtf8();
    if (bytes.isEmpty()) {
        return QString();
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16));
}

bool isControlBookmarkComment(const QString& text)
{
    const QString normalized = normalizedBookmarkCommentText(text);
    if (normalized.isEmpty()) {
        return true;
    }
    static const QRegularExpression meterRe(QStringLiteral(R"(^\d+\s*/\s*\d+$)"));
    return meterRe.match(normalized).hasMatch();
}

QString defaultBookmarkTitleFromComment(const QString& text)
{
    const QString normalized = normalizedBookmarkCommentText(text);
    if (normalized.size() <= kBookmarkAutoTitleMaxChars) {
        return normalized;
    }
    return normalized.left(kBookmarkAutoTitleMaxChars);
}

struct BookmarkCommentCandidate {
    int line = 1;
    int col = 1;
    int position = 0;
    QString text;
    QString fingerprint;
    QString contextBefore;
    QString contextAfter;
};

QVector<BookmarkCommentCandidate> collectBookmarkCommentCandidates(const QString& text)
{
    QVector<BookmarkCommentCandidate> candidates;
    const QStringList lines = text.split(QLatin1Char('\n'));
    int position = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& lineText = lines.at(i);
        const int marker = lineText.indexOf(QStringLiteral("||"));
        if (marker >= 0) {
            const QString commentText = normalizedBookmarkCommentText(lineText.mid(marker + 2));
            if (!isControlBookmarkComment(commentText)) {
                BookmarkCommentCandidate candidate;
                candidate.line = i + 1;
                candidate.col = marker + 1;
                candidate.position = position + marker;
                candidate.text = commentText;
                candidate.fingerprint = bookmarkFingerprintForText(commentText);
                const int beforeStart = qMax(0, candidate.position - kBookmarkContextChars);
                candidate.contextBefore = text.mid(beforeStart, candidate.position - beforeStart);
                const int afterStart = qMin(text.size(), candidate.position + 2 + commentText.size());
                candidate.contextAfter = text.mid(afterStart, qMin(kBookmarkContextChars, text.size() - afterStart));
                candidates.append(candidate);
            }
        }
        position += lineText.size() + 1;
    }
    return candidates;
}

bool bookmarksReferToSameImportIdentity(const MainWindow::EditorBookmark& left, const MainWindow::EditorBookmark& right)
{
    if (!left.commentFingerprint.isEmpty() && left.commentFingerprint == right.commentFingerprint) {
        return true;
    }
    if (left.line == right.line && left.title.trimmed() == right.title.trimmed()) {
        return true;
    }
    return false;
}

QJsonObject bookmarkToJsonObject(const MainWindow::EditorBookmark& bookmark)
{
    QJsonObject object;
    object.insert(QStringLiteral("line"), qMax(1, bookmark.line));
    object.insert(QStringLiteral("title"), bookmark.title);
    object.insert(QStringLiteral("text"), bookmark.text);
    object.insert(QStringLiteral("source"), bookmark.source);
    object.insert(QStringLiteral("comment_text"), bookmark.commentText);
    object.insert(QStringLiteral("comment_fingerprint"), bookmark.commentFingerprint);
    object.insert(QStringLiteral("context_before"), bookmark.contextBefore);
    object.insert(QStringLiteral("context_after"), bookmark.contextAfter);
    if (bookmark.second >= 0.0) {
        object.insert(QStringLiteral("second"), bookmark.second);
    }
    return object;
}

MainWindow::EditorBookmark bookmarkFromJsonObject(const QJsonObject& object)
{
    return MainWindow::EditorBookmark{
        object.value(QStringLiteral("title")).toString(),
        object.value(QStringLiteral("text")).toString(),
        qMax(1, object.value(QStringLiteral("line")).toInt(1)),
        object.value(QStringLiteral("source")).toString(),
        object.value(QStringLiteral("comment_text")).toString(),
        object.value(QStringLiteral("comment_fingerprint")).toString(),
        object.value(QStringLiteral("context_before")).toString(),
        object.value(QStringLiteral("context_after")).toString(),
        object.value(QStringLiteral("second")).toDouble(-1.0),
    };
}

bool confirmDeleteBookmark(QWidget* parent, const MainWindow::EditorBookmark& bookmark)
{
    const QString title = UiText::isChineseUi() ? QStringLiteral("删除书签") : QStringLiteral("Delete Bookmark");
    const QString name = bookmark.title.trimmed().isEmpty()
        ? (UiText::isChineseUi() ? QStringLiteral("未命名书签") : QStringLiteral("Untitled Bookmark"))
        : bookmark.title.trimmed();
    const QString message = UiText::isChineseUi()
        ? QStringLiteral("确定删除书签“%1”吗？此操作不会删除谱面注释。").arg(name)
        : QStringLiteral("Delete bookmark \"%1\"? This will not remove chart comments.").arg(name);
    return UiDialogs::showMessageBox(
        QMessageBox::Question,
        parent,
        title,
        message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No) == QMessageBox::Yes;
}

template <typename Bookmark>
QString bookmarkTitleForDisplay(const Bookmark& bookmark)
{
    const QString title = bookmark.title.trimmed().isEmpty()
        ? (UiText::isChineseUi() ? QStringLiteral("未命名书签") : QStringLiteral("Untitled Bookmark"))
        : bookmark.title.trimmed();
    return UiText::isChineseUi()
        ? QStringLiteral("%1 行  %2").arg(bookmark.line).arg(title)
        : QStringLiteral("Line %1  %2").arg(bookmark.line).arg(title);
}

QString bookmarkDialogStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QStringLiteral(
        "QDialog#BookmarkDialog { background: %1; }"
        "QFrame#BookmarkDialogFrame { background: %1; border: 1px solid %2; border-radius: 8px; }"
        "QWidget#BookmarkDialogHeader { background: %3; border-bottom: 1px solid %2; }"
        "QLabel#BookmarkDialogTitle { color: %4; font-weight: 700; }"
        "QToolButton#BookmarkDialogWindowButton { color: %4; border: none; background: transparent; min-width: 24px; min-height: 24px; border-radius: 6px; font-size: 16px; padding: 0; }"
        "QToolButton#BookmarkDialogWindowButton:hover { background: %5; }"
        "QLineEdit, QTextEdit, QPlainTextEdit { background: %6; color: %4; border: 1px solid %7; border-radius: 6px; padding: 6px 8px; selection-background-color: %8; selection-color: %9; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: %10; }"
        "QPushButton, QToolButton { color: %4; border: 1px solid %2; border-radius: 6px; background: %6; padding: 4px 8px; }"
        "QPushButton:hover, QToolButton:hover { background: %5; border-color: %10; }"
    )
        .arg(c.cardBg.name(QColor::HexRgb))
        .arg(c.border.name(QColor::HexRgb))
        .arg(c.cardAltBg.name(QColor::HexRgb))
        .arg(c.textPrimary.name(QColor::HexRgb))
        .arg(c.menuHoverBg.name(QColor::HexRgb))
        .arg(c.inputBg.name(QColor::HexRgb))
        .arg(c.borderSoft.name(QColor::HexRgb))
        .arg(c.selection.name(QColor::HexRgb))
        .arg(c.selectionText.name(QColor::HexRgb))
        .arg(c.accent.name(QColor::HexRgb));
}

class BookmarkDialogHeader final : public QWidget
{
public:
    using MoveCallback = std::function<void(const QPoint&, bool)>;
    using ReleaseCallback = std::function<void(const QPoint&, bool)>;

    explicit BookmarkDialogHeader(QWidget* parent = nullptr, bool allowWindowButtons = true)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("BookmarkDialogHeader"));
        setCursor(Qt::OpenHandCursor);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 8, 8, 8);
        layout->setSpacing(8);

        titleLabel_ = new QLabel(this);
        titleLabel_->setObjectName(QStringLiteral("BookmarkDialogTitle"));
        titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(titleLabel_, 1);

        if (allowWindowButtons) {
            minimizeButton_ = new QToolButton(this);
            minimizeButton_->setObjectName(QStringLiteral("BookmarkDialogWindowButton"));
            minimizeButton_->setText(QStringLiteral("-"));
            minimizeButton_->setCursor(Qt::PointingHandCursor);
            minimizeButton_->setAutoRaise(true);
            minimizeButton_->setFixedSize(24, 24);
            layout->addWidget(minimizeButton_, 0, Qt::AlignTop);

            closeButton_ = new QToolButton(this);
            closeButton_->setObjectName(QStringLiteral("BookmarkDialogWindowButton"));
            closeButton_->setText(QStringLiteral("×"));
            closeButton_->setCursor(Qt::PointingHandCursor);
            closeButton_->setAutoRaise(true);
            closeButton_->setFixedSize(24, 24);
            layout->addWidget(closeButton_, 0, Qt::AlignTop);
        }
    }

    void setTitle(const QString& title)
    {
        titleLabel_->setText(title);
        titleLabel_->setToolTip(title);
    }

    QToolButton* closeButton() const { return closeButton_; }
    QToolButton* minimizeButton() const { return minimizeButton_; }
    void setMoveCallback(MoveCallback callback) { moveCallback_ = std::move(callback); }
    void setReleaseCallback(ReleaseCallback callback) { releaseCallback_ = std::move(callback); }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            dragActive_ = true;
            dragStartPos_ = event->pos();
            dragOffset_ = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragActive_ || event == nullptr || !(event->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - dragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
            dragged_ = true;
        }
        if (QWidget* top = window(); top != nullptr) {
            top->move(event->globalPosition().toPoint() - dragOffset_);
        }
        if (moveCallback_) {
            moveCallback_(event->globalPosition().toPoint(), dragged_);
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            const bool wasDragged = dragActive_ && dragged_;
            const QPoint releaseGlobalPos = event->globalPosition().toPoint();
            dragActive_ = false;
            dragged_ = false;
            setCursor(Qt::OpenHandCursor);
            if (releaseCallback_) {
                releaseCallback_(releaseGlobalPos, wasDragged);
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QLabel* titleLabel_ = nullptr;
    QToolButton* minimizeButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    QPoint dragOffset_;
    QPoint dragStartPos_;
    MoveCallback moveCallback_;
    ReleaseCallback releaseCallback_;
    bool dragActive_ = false;
    bool dragged_ = false;
};

QFrame* makeBookmarkDialogShell(QWidget* parent, BookmarkDialogHeader** headerOut, bool allowWindowButtons = true)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("BookmarkDialogFrame"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new BookmarkDialogHeader(frame, allowWindowButtons);
    if (headerOut != nullptr) {
        *headerOut = header;
    }
    layout->addWidget(header, 0);
    return frame;
}

QWidget* makeBookmarkDialogBody(QWidget* parent)
{
    auto* body = new QWidget(parent);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    return body;
}

QWidget* makeBookmarkDialogListArea(QWidget* parent)
{
    auto* body = new QWidget(parent);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    return body;
}

void wireBookmarkDialogWindowButtons(BookmarkDialogHeader* header, QDialog* dialog)
{
    if (header == nullptr || dialog == nullptr) {
        return;
    }
    if (header->minimizeButton() != nullptr) {
        QObject::connect(header->minimizeButton(), &QToolButton::clicked, dialog, &QDialog::showMinimized);
    }
    if (header->closeButton() != nullptr) {
        QObject::connect(header->closeButton(), &QToolButton::clicked, dialog, &QDialog::close);
    }
}

template <typename Bookmark>
void sortBookmarks(QVector<Bookmark>& bookmarks)
{
    std::sort(bookmarks.begin(), bookmarks.end(), [](const Bookmark& left, const Bookmark& right) {
        if (left.line != right.line) {
            return left.line < right.line;
        }
        return left.title.localeAwareCompare(right.title) < 0;
    });
}
}  // namespace

MainWindow::EditorSection::EditorSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::EditorSection::loadPortableState()
{
    state_.lastSessionFilePath_.clear();
    state_.lastOpenDir_.clear();
    state_.recentFilePaths_.clear();
    state_.lastTrackPath_.clear();
    state_.autoRestoreLastSessionFile_ = true;
    resetPortablePreviewSettingsToDefaults();
    state_.editorLineSpacingFactor_ = kEditorLineSpacingFactorDefault;
    state_.editorOverwriteModeEnabled_ = false;
    state_.editorAutoCompletionEnabled_ = true;
    state_.editorTextFontPointSize_ = qBound(
        kEditorTextFontSizeMin,
        state_.editorTextFontPointSize_ > 0 ? state_.editorTextFontPointSize_ : editorFont().pointSize(),
        kEditorTextFontSizeMax
    );

    state_.editorHeaderTopDisplay_ = EditorHeaderTopDisplay::Offset;

    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject ui = root.value("ui").toObject();
    const QJsonObject app = root.value("app").toObject();
    const QJsonObject preview = app.value("preview").toObject();

    if (ui.value("editor_text_font_size").isDouble()) {
        state_.editorTextFontPointSize_ = qBound(
            kEditorTextFontSizeMin,
            qRound(ui.value("editor_text_font_size").toDouble(state_.editorTextFontPointSize_)),
            kEditorTextFontSizeMax
        );
    }
    if (ui.value("editor_line_spacing_factor").isDouble()) {
        state_.editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(
            ui.value("editor_line_spacing_factor").toDouble(state_.editorLineSpacingFactor_)
        );
    }
    state_.editorHalfWidthInputEnabled_ = ui.value("editor_half_width_input").toBool(true);
    state_.editorOverwriteModeEnabled_ = ui.value("editor_overwrite_mode").toBool(false);
    // Unified preference. Migrate from the three legacy keys: a saved
    // "editor_auto_completion" wins; otherwise fall back to the old auto-close
    // key (the primary of the three) so existing users keep their setting.
    state_.editorAutoCompletionEnabled_ = ui.value("editor_auto_completion").toBool(
        ui.value("editor_auto_close_brackets").toBool(true));
    // 顶部显示 — which field pair the difficulty header shows ("offset" default,
    // "designer" for the per-difficulty &des_N edit).
    state_.editorHeaderTopDisplay_ =
        ui.value("editor_header_top_display").toString() == QLatin1String("designer")
            ? EditorHeaderTopDisplay::Designer
            : EditorHeaderTopDisplay::Offset;
    if (ui.value("bottom_tabs_content_scale").isDouble()) {
        // Stored as a content-scale ratio (not pixels). The raw value is clamped
        // to its valid [min,max] range by applyBottomTabsContentScale() when the
        // height is re-applied below.
        state_.bottomTabsContentScale_ =
            ui.value("bottom_tabs_content_scale").toDouble(state_.bottomTabsContentScale_);
    }
    if (ui.value("preview_pane_width_ratio").isDouble()) {
        state_.previewPaneWidthRatio_ =
            qBound(0.0, ui.value("preview_pane_width_ratio").toDouble(state_.previewPaneWidthRatio_), 1.0);
    }
    if (ui.value("outline_dock_collapsed").isBool()) {
        state_.outlineDockCollapsed_ = ui.value("outline_dock_collapsed").toBool(state_.outlineDockCollapsed_);
    }
    if (ui.value("outline_dock_expanded_width").isDouble()) {
        state_.outlineDockExpandedWidth_ = qBound(
            120,
            qRound(ui.value("outline_dock_expanded_width").toDouble(state_.outlineDockExpandedWidth_)),
            640
        );
    }
    state_.editorImeInputDisabled_ = ui.value("editor_ime_input_disabled").toBool(true);
    applyEditorTextFontSize(state_.editorTextFontPointSize_, false);
    applyEditorHalfWidthInputEnabled(state_.editorHalfWidthInputEnabled_, false);
    applyEditorOverwriteModeEnabled(state_.editorOverwriteModeEnabled_, false);
    applyEditorAutoCompletionEnabled(state_.editorAutoCompletionEnabled_, false);
    applyEditorHeaderTopDisplay(state_.editorHeaderTopDisplay_, false);
    applyEditorImeInputDisabled(state_.editorImeInputDisabled_, false);

    const QString dir = app.value("last_open_dir").toString();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        state_.lastOpenDir_ = QDir::cleanPath(dir);
    }
    const QString lastOpenFile = app.value("last_open_file").toString();
    if (!lastOpenFile.isEmpty()) {
        state_.lastSessionFilePath_ = QDir::cleanPath(lastOpenFile);
    }
    if (app.value("auto_restore_last_open_file").isBool()) {
        state_.autoRestoreLastSessionFile_ = app.value("auto_restore_last_open_file").toBool(true);
    }
    const QJsonArray recentFiles = app.value("recent_files").toArray();
    QSet<QString> seenRecentFiles;
    for (const QJsonValue& value : recentFiles) {
        const QString path = QDir::cleanPath(value.toString().trimmed());
        if (path.isEmpty() || seenRecentFiles.contains(path)) {
            continue;
        }
        seenRecentFiles.insert(path);
        state_.recentFilePaths_.append(path);
        if (state_.recentFilePaths_.size() >= 10) {
            break;
        }
    }
    // `last_track_path` persistence removed (2026-06-03): it is a value derived
    // from the opened chart's directory and was reloaded at startup before the
    // document was restored, which let a stale cross-session track path leak into
    // preview audio. The track path is now always resolved fresh from the loaded
    // chart (see TimelineSection::setCurrentFilePath), so nothing is read here.
    if (app.value("show_slide_tracks").isBool()) {
        state_.showSlideTracks_ = app.value("show_slide_tracks").toBool(state_.showSlideTracks_);
    }
    applyPortablePreviewSettings(preview);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        if (preview.value("timeline_zoom_scale").isDouble()) {
            state_.timelineQuickStateBridge_->setZoomScale(
                preview.value("timeline_zoom_scale").toDouble(state_.timelineQuickStateBridge_->zoomScale())
            );
        }
        if (preview.value("timeline_waveform_brightness").isDouble()) {
            state_.timelineQuickStateBridge_->setWaveformBrightness(
                preview.value("timeline_waveform_brightness").toDouble(
                    miacode::timeline::kTimelineWaveformBrightnessDefault)
            );
        }
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(state_.previewFollowEnabled_);
        state_.timelineQuickStateBridge_->setViewportLockEnabled(state_.previewViewportLockEnabled_);
        state_.timelineQuickStateBridge_->setFollowProgressEnabled(state_.previewProgressFollowEnabled_);
        state_.timelineQuickStateBridge_->setTimelineSyncEnabled(state_.timelineSyncEnabled_);
    }
    // Re-apply the restored bottom-tabs divider height. The constructor already
    // ran updateBottomTabsDeviceHeight() once with the default scale before this
    // load; push the persisted scale into the layout now (it also clamps it).
    if (owner_.windowSection_ != nullptr) {
        owner_.windowSection_->updateBottomTabsDeviceHeight();
    }
    owner_.refreshPreviewFrameRateTimers();
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "editor_display_apply_settings");
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
    }
}

void MainWindow::EditorSection::resetPortablePreviewSettingsToDefaults()
{
    state_.softwarePreviewAudioSettings_ = PreviewAudioSettings();
    state_.previewAudioSettings_ = state_.softwarePreviewAudioSettings_;
    state_.softwarePreviewTimingSettings_ = PreviewTimingSettings();
    state_.previewTimingSettings_ = state_.softwarePreviewTimingSettings_;
    state_.previewPlaybackRate_ = 1.0;
    state_.showSlideTracks_ = true;
    state_.showJudgeMarkers_ = false;
    state_.showTouchTrail_ = false;
    state_.previewFollowEnabled_ = false;
    state_.previewViewportLockEnabled_ = true;
    state_.previewProgressFollowEnabled_ = true;
    state_.timelineSyncEnabled_ = false;
    state_.muriRenderOptions_ = MuriRenderOptions();
    state_.staticTapOnSlideThresholdMs_ = miacode::muri::kStaticTapOnSlideThresholdDefaultMs;
    state_.previewBackgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    state_.previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    state_.previewLayoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    state_.previewSmoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    state_.previewOutlineVariant_ = PreviewOutlineVariant::Line;
    state_.previewOutlineVariantUsesAutoSelection_ = true;
    state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    state_.previewTapFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    state_.previewTouchFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    state_.previewSlideEarlierSecondAndTextOnTop_ = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    state_.previewSkinVariant_ = PreviewSkinVariant::Standard;
    state_.previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    state_.previewStageMediaFrameRateMode_ = PreviewCanvasFrameRateMode::Fps30;
    state_.videoDecodePrefersSoftware_ = false;
    state_.timelineFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    state_.previewCanvasAspectRatio_ = 1.0;
    state_.previewAutoRestoreSquareAfterExport_ = false;
    state_.previewForceLabeledJudgeLineWhenPaused_ = true;
    state_.previewShowDebugInfo_ = false;
    state_.previewShowTimestamp_ = true;
    state_.previewShowObjectStatsHud_ = false;
    state_.exportShowObjectStatsHud_ = false;
    state_.previewShowChartInfoHud_ = false;
    state_.exportShowChartInfoHud_ = false;
    state_.previewShowValidationSummary_ = true;
    state_.chartNormalizeStartAtNewMeasure_ = true;
    state_.chartNormalizeReduceTo384Grid_ = false;
    state_.workspacePanelsSwapped_ = false;
}

void MainWindow::EditorSection::applyPortablePreviewSettings(const QJsonObject& preview)
{
    if (preview.value("preview_playback_rate").isDouble()) {
        state_.previewPlaybackRate_ = qMax(
            0.25,
            preview.value("preview_playback_rate").toDouble(state_.previewPlaybackRate_)
        );
    }
    if (preview.value("show_slide_tracks").isBool()) {
        state_.showSlideTracks_ = preview.value("show_slide_tracks").toBool(state_.showSlideTracks_);
    }
    if (preview.value("show_judge_markers").isBool()) {
        state_.showJudgeMarkers_ = preview.value("show_judge_markers").toBool(state_.showJudgeMarkers_);
    }
    if (preview.value("show_touch_trail").isBool()) {
        state_.showTouchTrail_ = preview.value("show_touch_trail").toBool(state_.showTouchTrail_);
    }
    if (preview.value("static_tap_on_slide_threshold_ms").isDouble()) {
        state_.staticTapOnSlideThresholdMs_ = qBound(
            miacode::muri::kStaticTapOnSlideThresholdMinMs,
            qRound(preview.value("static_tap_on_slide_threshold_ms")
                       .toDouble(state_.staticTapOnSlideThresholdMs_)),
            miacode::muri::kStaticTapOnSlideThresholdMaxMs
        );
    }
    const QString muriRenderMode = preview.value("muri_render_mode").toString().trimmed().toLower();
    state_.muriRenderOptions_.renderMode =
        muriRenderMode == QLatin1String("maimuri_dx_style")
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
    if (preview.value("show_chart_review_slide_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay =
            preview.value("show_chart_review_slide_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    }
    if (preview.value("show_chart_review_tap_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewTapJudgeOverlay =
            preview.value("show_chart_review_tap_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewTapJudgeOverlay);
    }
    if (preview.value("show_chart_review_touch_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay =
            preview.value("show_chart_review_touch_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay);
    }
    if (preview.value("wifi_need_c").isBool()) {
        state_.muriRenderOptions_.wifiNeedC = preview.value("wifi_need_c").toBool(state_.muriRenderOptions_.wifiNeedC);
    }
    state_.muriRenderOptions_.excludeTouchFromMultiTouch = true;
    if (preview.value("outline_variant").isString()) {
        const QString outlineVariant = preview.value("outline_variant").toString().trimmed();
        if (!outlineVariant.isEmpty()) {
            state_.previewOutlineVariant_ = owner_.previewOutlineVariantFromStorageValue(outlineVariant);
            state_.previewOutlineVariantUsesAutoSelection_ = false;
        }
    }
    if (preview.value("custom_outline_file").isString()) {
        state_.previewCustomOutlineFileName_ = QFileInfo(preview.value("custom_outline_file").toString().trimmed()).fileName();
        if (!state_.previewCustomOutlineFileName_.isEmpty()) {
            state_.previewOutlineVariantUsesAutoSelection_ = false;
        }
    }
    const double legacyBrightness = qBound(
        0.0,
        preview.value("background_brightness").toDouble(miacode::preview_video::kBackgroundBrightnessDefault),
        1.0
    );
    if (preview.value("background_brightness_outer").isDouble()) {
        state_.previewBackgroundBrightnessOuter_ =
            qBound(0.0, preview.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
    } else {
        state_.previewBackgroundBrightnessOuter_ = legacyBrightness;
    }
    if (preview.value("background_brightness_inner").isDouble()) {
        state_.previewBackgroundBrightnessInner_ =
            qBound(0.0, preview.value("background_brightness_inner").toDouble(miacode::preview_video::kBackgroundBrightnessInnerDefault), 1.0);
    } else {
        state_.previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    }
    if (preview.value("layout_square_scale").isDouble()) {
        state_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
            preview.value("layout_square_scale").toDouble(state_.previewLayoutSquareScale_)
        );
    }
    if (preview.value("smooth_brightness").isBool()) {
        state_.previewSmoothBrightness_ = preview.value("smooth_brightness").toBool(state_.previewSmoothBrightness_);
    }
    const QString scaleMode = preview.value("background_scale_mode").toString().trimmed().toLower();
    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
    } else if (scaleMode == QLatin1String("square_fit")
        || scaleMode == QLatin1String("square-fit")
        || scaleMode == QLatin1String("square_fill")
        || scaleMode == QLatin1String("square-fill")
        || scaleMode == QLatin1String("square")) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::SquareFitContain;
    } else if (!scaleMode.isEmpty()) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    }
    const double legacyFlowSpeed = preview.value("note_flow_speed").toDouble(
        miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed
    );
    if (preview.value("tap_flow_speed").isDouble()) {
        state_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
            preview.value("tap_flow_speed").toDouble(state_.previewTapFlowSpeed_)
        );
    } else if (preview.value("note_flow_speed").isDouble()) {
        state_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(legacyFlowSpeed);
    }
    if (preview.value("touch_flow_speed").isDouble()) {
        state_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
            preview.value("touch_flow_speed").toDouble(state_.previewTouchFlowSpeed_)
        );
    } else if (preview.value("note_flow_speed").isDouble()) {
        state_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(legacyFlowSpeed);
    }
    if (preview.value("slide_earlier_second_and_text_on_top").isBool()) {
        state_.previewSlideEarlierSecondAndTextOnTop_ =
            preview.value("slide_earlier_second_and_text_on_top")
                .toBool(state_.previewSlideEarlierSecondAndTextOnTop_);
    }
    if (preview.value("skin_variant").isString()) {
        const QString skinValue = preview.value("skin_variant").toString().trimmed();
        const QString normalizedSkinValue = skinValue.toLower();
        state_.previewSkinVariant_ = owner_.previewSkinVariantFromStorageValue(skinValue);
        state_.previewSkinDirectoryName_ =
            state_.previewSkinVariant_ == PreviewSkinVariant::Dx ? QStringLiteral("skinDX") : QStringLiteral("skinSD");
        if (!skinValue.isEmpty()
            && normalizedSkinValue != QLatin1String("standard")
            && normalizedSkinValue != QLatin1String("std")
            && normalizedSkinValue != QLatin1String("skin")
            && normalizedSkinValue != QLatin1String("skinsd")
            && normalizedSkinValue != QLatin1String("skinstd")
            && normalizedSkinValue != QLatin1String("dx")
            && normalizedSkinValue != QLatin1String("skin_dx")
            && normalizedSkinValue != QLatin1String("skindx")) {
            state_.previewSkinDirectoryName_ = skinValue;
        }
    }
    if (preview.value("intro_sound_file").isString()) {
        state_.previewIntroSoundFileName_ =
            miacode::preview_sfx::normalizeIntroSoundFileName(preview.value("intro_sound_file").toString());
    }
    miacode::preview_sfx::setSelectedIntroSoundFileName(state_.previewIntroSoundFileName_);
    if (preview.value("canvas_frame_rate_mode").isString()) {
        state_.previewCanvasFrameRateMode_ =
            owner_.previewCanvasFrameRateModeFromStorageValue(preview.value("canvas_frame_rate_mode").toString());
    }
    if (preview.value("pv_frame_rate_mode").isString()) {
        state_.previewStageMediaFrameRateMode_ = owner_.previewFrameRateModeFromStorageValue(
            preview.value("pv_frame_rate_mode").toString(),
            PreviewCanvasFrameRateMode::Fps30);
    }
    if (preview.value("video_decode_prefers_software").isBool()) {
        state_.videoDecodePrefersSoftware_ =
            preview.value("video_decode_prefers_software").toBool(state_.videoDecodePrefersSoftware_);
    }
    if (preview.value("timeline_frame_rate_mode").isString()) {
        state_.timelineFrameRateMode_ = owner_.previewFrameRateModeFromStorageValue(
            preview.value("timeline_frame_rate_mode").toString(),
            state_.previewCanvasFrameRateMode_);
    } else {
        state_.timelineFrameRateMode_ = state_.previewCanvasFrameRateMode_;
    }
    if (preview.value("force_labeled_judge_line_when_paused").isBool()) {
        state_.previewForceLabeledJudgeLineWhenPaused_ =
            preview.value("force_labeled_judge_line_when_paused")
                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
    } else if (preview.value("hide_stage_media_when_paused").isBool()) {
        state_.previewForceLabeledJudgeLineWhenPaused_ =
            preview.value("hide_stage_media_when_paused")
                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
    }
    if (preview.value("show_debug_info").isBool()) {
        state_.previewShowDebugInfo_ = preview.value("show_debug_info").toBool(false);
    }
    if (preview.value("show_timestamp").isBool()) {
        state_.previewShowTimestamp_ = preview.value("show_timestamp").toBool(true);
    }
    if (preview.value("show_object_stats_preview").isBool()) {
        state_.previewShowObjectStatsHud_ = preview.value("show_object_stats_preview").toBool(false);
    }
    if (preview.value("show_object_stats_export").isBool()) {
        state_.exportShowObjectStatsHud_ = preview.value("show_object_stats_export").toBool(false);
    }
    state_.previewCenterDisplayMode_ = miacode::preview_gameplay::centerDisplayModeFromToken(
        preview.value(QStringLiteral("center_display_mode")).toString(
            QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(state_.previewCenterDisplayMode_))));
    const bool unifiedObjectStatsHud = state_.previewShowObjectStatsHud_ || state_.exportShowObjectStatsHud_;
    state_.previewShowObjectStatsHud_ = unifiedObjectStatsHud;
    state_.exportShowObjectStatsHud_ = unifiedObjectStatsHud;
    if (preview.value("show_chart_info_preview").isBool()) {
        state_.previewShowChartInfoHud_ = preview.value("show_chart_info_preview").toBool(false);
    }
    if (preview.value("show_chart_info_export").isBool()) {
        state_.exportShowChartInfoHud_ = preview.value("show_chart_info_export").toBool(false);
    }
    const bool unifiedChartInfoHud = state_.previewShowChartInfoHud_ || state_.exportShowChartInfoHud_;
    state_.previewShowChartInfoHud_ = unifiedChartInfoHud;
    state_.exportShowChartInfoHud_ = unifiedChartInfoHud;
    if (preview.value("show_validation_summary").isBool()) {
        state_.previewShowValidationSummary_ = preview.value("show_validation_summary").toBool(true);
    }
    if (preview.value("follow_preview").isBool()) {
        state_.previewFollowEnabled_ = preview.value("follow_preview").toBool(false);
    }
    state_.previewViewportLockEnabled_ = true;
    state_.previewProgressFollowEnabled_ = true;
    if (preview.value("timeline_sync").isBool()) {
        state_.timelineSyncEnabled_ = preview.value("timeline_sync").toBool(false);
    }
    const miacode::chart_transform::ChartNormalizationOptions normalizationOptions =
        miacode::chart_transform::chartNormalizationOptionsFromPreferences(
            preview,
            miacode::chart_transform::ChartNormalizationOptions{true, false});
    state_.chartNormalizeStartAtNewMeasure_ = normalizationOptions.startAtNewMeasure;
    state_.chartNormalizeReduceTo384Grid_ = normalizationOptions.reduceTo384Grid;
    if (preview.value("swap_side_panels").isBool()) {
        state_.workspacePanelsSwapped_ = preview.value("swap_side_panels").toBool(false);
    }
    state_.previewCanvasAspectRatio_ = 1.0;
    state_.previewAutoRestoreSquareAfterExport_ = false;
    if (preview.value("audio").isObject()) {
        state_.softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview.value("audio").toObject());
    } else {
        state_.softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview);
    }
    state_.softwarePreviewAudioSettings_.normalize();
    state_.previewAudioSettings_ = state_.softwarePreviewAudioSettings_;
    state_.softwarePreviewTimingSettings_ = PreviewTimingSettings::fromJson(preview.value("timing").toObject());
    state_.softwarePreviewTimingSettings_.normalize();
    state_.previewTimingSettings_ = state_.softwarePreviewTimingSettings_;
}

void MainWindow::EditorSection::savePortableState() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    QJsonObject app = root.value("app").toObject();
    QJsonObject preview = app.value("preview").toObject();

    ui.insert("editor_text_font_size", state_.editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", state_.editorLineSpacingFactor_);
    ui.insert("editor_half_width_input", state_.editorHalfWidthInputEnabled_);
    ui.insert("editor_overwrite_mode", state_.editorOverwriteModeEnabled_);
    ui.insert("editor_auto_completion", state_.editorAutoCompletionEnabled_);
    ui.insert(
        "editor_header_top_display",
        state_.editorHeaderTopDisplay_ == EditorHeaderTopDisplay::Designer
            ? QStringLiteral("designer")
            : QStringLiteral("offset")
    );
    // Bottom-tabs (timeline/validation/muri) divider height, persisted as a
    // content-scale ratio rather than pixels (see loadPortableState()).
    ui.insert("bottom_tabs_content_scale", state_.bottomTabsContentScale_);
    if (state_.previewPaneWidthRatio_ > 0.0) {
        ui.insert("preview_pane_width_ratio", state_.previewPaneWidthRatio_);
    } else {
        ui.remove("preview_pane_width_ratio");
    }
    ui.insert("outline_dock_collapsed", state_.outlineDockCollapsed_);
    ui.insert("outline_dock_expanded_width", state_.outlineDockExpandedWidth_);
    ui.insert("editor_ime_input_disabled", state_.editorImeInputDisabled_);
    root.insert("ui", ui);

    app.insert("last_open_dir", state_.lastOpenDir_);
    app.insert("last_open_file", state_.lastSessionFilePath_);
    app.insert("auto_restore_last_open_file", state_.autoRestoreLastSessionFile_);
    QJsonArray recentFiles;
    for (const QString& path : state_.recentFilePaths_) {
        if (!path.trimmed().isEmpty()) {
            recentFiles.append(path);
        }
    }
    app.insert("recent_files", recentFiles);
    // `last_track_path` no longer persisted (derived value; see loadPortableState).
    app.insert("show_slide_tracks", state_.showSlideTracks_);

    preview.insert("preview_playback_rate", state_.previewPlaybackRate_);
    preview.insert("show_slide_tracks", state_.showSlideTracks_);
    preview.insert("show_judge_markers", state_.showJudgeMarkers_);
    preview.insert("show_touch_trail", state_.showTouchTrail_);
    preview.insert("static_tap_on_slide_threshold_ms", state_.staticTapOnSlideThresholdMs_);
    preview.insert(
        "muri_render_mode",
        state_.muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle
            ? QStringLiteral("maimuri_dx_style")
            : QStringLiteral("native")
    );
    preview.insert("show_chart_review_slide_judge_overlay", state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    preview.insert("show_chart_review_tap_judge_overlay", state_.muriRenderOptions_.showChartReviewTapJudgeOverlay);
    preview.insert("show_chart_review_touch_judge_overlay", state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay);
    preview.insert("wifi_need_c", state_.muriRenderOptions_.wifiNeedC);
    preview.insert("background_brightness", state_.previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_outer", state_.previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_inner", state_.previewBackgroundBrightnessInner_);
    preview.insert("layout_square_scale", state_.previewLayoutSquareScale_);
    preview.insert("smooth_brightness", state_.previewSmoothBrightness_);
    if (state_.previewOutlineVariantUsesAutoSelection_) {
        preview.remove("outline_variant");
        preview.remove("custom_outline_file");
    } else {
        preview.insert("outline_variant", owner_.previewOutlineVariantStorageValue());
        if (state_.previewCustomOutlineFileName_.isEmpty()) {
            preview.remove("custom_outline_file");
        } else {
            preview.insert("custom_outline_file", state_.previewCustomOutlineFileName_);
        }
    }
    preview.insert(
        "background_scale_mode",
        state_.previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : (state_.previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::SquareFitContain
                   ? QStringLiteral("square_fit")
                   : QStringLiteral("fill"))
    );
    preview.insert("tap_flow_speed", state_.previewTapFlowSpeed_);
    preview.insert("touch_flow_speed", state_.previewTouchFlowSpeed_);
    preview.insert("slide_earlier_second_and_text_on_top", state_.previewSlideEarlierSecondAndTextOnTop_);
    preview.insert("skin_variant", owner_.previewSkinVariantStorageValue());
    if (state_.previewIntroSoundFileName_.trimmed().isEmpty()) {
        preview.remove("intro_sound_file");
    } else {
        preview.insert("intro_sound_file", state_.previewIntroSoundFileName_);
    }
    preview.insert("canvas_frame_rate_mode", owner_.previewCanvasFrameRateModeStorageValue());
    preview.insert("pv_frame_rate_mode", owner_.previewStageMediaFrameRateModeStorageValue());
    preview.insert("video_decode_prefers_software", owner_.currentVideoDecodePrefersSoftware());
    preview.insert("timeline_frame_rate_mode", owner_.timelineFrameRateModeStorageValue());
    preview.insert(
        "force_labeled_judge_line_when_paused",
        state_.previewForceLabeledJudgeLineWhenPaused_
    );
    preview.insert("show_debug_info", state_.previewShowDebugInfo_);
    preview.insert("show_timestamp", state_.previewShowTimestamp_);
    preview.insert("show_object_stats_preview", state_.previewShowObjectStatsHud_);
    preview.insert("show_object_stats_export", state_.exportShowObjectStatsHud_);
    preview.insert("show_chart_info_preview", state_.previewShowChartInfoHud_);
    preview.insert("show_chart_info_export", state_.exportShowChartInfoHud_);
    preview.insert(
        "center_display_mode",
        QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(state_.previewCenterDisplayMode_))
    );
    preview.insert("show_validation_summary", state_.previewShowValidationSummary_);
    preview.insert("follow_preview", state_.previewFollowEnabled_);
    preview.insert("viewport_lock", true);
    preview.insert("follow_progress", true);
    preview.insert("timeline_sync", state_.timelineSyncEnabled_);
    preview.insert(
        "timeline_zoom_scale",
        state_.timelineQuickStateBridge_ != nullptr ? state_.timelineQuickStateBridge_->zoomScale() : 0.5
    );
    preview.insert(
        "timeline_waveform_brightness",
        state_.timelineQuickStateBridge_ != nullptr
            ? state_.timelineQuickStateBridge_->waveformBrightness()
            : miacode::timeline::kTimelineWaveformBrightnessDefault
    );
    miacode::chart_transform::saveChartNormalizationOptionsToPreferences(
        &preview,
        miacode::chart_transform::ChartNormalizationOptions{
            state_.chartNormalizeStartAtNewMeasure_,
            state_.chartNormalizeReduceTo384Grid_});
    preview.insert("swap_side_panels", state_.workspacePanelsSwapped_);
    preview.insert("canvas_aspect_ratio", 1.0);
    preview.insert("auto_restore_square_after_export", false);
    preview.insert("audio", state_.softwarePreviewAudioSettings_.toJson());
    preview.insert("timing", state_.softwarePreviewTimingSettings_.toJson());

    app.insert("preview", preview);
    root.insert("app", app);
    UiText::savePreferencesObject(root);
}

void MainWindow::EditorSection::persistEditorTextFontPreference() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    ui.insert("editor_text_font_size", state_.editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", state_.editorLineSpacingFactor_);
    ui.insert("editor_half_width_input", state_.editorHalfWidthInputEnabled_);
    ui.insert("editor_overwrite_mode", state_.editorOverwriteModeEnabled_);
    ui.insert("editor_auto_completion", state_.editorAutoCompletionEnabled_);
    ui.insert(
        "editor_header_top_display",
        state_.editorHeaderTopDisplay_ == EditorHeaderTopDisplay::Designer
            ? QStringLiteral("designer")
            : QStringLiteral("offset")
    );
    ui.insert("editor_ime_input_disabled", state_.editorImeInputDisabled_);
    root.insert("ui", ui);
    UiText::savePreferencesObject(root);
}

void MainWindow::EditorSection::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    const int normalized = qBound(kEditorTextFontSizeMin, pointSize, kEditorTextFontSizeMax);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(normalized, state_.editorLineSpacingFactor_);
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorTextFontPointSize_ = normalized;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setFont(editorFont(normalized));
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    syncCopyAreaEditorAppearance();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        owner_.windowSection_->updateBottomTabsDeviceHeight();
    }
    if (ui_.metadataExtraEdit_ != nullptr) {
        ui_.metadataExtraEdit_->setFont(editorFont(normalized));
        applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixels);
    }
    state_.suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(factor);
    const int blockSpacingPixels =
        blockSpacingPixelsForPointSize(state_.editorTextFontPointSize_, state_.editorLineSpacingFactor_);
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    syncCopyAreaEditorAppearance();
    if (ui_.metadataExtraEdit_ != nullptr) {
        applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixels);
    }
    state_.suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorHalfWidthInputEnabled(bool enabled, bool persistPreference)
{
    state_.editorHalfWidthInputEnabled_ = enabled;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        editor->setHalfWidthInputEnabled(enabled);
    }
    if (ui_.copyAreaEditor_ != nullptr) {
        ui_.copyAreaEditor_->setHalfWidthInputEnabled(enabled);
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference)
{
    state_.editorOverwriteModeEnabled_ = enabled;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setEditorOverwriteMode(enabled);
    }
    if (ui_.copyAreaEditor_ != nullptr) {
        QSignalBlocker blocker(ui_.copyAreaEditor_);
        ui_.copyAreaEditor_->setEditorOverwriteMode(enabled);
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorAutoCompletionEnabled(bool enabled, bool persistPreference)
{
    state_.editorAutoCompletionEnabled_ = enabled;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        editor->setAutoCompletionEnabled(enabled);
    }
    if (ui_.copyAreaEditor_ != nullptr) {
        ui_.copyAreaEditor_->setAutoCompletionEnabled(enabled);
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorHeaderTopDisplay(EditorHeaderTopDisplay mode, bool persistPreference)
{
    state_.editorHeaderTopDisplay_ = mode;
    if (owner_.documentSection_ != nullptr) {
        // Refresh the header designer edit from the model before unhiding it
        // (covers a mode flip that races a pending designer rewrite), then let
        // the layout-mode pass apply the offset/designer pair visibility.
        owner_.documentSection_->syncHeaderDesignerEditFromModel();
        owner_.documentSection_->updateEditorHeaderLayoutMode();
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorImeInputDisabled(bool disabled, bool persistPreference)
{
    state_.editorImeInputDisabled_ = disabled;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        editor->setImeInputDisabled(disabled);
    }
    if (ui_.copyAreaEditor_ != nullptr) {
        ui_.copyAreaEditor_->setImeInputDisabled(disabled);
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::refreshEditorBookmarkLines()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        return;
    }
    QSet<int> lines;
    for (const EditorBookmark& bookmark : std::as_const(state_.editorBookmarks_)) {
        if (bookmark.line > 0) {
            lines.insert(bookmark.line);
        }
    }
    editor->setBookmarkedLines(lines);
}

void MainWindow::EditorSection::toggleBookmarkSidebarExpanded()
{
    state_.editorBookmarksSidebarExpanded_ = !state_.editorBookmarksSidebarExpanded_;
}

void MainWindow::EditorSection::syncBookmarksFromEditorText(int changePosition, int charsRemoved, int charsAdded)
{
    const QString text = owner_.editorText();
    const QString previousText = state_.editorBookmarkLastSyncedText_;
    const QVector<BookmarkCommentCandidate> candidates = collectBookmarkCommentCandidates(text);

    bool mutated = false;
    if (changePosition >= 0) {
        const int addedLineCount = charsAdded > 0
            ? text.mid(changePosition, charsAdded).count(QLatin1Char('\n'))
            : 0;
        const int removedLineCount = charsRemoved > 0
            ? previousText.mid(changePosition, charsRemoved).count(QLatin1Char('\n'))
            : 0;
        const int lineDelta = addedLineCount - removedLineCount;
        if (lineDelta != 0) {
            int changedLine = 1;
            const QString& lineSource = !previousText.isEmpty() ? previousText : text;
            for (int i = 0; i < changePosition && i < lineSource.size(); ++i) {
                if (lineSource.at(i) == QLatin1Char('\n')) {
                    ++changedLine;
                }
            }
            for (EditorBookmark& bookmark : state_.editorBookmarks_) {
                if (bookmark.line > changedLine || (lineDelta > 0 && bookmark.line >= changedLine)) {
                    const int shiftedLine = qMax(1, bookmark.line + lineDelta);
                    if (bookmark.line != shiftedLine) {
                        bookmark.line = shiftedLine;
                        mutated = true;
                    }
                }
            }
        }
    }

    QSet<QString> usedFingerprints;
    QSet<int> usedCommentLines;
    for (EditorBookmark& bookmark : state_.editorBookmarks_) {
        if (bookmark.commentFingerprint.isEmpty()) {
            continue;
        }
        for (const BookmarkCommentCandidate& candidate : candidates) {
            if (candidate.fingerprint != bookmark.commentFingerprint) {
                continue;
            }
            usedFingerprints.insert(candidate.fingerprint);
            usedCommentLines.insert(candidate.line);
            if (bookmark.line != candidate.line
                || bookmark.commentText != candidate.text
                || bookmark.contextBefore != candidate.contextBefore
                || bookmark.contextAfter != candidate.contextAfter) {
                bookmark.line = candidate.line;
                bookmark.commentText = candidate.text;
                bookmark.contextBefore = candidate.contextBefore;
                bookmark.contextAfter = candidate.contextAfter;
                bookmark.second = owner_.timelineSecondForCursor(candidate.line, candidate.col);
                mutated = true;
            }
            break;
        }
    }

    for (const BookmarkCommentCandidate& candidate : candidates) {
        if (candidate.fingerprint.isEmpty() || usedCommentLines.contains(candidate.line)) {
            continue;
        }
        auto existingLineBookmark = std::find_if(
            state_.editorBookmarks_.begin(),
            state_.editorBookmarks_.end(),
            [&candidate](const EditorBookmark& bookmark) {
                return bookmark.line == candidate.line
                    && bookmark.source == QLatin1String("comment");
            });
        if (existingLineBookmark == state_.editorBookmarks_.end()) {
            continue;
        }
        const QString previousCommentText = existingLineBookmark->commentText;
        existingLineBookmark->title = defaultBookmarkTitleFromComment(candidate.text);
        if (existingLineBookmark->text.trimmed().isEmpty()
            || existingLineBookmark->text == previousCommentText) {
            existingLineBookmark->text = candidate.text;
        }
        existingLineBookmark->commentText = candidate.text;
        existingLineBookmark->commentFingerprint = candidate.fingerprint;
        existingLineBookmark->contextBefore = candidate.contextBefore;
        existingLineBookmark->contextAfter = candidate.contextAfter;
        existingLineBookmark->second = owner_.timelineSecondForCursor(candidate.line, candidate.col);
        usedFingerprints.insert(candidate.fingerprint);
        usedCommentLines.insert(candidate.line);
        mutated = true;
    }

    for (const BookmarkCommentCandidate& candidate : candidates) {
        if (candidate.fingerprint.isEmpty()
            || usedFingerprints.contains(candidate.fingerprint)
            || usedCommentLines.contains(candidate.line)) {
            continue;
        }
        const auto existing = std::find_if(
            state_.editorBookmarks_.cbegin(),
            state_.editorBookmarks_.cend(),
            [&candidate](const EditorBookmark& bookmark) {
                return bookmark.commentFingerprint == candidate.fingerprint
                    || bookmark.line == candidate.line;
            });
        if (existing != state_.editorBookmarks_.cend()) {
            continue;
        }
        state_.editorBookmarks_.append(EditorBookmark{
            defaultBookmarkTitleFromComment(candidate.text),
            candidate.text,
            candidate.line,
            QStringLiteral("comment"),
            candidate.text,
            candidate.fingerprint,
            candidate.contextBefore,
            candidate.contextAfter,
            owner_.timelineSecondForCursor(candidate.line, candidate.col),
        });
        usedFingerprints.insert(candidate.fingerprint);
        usedCommentLines.insert(candidate.line);
        mutated = true;
    }

    QSet<int> seenAutoBookmarkLines;
    for (int i = 0; i < state_.editorBookmarks_.size();) {
        const EditorBookmark& bookmark = state_.editorBookmarks_.at(i);
        if (bookmark.source == QLatin1String("comment")) {
            if (seenAutoBookmarkLines.contains(bookmark.line)) {
                state_.editorBookmarks_.removeAt(i);
                mutated = true;
                continue;
            }
            seenAutoBookmarkLines.insert(bookmark.line);
        }
        ++i;
    }

    if (mutated) {
        sortBookmarks(state_.editorBookmarks_);
        refreshEditorBookmarkLines();
        owner_.saveProjectRenderState();
        if (owner_.documentSection_ != nullptr) {
            owner_.documentSection_->rebuildFieldSidebar();
        }
    } else {
        refreshEditorBookmarkLines();
    }
    state_.editorBookmarkLastSyncedText_ = text;
}

void MainWindow::EditorSection::replaceBookmarkLine(int fromLine, int toLine)
{
    const int normalizedFrom = qMax(1, fromLine);
    const int normalizedTo = qMax(1, toLine);
    if (normalizedFrom == normalizedTo) {
        return;
    }
    bool moved = false;
    for (EditorBookmark& bookmark : state_.editorBookmarks_) {
        if (bookmark.line == normalizedFrom) {
            bookmark.line = normalizedTo;
            bookmark.source = QStringLiteral("manual");
            bookmark.second = owner_.timelineSecondForCursor(normalizedTo, 1);
            moved = true;
            break;
        }
    }
    if (!moved) {
        return;
    }
    sortBookmarks(state_.editorBookmarks_);
    refreshEditorBookmarkLines();
    owner_.saveProjectRenderState();
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->rebuildFieldSidebar();
    }
}

void MainWindow::EditorSection::addBookmark(int line, const QString& title, const QString& text)
{
    const int normalizedLine = qMax(1, line);
    state_.editorBookmarks_.append(EditorBookmark{
        title.trimmed().isEmpty()
            ? (UiText::isChineseUi() ? QStringLiteral("未命名书签") : QStringLiteral("Untitled Bookmark"))
            : title.trimmed(),
        text,
        normalizedLine,
        QStringLiteral("manual"),
        QString(),
        QString(),
        QString(),
        QString(),
        owner_.timelineSecondForCursor(normalizedLine, 1),
    });
    sortBookmarks(state_.editorBookmarks_);
    refreshEditorBookmarkLines();
    owner_.saveProjectRenderState();
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->rebuildFieldSidebar();
    }
}

void MainWindow::EditorSection::openBookmarkAtLine(int line)
{
    const auto it = std::find_if(state_.editorBookmarks_.begin(), state_.editorBookmarks_.end(), [line](const EditorBookmark& bookmark) {
        return bookmark.line == line;
    });
    if (it == state_.editorBookmarks_.end()) {
        owner_.jumpToLocation(line, 1);
        return;
    }

    const int bookmarkIndex = static_cast<int>(std::distance(state_.editorBookmarks_.begin(), it));
    owner_.jumpToLocation(it->line, 1);
    auto* dialog = new QDialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setObjectName(QStringLiteral("BookmarkDialog"));
    dialog->setWindowTitle(bookmarkTitleForDisplay(*it));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    UiDialogs::applyDetachedParentBehavior(dialog, UiDialogs::effectiveParentWidget(&owner_));
    dialog->resize(460, 260);
    dialog->setStyleSheet(bookmarkDialogStyleSheet());

    BookmarkDialogHeader* header = nullptr;
    auto* shell = makeBookmarkDialogShell(dialog, &header);
    header->setTitle(bookmarkTitleForDisplay(*it));
    wireBookmarkDialogWindowButtons(header, dialog);

    auto* shellLayout = new QVBoxLayout(dialog);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    shellLayout->addWidget(shell, 1);

    auto* shellVBox = qobject_cast<QVBoxLayout*>(shell->layout());
    auto* body = makeBookmarkDialogBody(shell);
    shellVBox->addWidget(body, 1);
    auto* layout = qobject_cast<QVBoxLayout*>(body->layout());

    auto* metaRow = new QWidget(body);
    auto* metaLayout = new QHBoxLayout(metaRow);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(8);
    auto* lineEdit = new QLineEdit(body);
    lineEdit->setValidator(new QIntValidator(1, 999999, lineEdit));
    lineEdit->setReadOnly(true);
    lineEdit->setText(QString::number(it->line));
    lineEdit->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("行号") : QStringLiteral("Line"));
    lineEdit->setFixedWidth(88);
    metaLayout->addWidget(lineEdit, 0);

    auto* titleEdit = new QLineEdit(body);
    titleEdit->setReadOnly(true);
    titleEdit->setText(it->title);
    metaLayout->addWidget(titleEdit, 1);
    layout->addWidget(metaRow, 0);

    auto* edit = new QTextEdit(body);
    edit->setReadOnly(true);
    edit->setPlainText(it->text);
    layout->addWidget(edit, 1);

    auto* buttonBox = new QDialogButtonBox(body);
    auto* editButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("修改") : QStringLiteral("Edit"),
        QDialogButtonBox::ActionRole);
    auto* deleteButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("删除") : QStringLiteral("Delete"),
        QDialogButtonBox::DestructiveRole);
    auto* saveButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("保存") : QStringLiteral("Save"),
        QDialogButtonBox::ActionRole);
    saveButton->setVisible(false);
    connect(editButton, &QPushButton::clicked, dialog, [lineEdit, titleEdit, edit, editButton, saveButton]() {
        lineEdit->setReadOnly(false);
        titleEdit->setReadOnly(false);
        edit->setReadOnly(false);
        lineEdit->setFocus();
        editButton->setVisible(false);
        saveButton->setVisible(true);
    });
    connect(saveButton, &QPushButton::clicked, dialog, [this, bookmarkIndex, dialog, lineEdit, titleEdit, edit, header]() {
        if (bookmarkIndex < 0 || bookmarkIndex >= state_.editorBookmarks_.size()) {
            return;
        }
        EditorBookmark& bookmark = state_.editorBookmarks_[bookmarkIndex];
        const int targetLine = qMax(1, lineEdit->text().trimmed().toInt());
        bookmark.title = titleEdit->text().trimmed().isEmpty()
            ? (UiText::isChineseUi() ? QStringLiteral("未命名书签") : QStringLiteral("Untitled Bookmark"))
            : titleEdit->text().trimmed();
        bookmark.text = edit->toPlainText();
        bookmark.line = targetLine;
        const QString updatedTitle = bookmarkTitleForDisplay(bookmark);
        sortBookmarks(state_.editorBookmarks_);
        refreshEditorBookmarkLines();
        owner_.saveProjectRenderState();
        header->setTitle(updatedTitle);
        dialog->setWindowTitle(updatedTitle);
        dialog->close();
    });
    connect(deleteButton, &QPushButton::clicked, dialog, [this, bookmarkIndex, dialog]() {
        if (bookmarkIndex < 0 || bookmarkIndex >= state_.editorBookmarks_.size()) {
            return;
        }
        if (!confirmDeleteBookmark(UiDialogs::effectiveParentWidget(&owner_), state_.editorBookmarks_.at(bookmarkIndex))) {
            return;
        }
        state_.editorBookmarks_.removeAt(bookmarkIndex);
        refreshEditorBookmarkLines();
        owner_.saveProjectRenderState();
        if (owner_.documentSection_ != nullptr) {
            owner_.documentSection_->rebuildFieldSidebar();
        }
        dialog->close();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    UiDialogs::localizeButtonBox(buttonBox);
    layout->addWidget(buttonBox, 0);
    dialog->show();
}

void MainWindow::EditorSection::deleteBookmarkAtLineWithConfirmation(int line)
{
    const auto it = std::find_if(state_.editorBookmarks_.begin(), state_.editorBookmarks_.end(), [line](const EditorBookmark& bookmark) {
        return bookmark.line == line;
    });
    if (it == state_.editorBookmarks_.end()) {
        return;
    }
    if (!confirmDeleteBookmark(UiDialogs::effectiveParentWidget(&owner_), *it)) {
        return;
    }
    state_.editorBookmarks_.erase(it);
    refreshEditorBookmarkLines();
    owner_.saveProjectRenderState();
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->rebuildFieldSidebar();
    }
}

void MainWindow::EditorSection::showCreateBookmarkDialog()
{
    int targetLine = 0;
    if (auto* sourceEditor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); sourceEditor != nullptr) {
        targetLine = qMax(1, sourceEditor->textCursor().blockNumber() + 1);
    }
    showCreateBookmarkDialogForLine(targetLine);
}

void MainWindow::EditorSection::showCreateBookmarkDialogForLine(int initialLine)
{
    auto* dialog = new QDialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setObjectName(QStringLiteral("BookmarkDialog"));
    dialog->setWindowTitle(UiText::isChineseUi() ? QStringLiteral("创建书签") : QStringLiteral("Create Bookmark"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    UiDialogs::applyDetachedParentBehavior(dialog, UiDialogs::effectiveParentWidget(&owner_));
    dialog->resize(520, 400);
    dialog->setStyleSheet(bookmarkDialogStyleSheet());

    BookmarkDialogHeader* header = nullptr;
    auto* shell = makeBookmarkDialogShell(dialog, &header);
    header->setTitle(UiText::isChineseUi() ? QStringLiteral("创建书签") : QStringLiteral("Create Bookmark"));
    wireBookmarkDialogWindowButtons(header, dialog);

    auto* shellLayout = new QVBoxLayout(dialog);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    shellLayout->addWidget(shell, 1);

    auto* shellVBox = qobject_cast<QVBoxLayout*>(shell->layout());
    auto* body = makeBookmarkDialogBody(shell);
    shellVBox->addWidget(body, 1);
    auto* layout = qobject_cast<QVBoxLayout*>(body->layout());

    auto* metaRow = new QWidget(body);
    auto* metaLayout = new QHBoxLayout(metaRow);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(8);
    auto* lineEdit = new QLineEdit(body);
    lineEdit->setValidator(new QIntValidator(1, 999999, lineEdit));
    lineEdit->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("行号") : QStringLiteral("Line"));
    if (initialLine > 0) {
        lineEdit->setText(QString::number(initialLine));
    }
    lineEdit->setFixedWidth(88);
    auto* lineEditedByUser = new bool(false);
    connect(lineEdit, &QLineEdit::textEdited, dialog, [lineEditedByUser](const QString&) {
        *lineEditedByUser = true;
    });
    connect(dialog, &QObject::destroyed, dialog, [lineEditedByUser]() {
        delete lineEditedByUser;
    });
    auto* titleEdit = new QLineEdit(body);
    titleEdit->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("书签名称") : QStringLiteral("Bookmark name"));
    metaLayout->addWidget(lineEdit, 0);
    metaLayout->addWidget(titleEdit, 1);
    layout->addWidget(metaRow, 0);

    auto* edit = new PlainCodeEditor(body);
    edit->setFont(editorFont(state_.editorTextFontPointSize_));
    edit->setBlockSpacingPixels(blockSpacingPixelsForPointSize(
        state_.editorTextFontPointSize_,
        state_.editorLineSpacingFactor_));
    edit->setHalfWidthInputEnabled(state_.editorHalfWidthInputEnabled_);
    edit->setImeInputDisabled(state_.editorImeInputDisabled_);
    edit->setEditorOverwriteMode(state_.editorOverwriteModeEnabled_);
    connect(edit, &PlainCodeEditor::editorOverwriteModeChanged, &owner_, [this](bool enabled) {
        applyEditorOverwriteModeEnabled(enabled, true);
    });
    edit->setAutoCompletionEnabled(state_.editorAutoCompletionEnabled_);
    edit->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("书签内容") : QStringLiteral("Bookmark notes"));
    edit->setStyleSheet(UiTheme::editorTextEditStyleSheet());
    if (auto* vbar = edit->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    if (auto* hbar = edit->horizontalScrollBar()) {
        hbar->setStyleSheet(modernScrollBarStyle());
    }
    layout->addWidget(edit, 1);

    auto* buttonBox = new QDialogButtonBox(body);
    auto* createButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("创建") : QStringLiteral("Create"),
        QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    connect(createButton, &QPushButton::clicked, dialog, [this, dialog, lineEdit, titleEdit, edit]() {
        int targetLine = lineEdit->text().trimmed().toInt();
        if (targetLine <= 0) {
            if (auto* sourceEditor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); sourceEditor != nullptr) {
                targetLine = qMax(1, sourceEditor->textCursor().blockNumber() + 1);
            }
        }
        targetLine = qMax(1, targetLine);
        addBookmark(targetLine, titleEdit->text(), edit->toPlainText());
        dialog->close();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    UiDialogs::localizeButtonBox(buttonBox);
    layout->addWidget(buttonBox, 0);

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor != nullptr) {
        QPointer<QDialog> dialogPtr(dialog);
        const auto createBookmarkOnLine = [this, dialogPtr, lineEdit, lineEditedByUser, titleEdit, edit, header](int line) {
            if (dialogPtr.isNull()) {
                return;
            }
            const int typedLine = lineEdit->text().trimmed().toInt();
            addBookmark(*lineEditedByUser && typedLine > 0 ? typedLine : line, titleEdit->text(), edit->toPlainText());
            header->setTitle(titleEdit->text().trimmed().isEmpty()
                ? (UiText::isChineseUi() ? QStringLiteral("创建书签") : QStringLiteral("Create Bookmark"))
                : titleEdit->text().trimmed());
            dialogPtr->close();
        };
        header->setMoveCallback([editor](const QPoint& globalPos, bool wasDragged) {
            if (editor == nullptr) {
                return;
            }
            editor->setBookmarkDropPreviewLine(wasDragged ? editor->lineNumberAtGlobalPosition(globalPos) : -1);
        });
        header->setReleaseCallback([editor, createBookmarkOnLine](const QPoint& globalPos, bool wasDragged) {
            if (editor == nullptr) {
                return;
            }
            editor->setBookmarkDropPreviewLine(-1);
            if (!wasDragged) {
                return;
            }
            const int line = editor->lineNumberAtGlobalPosition(globalPos);
            if (line > 0) {
                createBookmarkOnLine(line);
            }
        });
        connect(dialog, &QObject::destroyed, editor, [editor]() {
            editor->setBookmarkDropPreviewLine(-1);
        });
    }
    dialog->show();
}

void MainWindow::EditorSection::showBookmarkManager()
{
    auto* menu = new QMenu(UiDialogs::effectiveParentWidget(&owner_));
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu->setObjectName(QStringLiteral("BookmarkDialog"));
    menu->setStyleSheet(bookmarkDialogStyleSheet());

    QVector<EditorBookmark> sorted = state_.editorBookmarks_;
    sortBookmarks(sorted);

    if (sorted.isEmpty()) {
        QAction* emptyAction = menu->addAction(UiText::isChineseUi() ? QStringLiteral("暂无书签") : QStringLiteral("No bookmarks"));
        emptyAction->setEnabled(false);
    } else {
        BookmarkDialogHeader* header = nullptr;
        auto* shell = makeBookmarkDialogShell(menu, &header);
        header->setTitle(UiText::isChineseUi() ? QStringLiteral("书签管理") : QStringLiteral("Bookmark Manager"));
        if (header->minimizeButton() != nullptr) {
            header->minimizeButton()->setVisible(false);
        }
        QObject::connect(header->closeButton(), &QToolButton::clicked, menu, &QMenu::close);
        auto* shellLayout = new QVBoxLayout(menu);
        shellLayout->setContentsMargins(0, 0, 0, 0);
        shellLayout->setSpacing(0);
        shellLayout->addWidget(shell, 1);

        auto* shellVBox = qobject_cast<QVBoxLayout*>(shell->layout());
        auto* body = makeBookmarkDialogListArea(shell);
        shellVBox->addWidget(body, 1);
        auto* bodyLayout = qobject_cast<QVBoxLayout*>(body->layout());

        auto* list = new QListWidget(body);
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollBarPolicy(sorted.size() > kBookmarkManagerVisibleRows ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setUniformItemSizes(true);
        list->setStyleSheet(UiTheme::outlineListStyleSheet());
        if (auto* vbar = list->verticalScrollBar()) {
            vbar->setStyleSheet(modernScrollBarStyle());
        }
        for (const EditorBookmark& bookmark : std::as_const(sorted)) {
            auto* item = new QListWidgetItem(bookmarkTitleForDisplay(bookmark), list);
            item->setData(Qt::UserRole, bookmark.line);
        }
        const int visibleRows = qMin(kBookmarkManagerVisibleRows, sorted.size());
        list->setFixedWidth(240);
        list->setFixedHeight(qMax(1, visibleRows) * 34 + 8);
        connect(list, &QListWidget::itemClicked, menu, [this, menu](QListWidgetItem* item) {
            if (item == nullptr) {
                return;
            }
            const int line = item->data(Qt::UserRole).toInt();
            menu->close();
            openBookmarkAtLine(line);
        });

        auto* widgetAction = new QWidgetAction(menu);
        widgetAction->setDefaultWidget(list);
        bodyLayout->addWidget(list, 1);
        menu->addAction(widgetAction);
    }

    QPoint anchor = QCursor::pos();
    if (ui_.toolboxMenu_ != nullptr) {
        anchor = ui_.toolboxMenu_->parentWidget() != nullptr
            ? ui_.toolboxMenu_->parentWidget()->mapToGlobal(QPoint(24, 24))
            : QCursor::pos();
    }
    menu->popup(anchor);
}

void MainWindow::EditorSection::exportBookmarksJson()
{
    const QString defaultDir = state_.currentFilePath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(state_.currentFilePath_).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        UiDialogs::effectiveParentWidget(&owner_),
        UiText::isChineseUi() ? QStringLiteral("导出书签") : QStringLiteral("Export Bookmarks"),
        QDir(defaultDir).filePath(QStringLiteral("miacode_bookmarks.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("miacode_bookmarks_v1"));
    QJsonArray items;
    for (const EditorBookmark& bookmark : std::as_const(state_.editorBookmarks_)) {
        items.append(bookmarkToJsonObject(bookmark));
    }
    root.insert(QStringLiteral("bookmarks"), items);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            UiText::isChineseUi() ? QStringLiteral("导出失败") : QStringLiteral("Export Failed"),
            UiText::isChineseUi() ? QStringLiteral("无法写入文件：\n%1").arg(path) : QStringLiteral("Cannot write file:\n%1").arg(path));
        return;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            UiText::isChineseUi() ? QStringLiteral("导出失败") : QStringLiteral("Export Failed"),
            UiText::isChineseUi() ? QStringLiteral("写入失败：\n%1").arg(path) : QStringLiteral("Write failed:\n%1").arg(path));
    }
}

void MainWindow::EditorSection::importBookmarksJson()
{
    const QString defaultDir = state_.currentFilePath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(state_.currentFilePath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        UiDialogs::effectiveParentWidget(&owner_),
        UiText::isChineseUi() ? QStringLiteral("导入书签") : QStringLiteral("Import Bookmarks"),
        defaultDir,
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            UiText::isChineseUi() ? QStringLiteral("导入失败") : QStringLiteral("Import Failed"),
            UiText::isChineseUi() ? QStringLiteral("无法打开文件：\n%1").arg(path) : QStringLiteral("Cannot open file:\n%1").arg(path));
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            UiText::isChineseUi() ? QStringLiteral("导入失败") : QStringLiteral("Import Failed"),
            UiText::isChineseUi() ? QStringLiteral("书签 JSON 格式无效。") : QStringLiteral("Invalid bookmark JSON."));
        return;
    }

    const QJsonArray items = doc.object().value(QStringLiteral("bookmarks")).toArray();
    int imported = 0;
    for (const QJsonValue& value : items) {
        if (!value.isObject()) {
            continue;
        }
        EditorBookmark bookmark = bookmarkFromJsonObject(value.toObject());
        const auto existing = std::find_if(
            state_.editorBookmarks_.cbegin(),
            state_.editorBookmarks_.cend(),
            [&bookmark](const EditorBookmark& current) {
                return bookmarksReferToSameImportIdentity(current, bookmark);
            });
        if (existing != state_.editorBookmarks_.cend()) {
            continue;
        }
        state_.editorBookmarks_.append(bookmark);
        ++imported;
    }
    if (imported > 0) {
        sortBookmarks(state_.editorBookmarks_);
        refreshEditorBookmarkLines();
        owner_.saveProjectRenderState();
        if (owner_.documentSection_ != nullptr) {
            owner_.documentSection_->rebuildFieldSidebar();
        }
    }
    UiDialogs::showMessageBox(
        QMessageBox::Information,
        &owner_,
        UiText::isChineseUi() ? QStringLiteral("导入书签") : QStringLiteral("Import Bookmarks"),
        UiText::isChineseUi()
            ? QStringLiteral("已导入 %1 个书签。").arg(imported)
            : QStringLiteral("Imported %1 bookmark(s).").arg(imported));
}

void MainWindow::EditorSection::setFullCopyAreaVisible(bool visible)
{
    if (ui_.copyAreaPanel_ == nullptr || ui_.fullCopyAreaAction_ == nullptr) {
        return;
    }
    ui_.copyAreaPanel_->setVisible(visible);
    ui_.fullCopyAreaAction_->setChecked(visible);
    if (visible) {
        syncCopyAreaEditorAppearance();
        syncCopyAreaLineCount();
        if (ui_.chartCopySplitter_ != nullptr) {
            ui_.chartCopySplitter_->setSizes({1, 1});
        }
        if (ui_.copyAreaEditor_ != nullptr) {
            ui_.copyAreaEditor_->setFocus();
        }
    } else if (ui_.editorWidget_ != nullptr) {
        ui_.editorWidget_->setFocus();
    }
}

void MainWindow::EditorSection::syncCopyAreaEditorAppearance()
{
    if (ui_.copyAreaEditor_ == nullptr) {
        return;
    }
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(
        state_.editorTextFontPointSize_,
        state_.editorLineSpacingFactor_);
    QSignalBlocker blocker(ui_.copyAreaEditor_);
    ui_.copyAreaEditor_->setFont(editorFont(state_.editorTextFontPointSize_));
    ui_.copyAreaEditor_->setBlockSpacingPixels(blockSpacingPixels);
    ui_.copyAreaEditor_->setHalfWidthInputEnabled(state_.editorHalfWidthInputEnabled_);
    ui_.copyAreaEditor_->setImeInputDisabled(state_.editorImeInputDisabled_);
    ui_.copyAreaEditor_->setEditorOverwriteMode(state_.editorOverwriteModeEnabled_);
    ui_.copyAreaEditor_->setAutoCompletionEnabled(state_.editorAutoCompletionEnabled_);
    ui_.copyAreaEditor_->refreshLineNumberAreaLayout();
}

void MainWindow::EditorSection::syncCopyAreaLineCount()
{
    if (ui_.copyAreaEditor_ == nullptr) {
        return;
    }
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr || ui_.copyAreaEditor_->document() == nullptr) {
        return;
    }
    const int sourceLines = qMax(1, editor->document()->blockCount());
    const int copyLines = qMax(1, ui_.copyAreaEditor_->document()->blockCount());
    if (copyLines >= sourceLines) {
        return;
    }
    QTextCursor cursor(ui_.copyAreaEditor_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    for (int i = copyLines; i < sourceLines; ++i) {
        cursor.insertBlock();
    }
    cursor.endEditBlock();
}

void MainWindow::loadPortableState()
{
    editorSection_->loadPortableState();
}

void MainWindow::resetPortablePreviewSettingsToDefaults()
{
    editorSection_->resetPortablePreviewSettingsToDefaults();
}

void MainWindow::applyPortablePreviewSettings(const QJsonObject& preview)
{
    editorSection_->applyPortablePreviewSettings(preview);
}

void MainWindow::savePortableState() const
{
    editorSection_->savePortableState();
}

void MainWindow::persistEditorTextFontPreference() const
{
    editorSection_->persistEditorTextFontPreference();
}

void MainWindow::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    editorSection_->applyEditorTextFontSize(pointSize, persistPreference);
}

void MainWindow::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    editorSection_->applyEditorLineSpacingFactor(factor, persistPreference);
}

void MainWindow::applyEditorHalfWidthInputEnabled(bool enabled, bool persistPreference)
{
    editorSection_->applyEditorHalfWidthInputEnabled(enabled, persistPreference);
}

void MainWindow::applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference)
{
    editorSection_->applyEditorOverwriteModeEnabled(enabled, persistPreference);
}

void MainWindow::applyEditorAutoCompletionEnabled(bool enabled, bool persistPreference)
{
    editorSection_->applyEditorAutoCompletionEnabled(enabled, persistPreference);
}

void MainWindow::applyEditorImeInputDisabled(bool disabled, bool persistPreference)
{
    editorSection_->applyEditorImeInputDisabled(disabled, persistPreference);
}

void MainWindow::applyEditorHeaderTopDisplay(EditorHeaderTopDisplay mode, bool persistPreference)
{
    editorSection_->applyEditorHeaderTopDisplay(mode, persistPreference);
}

void MainWindow::showCreateBookmarkDialog()
{
    editorSection_->showCreateBookmarkDialog();
}

void MainWindow::showBookmarkManager()
{
    editorSection_->showBookmarkManager();
}

void MainWindow::openBookmarkAtLine(int line)
{
    editorSection_->openBookmarkAtLine(line);
}

void MainWindow::setFullCopyAreaVisible(bool visible)
{
    editorSection_->setFullCopyAreaVisible(visible);
}

void MainWindow::syncCopyAreaEditorAppearance()
{
    editorSection_->syncCopyAreaEditorAppearance();
}

void MainWindow::syncCopyAreaLineCount()
{
    editorSection_->syncCopyAreaLineCount();
}
