#include "runtime/editor/EditorHost.h"
#include "runtime/settings/SettingsHost.h"
#include "runtime/playback/PlaybackHost.h"
#include "runtime/Shared.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "editor/BookmarkCommentSyntax.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
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

using namespace miacode::runtime::shared;

namespace {
// Derived bookmark names come from the line's first non-control `||` comment:
// an explicit `[label]` prefix wins, otherwise the first short token is used.
constexpr int kBookmarkAutoNameTokenMaxChars = 16;
constexpr int kBookmarkAutoNameMaxChars = 20;
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

struct BookmarkLabelParts {
    QString normalizedComment;
    QString explicitLabel;
    QString bodyAfterExplicitLabel;
    int labelStartInRawComment = -1;
    int labelEndInRawComment = -1;
    int afterLabelInRawComment = -1;
    bool hasExplicitLabel = false;
};

BookmarkLabelParts parseBookmarkLabelParts(const QString& rawComment)
{
    BookmarkLabelParts parts;
    parts.normalizedComment = normalizedBookmarkCommentText(rawComment);
    int firstContent = 0;
    while (firstContent < rawComment.size() && rawComment.at(firstContent).isSpace()) {
        ++firstContent;
    }
    if (firstContent < rawComment.size() && rawComment.at(firstContent) == QLatin1Char('[')) {
        const int close = rawComment.indexOf(QLatin1Char(']'), firstContent + 1);
        if (close > firstContent + 1) {
            const QString label = rawComment.mid(firstContent + 1, close - firstContent - 1).trimmed();
            if (!label.isEmpty()) {
                parts.hasExplicitLabel = true;
                parts.explicitLabel = label;
                parts.labelStartInRawComment = firstContent + 1;
                parts.labelEndInRawComment = close;
                parts.afterLabelInRawComment = close + 1;
                while (parts.afterLabelInRawComment < rawComment.size()
                       && rawComment.at(parts.afterLabelInRawComment).isSpace()) {
                    ++parts.afterLabelInRawComment;
                }
                parts.bodyAfterExplicitLabel =
                    normalizedBookmarkCommentText(rawComment.mid(parts.afterLabelInRawComment));
            }
        }
    }
    return parts;
}

// Comment → default name: take the first delimiter-separated token (space,
// half/full-width commas, 顿号, semicolons, colons, slashes), truncated when
// overlong; a comment with no usable token falls back to a raw prefix. The
// original language is kept verbatim; empty for control comments.
QString defaultBookmarkNameFromComment(const QString& text)
{
    const QString normalized = normalizedBookmarkCommentText(text);
    if (normalized.isEmpty() || isControlBookmarkComment(normalized)) {
        return QString();
    }
    static const QRegularExpression separatorRe(
        QStringLiteral("[\\s,\\x{FF0C}\\x{3001};\\x{FF1B}:\\x{FF1A}/\\x{FF0F}]+"));
    const QStringList tokens = normalized.split(separatorRe, Qt::SkipEmptyParts);
    if (!tokens.isEmpty() && !tokens.first().isEmpty()) {
        const QString& token = tokens.first();
        return token.size() > kBookmarkAutoNameTokenMaxChars
            ? token.left(kBookmarkAutoNameTokenMaxChars)
            : token;
    }
    return normalized.left(kBookmarkAutoNameMaxChars);
}

// Line-number fallback used when a bookmark is created on a line without a
// usable comment: "第 8 行" / "L8".
QString fallbackBookmarkNameForLine(int line)
{
    return UiText::text(QStringLiteral("editor.l_1")).arg(qMax(1, line));
}

QString defaultExplicitBookmarkLabel()
{
    return UiText::text(QStringLiteral("editor.new_bookmark"));
}

struct BookmarkCommentCandidate {
    int line = 1;
    int col = 1;
    int position = 0;
    int markerInLine = -1;
    QString text;
    QString title;
    QString fingerprint;
    QString contextBefore;
    QString contextAfter;
    bool hasExplicitLabel = false;
};

QVector<BookmarkCommentCandidate> collectBookmarkCommentCandidates(const QString& text)
{
    QVector<BookmarkCommentCandidate> candidates;
    const QStringList lines = text.split(QLatin1Char('\n'));
    int position = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& lineText = lines.at(i);
        const int marker = lineText.indexOf(QStringLiteral("||"));
        if (miacode::editor::isBookmarkCommentMarker(lineText, marker)) {
            const QString rawComment = lineText.mid(marker + 2);
            const BookmarkLabelParts labelParts = parseBookmarkLabelParts(rawComment);
            const QString commentText = labelParts.normalizedComment;
            if (!isControlBookmarkComment(commentText)) {
                BookmarkCommentCandidate candidate;
                candidate.line = i + 1;
                candidate.col = marker + 1;
                candidate.position = position + marker;
                candidate.markerInLine = marker;
                candidate.text = commentText;
                candidate.title = labelParts.hasExplicitLabel
                    ? labelParts.explicitLabel
                    : defaultBookmarkNameFromComment(commentText);
                if (candidate.title.isEmpty()) {
                    candidate.title = fallbackBookmarkNameForLine(candidate.line);
                }
                candidate.fingerprint = bookmarkFingerprintForText(commentText);
                const int beforeStart = qMax(0, candidate.position - kBookmarkContextChars);
                candidate.contextBefore = text.mid(beforeStart, candidate.position - beforeStart);
                const int afterStart = qMin(text.size(), candidate.position + 2 + commentText.size());
                candidate.contextAfter = text.mid(afterStart, qMin(kBookmarkContextChars, text.size() - afterStart));
                candidate.hasExplicitLabel = labelParts.hasExplicitLabel;
                candidates.append(candidate);
            }
        }
        position += lineText.size() + 1;
    }
    return candidates;
}


template <typename Bookmark>
void sortBookmarks(QVector<Bookmark>& bookmarks)
{
    std::sort(bookmarks.begin(), bookmarks.end(), [](const Bookmark& left, const Bookmark& right) {
        if (left.difficultyId != right.difficultyId) {
            return left.difficultyId < right.difficultyId;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        return left.title.localeAwareCompare(right.title) < 0;
    });
}

bool editorBookmarkListsEqual(const QVector<Session::EditorBookmark>& left,
                              const QVector<Session::EditorBookmark>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int i = 0; i < left.size(); ++i) {
        const Session::EditorBookmark& a = left.at(i);
        const Session::EditorBookmark& b = right.at(i);
        if (a.difficultyId != b.difficultyId
            || a.line != b.line
            || a.title != b.title
            || a.commentText != b.commentText
            || a.commentFingerprint != b.commentFingerprint) {
            return false;
        }
    }
    return true;
}

QVector<Session::EditorBookmark> derivedBookmarksForChart(
    const QString& chartText,
    int difficultyId)
{
    QVector<Session::EditorBookmark> bookmarks;
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return bookmarks;
    }
    const QVector<BookmarkCommentCandidate> candidates = collectBookmarkCommentCandidates(chartText);
    bookmarks.reserve(candidates.size());
    for (const BookmarkCommentCandidate& candidate : candidates) {
        bookmarks.append(Session::EditorBookmark{
            candidate.title,
            QString(),
            candidate.line,
            QStringLiteral("comment"),
            candidate.text,
            candidate.fingerprint,
            candidate.contextBefore,
            candidate.contextAfter,
            difficultyId,
            candidate.hasExplicitLabel,
        });
    }
    return bookmarks;
}

QTextBlock blockForOneBasedLine(QTextDocument* document, int line)
{
    if (document == nullptr || line <= 0) {
        return QTextBlock();
    }
    return document->findBlockByNumber(line - 1);
}

struct LineBookmarkCommentInfo {
    int marker = -1;
    int rawCommentStart = -1;
    int labelStart = -1;
    int labelEnd = -1;
    int afterLabel = -1;
    bool hasMarker = false;
    bool isBookmark = false;
    bool hasExplicitLabel = false;
    QString normalizedComment;
};

LineBookmarkCommentInfo inspectLineBookmarkComment(const QString& lineText)
{
    LineBookmarkCommentInfo info;
    info.marker = lineText.indexOf(QStringLiteral("||"));
    info.hasMarker = miacode::editor::isBookmarkCommentMarker(lineText, info.marker);
    if (!info.hasMarker) {
        return info;
    }
    info.rawCommentStart = info.marker + 2;
    const QString rawComment = lineText.mid(info.rawCommentStart);
    const BookmarkLabelParts labelParts = parseBookmarkLabelParts(rawComment);
    info.normalizedComment = labelParts.normalizedComment;
    info.isBookmark = !isControlBookmarkComment(info.normalizedComment);
    info.hasExplicitLabel = labelParts.hasExplicitLabel;
    if (labelParts.hasExplicitLabel) {
        info.labelStart = info.rawCommentStart + labelParts.labelStartInRawComment;
        info.labelEnd = info.rawCommentStart + labelParts.labelEndInRawComment;
        info.afterLabel = info.rawCommentStart + labelParts.afterLabelInRawComment;
    }
    return info;
}

bool lineIsStandaloneComment(const QString& lineText, const LineBookmarkCommentInfo& info)
{
    return info.isBookmark && lineText.left(info.marker).trimmed().isEmpty();
}
}  // namespace

miacode::runtime::EditorHost::EditorHost(
    Session& session,
    Session::HostUi& ui,
    Session::HostState& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
    , previewAppearanceValues_(session.previewAppearanceValues_)
{}

void miacode::runtime::EditorHost::loadPortableState()
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
    bool importedLegacyBottomPanelHeight = false;
    int legacyBottomPanelHeight = 0;
    QSettings legacySettings;
    const bool hasLegacyBottomPanelHeight =
        legacySettings.contains(QStringLiteral("ui/bottomPanelHeight"));
    if (ui.value("bottom_tabs_content_scale").isDouble()) {
        // Stored as a content-scale ratio (not pixels). The raw value is clamped
        // to its valid [min,max] range by applyBottomTabsContentScale() when the
        // height is re-applied below.
        state_.bottomTabsContentScale_ =
            ui.value("bottom_tabs_content_scale").toDouble(state_.bottomTabsContentScale_);
    } else {
        // QML v2 previously kept a competing pixel height in QSettings. Import
        // it once only when the canonical shell scale has not been saved, then
        // remove the legacy value so subsequent launches have one owner.
        if (hasLegacyBottomPanelHeight) {
            legacyBottomPanelHeight = qBound(
                120,
                legacySettings.value(QStringLiteral("ui/bottomPanelHeight")).toInt(),
                340);
            importedLegacyBottomPanelHeight = true;
        }
    }
    // The shell's scale is canonical even when it already existed before this
    // launch, so always retire the obsolete v2 pixel-height key.
    if (hasLegacyBottomPanelHeight) {
        legacySettings.remove(QStringLiteral("ui/bottomPanelHeight"));
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
        if (preview.value("timeline_measure_line_brightness").isDouble()) {
            state_.timelineQuickStateBridge_->setMeasureLineBrightness(
                preview.value("timeline_measure_line_brightness").toDouble(
                    miacode::timeline::kTimelineMeasureLineBrightnessDefault)
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
    if (session_.shell_ != nullptr) {
        if (importedLegacyBottomPanelHeight) {
            session_.shell_->setBottomTabsHeight(legacyBottomPanelHeight);
        }
        session_.shell_->updateBottomTabsDeviceHeight();
    }
    session_.refreshPreviewFrameRateTimers();
    session_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "editor_display_apply_settings");
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
    }
}

void miacode::runtime::EditorHost::resetPortablePreviewSettingsToDefaults()
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
    previewAppearanceValues_.outlineVariant = PreviewOutlineVariant::Line;
    state_.previewOutlineVariantUsesAutoSelection_ = true;
    state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    state_.previewTapFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    state_.previewTouchFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    previewAppearanceValues_.slideEarlierSecondAndTextOnTop = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    previewAppearanceValues_.tapJudgeTextDistance = PreviewTapJudgeTextDistance::Inner;
    previewAppearanceValues_.skinVariant = PreviewSkinVariant::Standard;
    state_.previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    state_.previewStageMediaFrameRateMode_ = PreviewCanvasFrameRateMode::Fps30;
    state_.videoDecodePrefersSoftware_ = false;
    state_.timelineFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    state_.previewCanvasAspectRatio_ = 1.0;
    state_.previewAutoRestoreSquareAfterExport_ = false;
    state_.previewForceLabeledJudgeLineWhenPaused_ = true;
    state_.previewTouchPadAuthoringShortcutEnabled_ = true;
    state_.previewShowDebugInfo_ = false;
    state_.previewShowTimestamp_ = true;
    state_.previewShowObjectStatsHud_ = false;
    state_.exportShowObjectStatsHud_ = false;
    state_.previewShowChartInfoHud_ = false;
    state_.exportShowChartInfoHud_ = false;
    state_.previewShowValidationSummary_ = true;
    state_.chartNormalizeStartAtNewMeasure_ = true;
    state_.chartNormalizeReduceTo384Grid_ = false;
    state_.chartNormalizeSplitEveryFourMeasures_ = true;
    state_.workspacePanelsSwapped_ = false;
}

void miacode::runtime::EditorHost::applyPortablePreviewSettings(const QJsonObject& preview)
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
    state_.muriRenderOptions_.renderMode = muriRenderModeFromToken(muriRenderMode);
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
    if (preview.value("show_chart_review_break_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewBreakJudgeOverlay =
            preview.value("show_chart_review_break_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewBreakJudgeOverlay);
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
            previewAppearanceValues_.outlineVariant = session_.previewOutlineVariantFromStorageValue(outlineVariant);
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
    } else if (scaleMode == QLatin1String("inner_circle_fit_outer_fill")
        || scaleMode == QLatin1String("inner-circle-fit-outer-fill")
        || scaleMode == QLatin1String("inner_fit_outer_fill")
        || scaleMode == QLatin1String("inner-fit-outer-fill")
        || scaleMode == QLatin1String("circle_fit_outer_fill")
        || scaleMode == QLatin1String("circle-fit-outer-fill")
        || scaleMode == QLatin1String("inner_circle_fit")
        || scaleMode == QLatin1String("inner-circle-fit")) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::InnerCircleFitOuterFill;
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
        previewAppearanceValues_.slideEarlierSecondAndTextOnTop =
            preview.value("slide_earlier_second_and_text_on_top")
                .toBool(previewAppearanceValues_.slideEarlierSecondAndTextOnTop);
    }
    previewAppearanceValues_.tapJudgeTextDistance = tapJudgeTextDistanceFromToken(
        preview.value(QStringLiteral("tap_judge_text_distance")).toString(
            QString::fromLatin1(tapJudgeTextDistanceToken(previewAppearanceValues_.tapJudgeTextDistance))));
    previewAppearanceValues_.judgeEffectStyle = judgeEffectStyleFromToken(
        preview.value(QStringLiteral("judge_effect_style")).toString(
            QString::fromLatin1(judgeEffectStyleToken(previewAppearanceValues_.judgeEffectStyle))));
    if (preview.value("skin_variant").isString()) {
        const QString skinValue = preview.value("skin_variant").toString().trimmed();
        const QString normalizedSkinValue = skinValue.toLower();
        previewAppearanceValues_.skinVariant = session_.previewSkinVariantFromStorageValue(skinValue);
        previewAppearanceValues_.skinDirectoryName =
            previewAppearanceValues_.skinVariant == PreviewSkinVariant::Dx ? QStringLiteral("skinDX") : QStringLiteral("skinSD");
        if (!skinValue.isEmpty()
            && normalizedSkinValue != QLatin1String("standard")
            && normalizedSkinValue != QLatin1String("std")
            && normalizedSkinValue != QLatin1String("skin")
            && normalizedSkinValue != QLatin1String("skinsd")
            && normalizedSkinValue != QLatin1String("skinstd")
            && normalizedSkinValue != QLatin1String("dx")
            && normalizedSkinValue != QLatin1String("skin_dx")
            && normalizedSkinValue != QLatin1String("skindx")) {
            previewAppearanceValues_.skinDirectoryName = skinValue;
        }
    }
    if (preview.value("intro_sound_file").isString()) {
        previewAppearanceValues_.introSoundFileName =
            miacode::preview_sfx::normalizeIntroSoundFileName(preview.value("intro_sound_file").toString());
    }
    miacode::preview_sfx::setSelectedIntroSoundFileName(previewAppearanceValues_.introSoundFileName);
    if (preview.value("canvas_frame_rate_mode").isString()) {
        state_.previewCanvasFrameRateMode_ =
            session_.previewCanvasFrameRateModeFromStorageValue(preview.value("canvas_frame_rate_mode").toString());
    }
    if (preview.value("pv_frame_rate_mode").isString()) {
        state_.previewStageMediaFrameRateMode_ = session_.previewFrameRateModeFromStorageValue(
            preview.value("pv_frame_rate_mode").toString(),
            PreviewCanvasFrameRateMode::Fps30);
    }
    if (preview.value("video_decode_prefers_software").isBool()) {
        state_.videoDecodePrefersSoftware_ =
            preview.value("video_decode_prefers_software").toBool(state_.videoDecodePrefersSoftware_);
    }
    if (preview.value("timeline_frame_rate_mode").isString()) {
        state_.timelineFrameRateMode_ = session_.previewFrameRateModeFromStorageValue(
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
    if (preview.value("touch_pad_authoring_shortcut_enabled").isBool()) {
        state_.previewTouchPadAuthoringShortcutEnabled_ =
            preview.value("touch_pad_authoring_shortcut_enabled")
                .toBool(state_.previewTouchPadAuthoringShortcutEnabled_);
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
    previewAppearanceValues_.centerDisplayMode = miacode::preview_gameplay::centerDisplayModeFromToken(
        preview.value(QStringLiteral("center_display_mode")).toString(
            QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(previewAppearanceValues_.centerDisplayMode))));
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
            miacode::chart_transform::ChartNormalizationOptions{true, false, true});
    state_.chartNormalizeStartAtNewMeasure_ = true;
    state_.chartNormalizeReduceTo384Grid_ = normalizationOptions.reduceTo384Grid;
    state_.chartNormalizeSplitEveryFourMeasures_ = normalizationOptions.splitEveryFourMeasures;
    state_.chartNormalizeSyntax_ = normalizationOptions.syntax;
    state_.chartNormalizeSectionMeasureCount_ = normalizationOptions.sectionMeasureCount;
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
    state_.breakSlideTailCheerMutedPreference_ = resolveBreakSlideTailCheerMutedPreference(preview);
    state_.softwarePreviewAudioSettings_ = previewAudioSettingsWithBreakSlideTailCheerPreference(
        state_.softwarePreviewAudioSettings_, state_.breakSlideTailCheerMutedPreference_);
    state_.previewAudioSettings_ = state_.softwarePreviewAudioSettings_;
    state_.softwarePreviewTimingSettings_ = PreviewTimingSettings::fromJson(preview.value("timing").toObject());
    state_.softwarePreviewTimingSettings_.normalize();
    state_.previewTimingSettings_ = state_.softwarePreviewTimingSettings_;
}

void miacode::runtime::EditorHost::savePortableState() const
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
    preview.insert("muri_render_mode", muriRenderModeToken(state_.muriRenderOptions_.renderMode));
    preview.insert("show_chart_review_slide_judge_overlay", state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    preview.insert("show_chart_review_tap_judge_overlay", state_.muriRenderOptions_.showChartReviewTapJudgeOverlay);
    preview.insert("show_chart_review_break_judge_overlay", state_.muriRenderOptions_.showChartReviewBreakJudgeOverlay);
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
        preview.insert("outline_variant", session_.previewOutlineVariantStorageValue());
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
                   : (state_.previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::InnerCircleFitOuterFill
                          ? QStringLiteral("inner_circle_fit_outer_fill")
                          : QStringLiteral("fill")))
    );
    preview.insert("tap_flow_speed", state_.previewTapFlowSpeed_);
    preview.insert("touch_flow_speed", state_.previewTouchFlowSpeed_);
    preview.insert("slide_earlier_second_and_text_on_top", previewAppearanceValues_.slideEarlierSecondAndTextOnTop);
    preview.insert(
        "tap_judge_text_distance",
        QString::fromLatin1(tapJudgeTextDistanceToken(previewAppearanceValues_.tapJudgeTextDistance))
    );
    preview.insert(
        "judge_effect_style",
        QString::fromLatin1(judgeEffectStyleToken(previewAppearanceValues_.judgeEffectStyle))
    );
    preview.insert("skin_variant", session_.previewSkinVariantStorageValue());
    if (previewAppearanceValues_.introSoundFileName.trimmed().isEmpty()) {
        preview.remove("intro_sound_file");
    } else {
        preview.insert("intro_sound_file", previewAppearanceValues_.introSoundFileName);
    }
    preview.insert("canvas_frame_rate_mode", session_.previewCanvasFrameRateModeStorageValue());
    preview.insert("pv_frame_rate_mode", session_.previewStageMediaFrameRateModeStorageValue());
    preview.insert("video_decode_prefers_software", session_.currentVideoDecodePrefersSoftware());
    preview.insert("timeline_frame_rate_mode", session_.timelineFrameRateModeStorageValue());
    preview.insert(
        "force_labeled_judge_line_when_paused",
        state_.previewForceLabeledJudgeLineWhenPaused_
    );
    preview.insert(
        "touch_pad_authoring_shortcut_enabled",
        state_.previewTouchPadAuthoringShortcutEnabled_
    );
    preview.insert("show_debug_info", state_.previewShowDebugInfo_);
    preview.insert("show_timestamp", state_.previewShowTimestamp_);
    preview.insert("show_object_stats_preview", state_.previewShowObjectStatsHud_);
    preview.insert("show_object_stats_export", state_.exportShowObjectStatsHud_);
    preview.insert("show_chart_info_preview", state_.previewShowChartInfoHud_);
    preview.insert("show_chart_info_export", state_.exportShowChartInfoHud_);
    preview.insert(
        "center_display_mode",
        QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(previewAppearanceValues_.centerDisplayMode))
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
    preview.insert(
        "timeline_measure_line_brightness",
        state_.timelineQuickStateBridge_ != nullptr
            ? state_.timelineQuickStateBridge_->measureLineBrightness()
            : miacode::timeline::kTimelineMeasureLineBrightnessDefault
    );
    miacode::chart_transform::saveChartNormalizationOptionsToPreferences(
        &preview,
        miacode::chart_transform::ChartNormalizationOptions{
            true,
            state_.chartNormalizeReduceTo384Grid_,
            state_.chartNormalizeSplitEveryFourMeasures_,
            state_.chartNormalizeSyntax_,
            state_.chartNormalizeSectionMeasureCount_});
    preview.insert("swap_side_panels", state_.workspacePanelsSwapped_);
    preview.insert("canvas_aspect_ratio", 1.0);
    preview.insert("auto_restore_square_after_export", false);
    PreviewAudioSettings storedAudio = previewAudioSettingsWithBreakSlideTailCheerPreference(
        state_.softwarePreviewAudioSettings_, state_.breakSlideTailCheerMutedPreference_);
    preview.insert("audio", storedAudio.toJson());
    preview.insert("break_slide_tail_cheer_muted", state_.breakSlideTailCheerMutedPreference_);
    preview.insert("timing", state_.softwarePreviewTimingSettings_.toJson());

    app.insert("preview", preview);
    root.insert("app", app);
    UiText::savePreferencesObject(root);
}

void miacode::runtime::EditorHost::persistEditorTextFontPreference() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    ui.insert("editor_text_font_size", state_.editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", state_.editorLineSpacingFactor_);
    ui.insert("editor_half_width_input", state_.editorHalfWidthInputEnabled_);
    ui.insert("editor_overwrite_mode", state_.editorOverwriteModeEnabled_);
    ui.insert("editor_auto_completion", state_.editorAutoCompletionEnabled_);
    ui.insert("editor_ime_input_disabled", state_.editorImeInputDisabled_);
    root.insert("ui", ui);
    UiText::savePreferencesObject(root);
}

void miacode::runtime::EditorHost::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    const int normalized = qBound(kEditorTextFontSizeMin, pointSize, kEditorTextFontSizeMax);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(normalized, state_.editorLineSpacingFactor_);
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorTextFontPointSize_ = normalized;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        session_.shell_->updateBottomTabsDeviceHeight();
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

void miacode::runtime::EditorHost::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(factor);
    const int blockSpacingPixels =
        blockSpacingPixelsForPointSize(state_.editorTextFontPointSize_, state_.editorLineSpacingFactor_);
    if (ui_.metadataExtraEdit_ != nullptr) {
        applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixels);
    }
    state_.suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void miacode::runtime::EditorHost::applyEditorHalfWidthInputEnabled(bool enabled, bool persistPreference)
{
    state_.editorHalfWidthInputEnabled_ = enabled;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void miacode::runtime::EditorHost::applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference)
{
    state_.editorOverwriteModeEnabled_ = enabled;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void miacode::runtime::EditorHost::applyEditorAutoCompletionEnabled(bool enabled, bool persistPreference)
{
    state_.editorAutoCompletionEnabled_ = enabled;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void miacode::runtime::EditorHost::applyEditorImeInputDisabled(bool disabled, bool persistPreference)
{
    state_.editorImeInputDisabled_ = disabled;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void miacode::runtime::EditorHost::syncBookmarksFromEditorText(int changePosition, int charsRemoved, int charsAdded)
{
    Q_UNUSED(changePosition);
    Q_UNUSED(charsRemoved);
    Q_UNUSED(charsAdded);

    QVector<Session::EditorBookmark> derived;
    const int activeDifficultyId = state_.activeDifficultyId_;
    const bool hasActiveDifficulty = session_.hasActiveDifficulty();
    const QVector<int> ids = session_.applicationServices_.workspace().document().difficultyIds();
    for (int difficultyId : ids) {
        const SimaiDifficultyData* difficultyData = session_.applicationServices_.workspace().document().difficulty(difficultyId);
        if (difficultyData == nullptr) {
            continue;
        }
        const bool useLiveEditorText = hasActiveDifficulty && difficultyId == activeDifficultyId;
        const QString chartText = useLiveEditorText ? session_.editorText() : difficultyData->chart;
        QVector<Session::EditorBookmark> perDifficulty = derivedBookmarksForChart(
            chartText,
            difficultyId);
        derived.append(perDifficulty);
    }
    sortBookmarks(derived);
    const bool mutated = !editorBookmarkListsEqual(state_.editorBookmarks_, derived);
    state_.editorBookmarks_ = derived;

    if (mutated && session_.documents_ != nullptr) {
        session_.documents_->rebuildFieldSidebar();
    }
}

void Session::loadPortableState()
{
    editor_->loadPortableState();
}

void Session::resetPortablePreviewSettingsToDefaults()
{
    editor_->resetPortablePreviewSettingsToDefaults();
}

void Session::applyPortablePreviewSettings(const QJsonObject& preview)
{
    editor_->applyPortablePreviewSettings(preview);
}

void Session::savePortableState() const
{
    editor_->savePortableState();
}

void Session::persistEditorTextFontPreference() const
{
    editor_->persistEditorTextFontPreference();
}

int Session::currentEditorTextFontSize() const
{
    return editorTextFontPointSize_;
}

double Session::currentEditorLineSpacingFactor() const
{
    return editorLineSpacingFactor_;
}

bool Session::currentEditorHalfWidthInputEnabled() const
{
    return editorHalfWidthInputEnabled_;
}

bool Session::currentEditorAutoCompletionEnabled() const
{
    return editorAutoCompletionEnabled_;
}

bool Session::currentEditorImeInputDisabled() const
{
    return editorImeInputDisabled_;
}

bool Session::currentWorkspacePanelsSwapped() const
{
    return workspacePanelsSwapped_;
}

int miacode::runtime::SettingsHost::editorTextFontSize() const
{
    return state_.editorTextFontPointSize_;
}

double miacode::runtime::SettingsHost::editorLineSpacingFactor() const
{
    return state_.editorLineSpacingFactor_;
}

bool miacode::runtime::SettingsHost::editorHalfWidthInputEnabled() const
{
    return state_.editorHalfWidthInputEnabled_;
}

bool miacode::runtime::SettingsHost::editorAutoCompletionEnabled() const
{
    return state_.editorAutoCompletionEnabled_;
}

bool miacode::runtime::SettingsHost::editorImeInputDisabled() const
{
    return state_.editorImeInputDisabled_;
}

PreviewCanvasFrameRateMode miacode::runtime::SettingsHost::previewCanvasFrameRateMode() const
{
    return state_.previewCanvasFrameRateMode_;
}

PreviewCanvasFrameRateMode miacode::runtime::SettingsHost::previewStageMediaFrameRateMode() const
{
    return session_.playback_->currentPreviewStageMediaFrameRateMode();
}

PreviewCanvasFrameRateMode miacode::runtime::SettingsHost::timelineFrameRateMode() const
{
    return session_.playback_->currentTimelineFrameRateMode();
}

double miacode::runtime::SettingsHost::previewCanvasRefreshRate() const
{
    return session_.playback_->currentPreviewCanvasRefreshRate();
}

bool miacode::runtime::SettingsHost::videoDecodePrefersSoftware() const
{
    return session_.playback_->currentVideoDecodePrefersSoftware();
}

bool miacode::runtime::SettingsHost::workspacePanelsSwapped() const
{
    return state_.workspacePanelsSwapped_;
}

void miacode::runtime::SettingsHost::applyEditorTextFontSize(int pointSize, bool persist)
{
    session_.editor_->applyEditorTextFontSize(pointSize, persist);
    if (persist) {
        emit session_.editorPreferencesChanged();
    }
}

void miacode::runtime::SettingsHost::applyEditorLineSpacingFactor(double factor, bool persist)
{
    session_.editor_->applyEditorLineSpacingFactor(factor, persist);
    if (persist) {
        emit session_.editorPreferencesChanged();
    }
}

void miacode::runtime::SettingsHost::applyEditorHalfWidthInputEnabled(bool enabled, bool persist)
{
    session_.editor_->applyEditorHalfWidthInputEnabled(enabled, persist);
    if (persist) {
        emit session_.editorPreferencesChanged();
    }
}

void miacode::runtime::SettingsHost::applyEditorAutoCompletionEnabled(bool enabled, bool persist)
{
    session_.editor_->applyEditorAutoCompletionEnabled(enabled, persist);
    if (persist) {
        emit session_.editorPreferencesChanged();
    }
}

void miacode::runtime::SettingsHost::applyEditorImeInputDisabled(bool disabled, bool persist)
{
    session_.editor_->applyEditorImeInputDisabled(disabled, persist);
    if (persist) {
        emit session_.editorPreferencesChanged();
    }
}

void miacode::runtime::SettingsHost::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist)
{
    session_.playback_->setPreviewCanvasFrameRateMode(mode, persist);
}

void miacode::runtime::SettingsHost::setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist)
{
    session_.playback_->setPreviewStageMediaFrameRateMode(mode, persist);
}

void miacode::runtime::SettingsHost::setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist)
{
    session_.playback_->setTimelineFrameRateMode(mode, persist);
}

void miacode::runtime::SettingsHost::setVideoDecodePrefersSoftware(bool preferSoftware, bool persist)
{
    session_.playback_->setVideoDecodePrefersSoftware(preferSoftware, persist);
}

void miacode::runtime::SettingsHost::setWorkspacePanelsSwapped(bool swapped, bool persist)
{
    session_.playback_->setWorkspacePanelsSwapped(swapped, persist);
}

void Session::applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference)
{
    editor_->applyEditorOverwriteModeEnabled(enabled, persistPreference);
    if (persistPreference) emit editorPreferencesChanged();
}
