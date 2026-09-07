#include "QmlCoverExportSession.h"

#include "core/chart/document/SimaiDocument.h"
#include "UiText.h"
#include "app/v2/PlaybackControl.h"
#include "tools/cover_export/CoverCompositionState.h"
#include "tools/cover_export/CoverFramePlaybackController.h"
#include "tools/cover_export/CoverFrameExportPlan.h"
#include "tools/cover_export/CoverFrameSceneBinder.h"
#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/SceneFrameRenderer.h"
#include "tools/video_export/FontLibrary.h"
#include "core/scene/PreviewLayerOrder.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QUrl>

#include <iterator>

namespace {

struct CoverResolutionPreset {
    int width;
    int height;
    const char* label;
};

constexpr CoverResolutionPreset kCoverResolutionPresets[] = {
    {720, 720, "720×720 (1:1)"}, {1024, 1024, "1024×1024 (1:1)"},
    {960, 720, "960×720 (4:3)"}, {1280, 720, "1280×720 (16:9)"},
    {1080, 1080, "1080×1080 (1:1)"}, {1440, 1080, "1440×1080 (4:3)"},
    {1920, 1080, "1920×1080 (16:9)"}, {1440, 1440, "1440×1440 (1:1)"},
    {1920, 1440, "1920×1440 (4:3)"}, {2560, 1440, "2560×1440 (16:9)"},
};

QVariantMap loadBannerTemplate()
{
    QFile file(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

QString normalisedCoverOutputDirectory(const QString& chartPath)
{
    const QFileInfo chartInfo(chartPath);
    const QDir directory = chartInfo.absoluteDir();
    return directory.exists() ? directory.absolutePath() : QDir::currentPath();
}

}  // namespace

QmlCoverExportSession::QmlCoverExportSession(miacode::v2::ExportEngine& exportEngine,
                                             miacode::v2::UiRequestService& uiRequests,
                                             miacode::v2::PlaybackControl*& playbackControlSlot,
                                             QObject* parent)
    : QObject(parent)
    , exportEngine_(exportEngine)
    , uiRequests_(&uiRequests)
    , playbackControlSlot_(&playbackControlSlot)
    , layout_(std::make_unique<miacode::cover_export::CoverLayoutModel>())
    , playback_(std::make_unique<miacode::cover_export::CoverFramePlaybackController>(this))
    , sceneBinder_(std::make_unique<miacode::cover_export::CoverFrameSceneBinder>(this))
    , bannerTemplate_(loadBannerTemplate())
{
    layout_->ensureDefaultLayers();
    activeLayerKey_ = miacode::cover_export::CoverLayoutModel::cardKey();
    connect(playback_.get(), &miacode::cover_export::CoverFramePlaybackController::secondsChanged,
            this, &QmlCoverExportSession::onPlaybackSecondsChanged);
    connect(playback_.get(), &miacode::cover_export::CoverFramePlaybackController::reachedEnd,
            this, &QmlCoverExportSession::onPlaybackReachedEnd);
    connect(playback_.get(), &miacode::cover_export::CoverFramePlaybackController::playingChanged,
            this, &QmlCoverExportSession::chartFramePlayingChanged);
    connect(sceneBinder_.get(), &miacode::cover_export::CoverFrameSceneBinder::liveChartSceneBoundChanged,
            this, &QmlCoverExportSession::liveChartSceneBoundChanged);
}

QmlCoverExportSession::~QmlCoverExportSession()
{
    stopAndDetachLiveChartScene();
    if (auto* scene = qobject_cast<PreviewQuickSceneRoot*>(lastLiveChartScene_.data())) {
        scene->setFrameState(nullptr);
    }
    lastLiveChartScene_.clear();
}

QObject* QmlCoverExportSession::uiRequests() const { return uiRequests_; }
QObject* QmlCoverExportSession::layoutModel() const { return layout_.get(); }
QObject* QmlCoverExportSession::activeLayer() const { return activeCoverLayer(); }

QVariantMap QmlCoverExportSession::templateMap() const
{
    QVariantMap result = bannerTemplate_;
    miacode::video_export::applyBannerFontOverride(result, cardFontDisplayPath_, cardFontBodyPath_);
    return result;
}

QVariantMap QmlCoverExportSession::trackOverrides() const
{
    QVariantMap track;
    const IntroBannerSpec& banner = task_.intro;
    track.insert(QStringLiteral("title"), banner.title);
    track.insert(QStringLiteral("artist"), banner.artist);
    track.insert(QStringLiteral("designer"), banner.designer);
    track.insert(QStringLiteral("level"), banner.level);
    track.insert(QStringLiteral("difficulty"), banner.difficulty);
    track.insert(QStringLiteral("bpm"), banner.bpm);
    track.insert(QStringLiteral("mode"), isAutoIntroBannerMode(cardMode_)
        ? normalizedIntroBannerMode(banner.mode)
        : normalizedIntroBannerMode(cardMode_));
    track.insert(QStringLiteral("lvRenderMode"), levelTextRender_ ? QStringLiteral("text")
                                                                     : QStringLiteral("atlas"));
    track.insert(QStringLiteral("stillTextMode"), longTextMode_);
    return track;
}

QUrl QmlCoverExportSession::jacketImage() const
{
    return task_.intro.jacketPath.trimmed().isEmpty() ? QUrl() : QUrl::fromLocalFile(task_.intro.jacketPath);
}

QUrl QmlCoverExportSession::backgroundImage() const
{
    return backgroundPath_.trimmed().isEmpty() ? QUrl() : QUrl::fromLocalFile(backgroundPath_);
}

int QmlCoverExportSession::backgroundMode() const { return static_cast<int>(backgroundMode_); }

QVariantList QmlCoverExportSession::fontLibraryOptions() const
{
    QVariantList output;
    const auto entries = miacode::video_export::fontLibraryEntries(
        true, UiText::text(QStringLiteral("card_font.default")));
    for (const auto& entry : entries) {
        output.append(QVariantMap{{QStringLiteral("label"), entry.label},
                                  {QStringLiteral("path"), entry.path},
                                  {QStringLiteral("family"), entry.family}});
    }
    return output;
}

QVariantList QmlCoverExportSession::resolutionOptions() const
{
    QVariantList output;
    for (const auto& preset : kCoverResolutionPresets) {
        output.append(QVariantMap{{QStringLiteral("label"), QString::fromUtf8(preset.label)},
                                  {QStringLiteral("width"), preset.width},
                                  {QStringLiteral("height"), preset.height}});
    }
    return output;
}

int QmlCoverExportSession::outputWidth() const { return kCoverResolutionPresets[resolutionIndex_].width; }
int QmlCoverExportSession::outputHeight() const { return kCoverResolutionPresets[resolutionIndex_].height; }

QObject* QmlCoverExportSession::chartSceneBinder() const
{
    return sceneBinder_.get();
}

bool QmlCoverExportSession::chartFramePlaying() const
{
    return playback_ != nullptr && playback_->playing();
}

bool QmlCoverExportSession::liveChartSceneBound() const
{
    return sceneBinder_ != nullptr && sceneBinder_->liveChartSceneBound();
}

double QmlCoverExportSession::activeChartFrameSeconds() const
{
    auto* layer = activeCoverLayer();
    return layer != nullptr && layer->kind() == QStringLiteral("chartFrame")
        ? layer->frameSeconds() : 0.0;
}

QVariantList QmlCoverExportSession::builtinPresets() const
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("card")},
                    {QStringLiteral("label"), UiText::text(QStringLiteral("cover.centered_card_default"))},
                    {QStringLiteral("requiresChartFrame"), false}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("card_chart_frame")},
                    {QStringLiteral("label"), UiText::text(QStringLiteral("cover.card_chart_frame"))},
                    {QStringLiteral("requiresChartFrame"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("dual_chart_frames")},
                    {QStringLiteral("label"), UiText::text(QStringLiteral("cover.dual_chart_frame_collage"))},
                    {QStringLiteral("requiresChartFrame"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("pure_chart_frame")},
                    {QStringLiteral("label"), UiText::text(QStringLiteral("cover.pure_chart_frame"))},
                    {QStringLiteral("requiresChartFrame"), true}},
    };
}

void QmlCoverExportSession::enter(int preferredDifficultyId)
{
    if (!pageSessionActive_) {
        pageSessionActive_ = true;
        emit pageSessionActiveChanged();
    }
    rebuildDifficultyList();
    selectDifficulty(defaultDifficultyId(preferredDifficultyId));
    refreshSavedLists();
    emit fontLibraryChanged();
}

void QmlCoverExportSession::leave()
{
    if (!pageSessionActive_) {
        return;
    }
    commitActiveLayerFrameSeconds();
    stopAndDetachLiveChartScene();
    persistComposition();
    pageSessionActive_ = false;
    emit pageSessionActiveChanged();
}

double QmlCoverExportSession::chartFrameDiskDiameter() const
{
    return chartFrameAvailable_ && frameRenderer_ != nullptr
        ? frameRenderer_->playfieldDiskDiameterFraction() : 0.0;
}

bool QmlCoverExportSession::containsDifficulty(int difficultyId) const
{
    for (const QVariant& row : difficulties_) {
        if (row.toMap().value(QStringLiteral("id")).toInt() == difficultyId) {
            return true;
        }
    }
    return false;
}

int QmlCoverExportSession::defaultDifficultyId(int preferredDifficultyId) const
{
    if (containsDifficulty(preferredDifficultyId)) {
        return preferredDifficultyId;
    }
    if (containsDifficulty(selectedDifficultyId_)) {
        return selectedDifficultyId_;
    }
    return difficulties_.isEmpty() ? 0 : difficulties_.constFirst().toMap().value(QStringLiteral("id")).toInt();
}

void QmlCoverExportSession::rebuildDifficultyList()
{
    QVariantList next;
    for (int id : exportEngine_.difficultyIds()) {
        next.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), SimaiDocument::difficultyShortName(id)},
        });
    }
    if (difficulties_ != next) {
        difficulties_ = next;
        emit difficultiesChanged();
    }
}

void QmlCoverExportSession::selectDifficulty(int difficultyId)
{
    const int next = containsDifficulty(difficultyId) ? difficultyId : 0;
    if (selectedDifficultyId_ != next) {
        selectedDifficultyId_ = next;
        emit selectedDifficultyIdChanged();
    }
    if (pageSessionActive_ && next > 0) {
        seedFromDifficulty(next);
    }
}

void QmlCoverExportSession::seedFromDifficulty(int difficultyId)
{
    commitActiveLayerFrameSeconds();
    stopAndDetachLiveChartScene();
    setBusy(true);
    task_ = exportEngine_.buildSeedTask(difficultyId);
    // Output folder and canvas size are seeded from the chart once. Every later
    // call re-seeds because the user picked a different DIFFICULTY, and the
    // difficulty says nothing about where the image goes or how big it is —
    // re-deriving them there silently discarded whatever the user had chosen.
    // The saved composition, applied just below on the first seed, wins over
    // both.
    if (outputDirectory_.isEmpty()) {
        outputDirectory_ = normalisedCoverOutputDirectory(task_.chartPath);
    }
    if (!hasLoadedPreferences_) {
        for (int index = 0; index < std::size(kCoverResolutionPresets); ++index) {
            const auto& preset = kCoverResolutionPresets[index];
            if (preset.width == task_.outputWidth && preset.height == task_.outputHeight) {
                resolutionIndex_ = index;
                break;
            }
        }
    }
    frameRenderer_ = std::make_unique<miacode::cover_export::SceneFrameRenderer>();
    chartFrameAvailable_ = !task_.noteMarkers.isEmpty() && frameRenderer_->bootstrap(task_);
    chartFrameDuration_ = chartFrameAvailable_ ? frameRenderer_->contentDurationSeconds() : 0.0;

    if (!hasLoadedPreferences_) {
        const QJsonObject saved = miacode::cover_export::CoverCompositionState::loadPreferences();
        if (!saved.isEmpty()) {
            applyCompositionJson(saved, false);
        }
        hasLoadedPreferences_ = true;
    }
    // The layout survives difficulty changes, but a still belongs to the
    // chart that produced it. Drop those secondary images before installing a
    // new live frame state; the active layer will be painted by the live scene
    // immediately, while a later capture may repopulate inactive layers.
    for (auto* layer : layout_->chartFrameLayers()) {
        layout_->clearLayerImage(layer->key());
    }
    if (!chartFrameAvailable_) {
        for (auto* layer : layout_->chartFrameLayers()) {
            layer->setVisible(false);
        }
    }
    // v1 makes the first visible chart frame the live frame when entering the
    // cover editor. The visible Quick scene is the primary preview; a still
    // capture is never allowed to decide whether the page is usable.
    const auto visibleChartFrames = layout_->visibleChartFrameLayers();
    const QString nextActiveLayerKey = chartFrameAvailable_ && !visibleChartFrames.isEmpty()
        ? visibleChartFrames.constFirst()->key()
        : miacode::cover_export::CoverLayoutModel::cardKey();
    if (activeLayerKey_ != nextActiveLayerKey) {
        activeLayerKey_ = nextActiveLayerKey;
        emit activeLayerChanged();
    }
    if (activeCoverLayer() == nullptr) {
        activeLayerKey_ = miacode::cover_export::CoverLayoutModel::cardKey();
        emit activeLayerChanged();
    }
    emit outputChanged();
    emit chartFrameAvailabilityChanged();
    emit inputsChanged();
    syncPlaybackFromActiveLayer();
    // The cover window is constructed before the session is seeded. Restoring a saved
    // frame can leave playback_->seconds() unchanged during this final sync,
    // so no secondsChanged signal would be emitted for the existing QML
    // binding. Republish the settled layer time after the active layer and
    // duration are both final.
    emit activeChartFrameSecondsChanged();
    rebindLiveChartScene();
    // Warm the secondary capture surface without making it a prerequisite for
    // the live preview. By the time the user switches away from the active
    // chart frame, the surface has had normal event-loop time to initialize.
    if (chartFrameAvailable_ && !visibleChartFrames.isEmpty() && frameRenderer_ != nullptr) {
        frameRenderer_->prepareCaptureWindow(
            qBound(512, qMax(outputWidth(), outputHeight()), 2048),
            activeChartFrameSeconds());
    }
    setBusy(false);
}

miacode::cover_export::CoverLayer* QmlCoverExportSession::activeCoverLayer() const
{
    return layout_ != nullptr ? layout_->layer(activeLayerKey_) : nullptr;
}

void QmlCoverExportSession::selectLayerKey(const QString& key)
{
    if (layout_ == nullptr || layout_->layer(key) == nullptr || activeLayerKey_ == key) {
        return;
    }
    if (activeCoverLayer() != nullptr && activeCoverLayer()->kind() == QStringLiteral("chartFrame")) {
        playback_->pause();
        playback_->cancelInput();
        commitActiveLayerFrameSeconds();
    }
    activeLayerKey_ = key;
    emit activeLayerChanged();
    syncPlaybackFromActiveLayer();
    rebindLiveChartScene();
    emit activeChartFrameSecondsChanged();
    emit inputsChanged();
}

bool QmlCoverExportSession::renderChartFrame(miacode::cover_export::CoverLayer* layer,
                                             int sidePx, bool reportErrors)
{
    if (layer == nullptr || layer->kind() != QStringLiteral("chartFrame")
        || !chartFrameAvailable_ || frameRenderer_ == nullptr) {
        return false;
    }
    const int side = sidePx > 0 ? sidePx : qBound(512, qMax(outputWidth(), outputHeight()), 2048);
    layer->setFrameSeconds(qBound(0.0, layer->frameSeconds(), chartFrameDuration_));
    const double previousPlayhead = frameRenderer_->playheadSeconds();
    const bool captureWasReady = frameRenderer_->captureReady();
    QString error;
    const QImage image = frameRenderer_->renderAt(layer->frameSeconds(), side, &error);
    frameRenderer_->setPlayheadSeconds(previousPlayhead);
    if (auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(sceneBinder_->liveChartScene())) {
        liveScene->update();
    }
    if (image.isNull()) {
        // A newly-created Quick window needs one or more event-loop turns to
        // become exposed and initialize its scene graph. This is a normal
        // transient state for the preview path, not a user-visible render error.
        if (!captureWasReady && !frameRenderer_->captureReady()) {
            return false;
        }
        if (reportErrors) {
            notifyError(UiText::text(QStringLiteral("cover.chart_frame")),
                        UiText::text(QStringLiteral("cover.could_not_render_the_chart")), error);
        }
        return false;
    }
    layout_->setLayerImage(layer->key(), image);
    return true;
}

void QmlCoverExportSession::syncPlaybackFromActiveLayer()
{
    if (playback_ == nullptr) {
        return;
    }
    auto* layer = activeCoverLayer();
    const bool chartFrame = isActiveChartFrame(layer);
    playback_->setDuration(chartFrame && chartFrameAvailable_ ? chartFrameDuration_ : 0.0);
    playback_->setSeconds(chartFrame ? qBound(0.0, layer->frameSeconds(), chartFrameDuration_) : 0.0);
    if (chartFrame && layer->frameSeconds() != playback_->seconds()) {
        layer->setFrameSeconds(playback_->seconds());
    }
    if (chartFrame && frameRenderer_ != nullptr) {
        frameRenderer_->setPlayheadSeconds(playback_->seconds());
    }
    if (!chartFrame) {
        playback_->pause();
        playback_->cancelInput();
    }
}

void QmlCoverExportSession::onPlaybackSecondsChanged()
{
    auto* layer = activeCoverLayer();
    if (!isActiveChartFrame(layer) || playback_ == nullptr) {
        return;
    }
    const double seconds = qBound(0.0, playback_->seconds(), chartFrameDuration_);
    layer->setFrameSeconds(seconds);
    if (frameRenderer_ != nullptr) {
        frameRenderer_->setPlayheadSeconds(seconds);
    }
    if (auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(sceneBinder_->liveChartScene())) {
        liveScene->update();
    }
    emit activeChartFrameSecondsChanged();
}

void QmlCoverExportSession::onPlaybackReachedEnd()
{
    commitActiveLayerFrameSeconds();
}

bool QmlCoverExportSession::renderVisibleChartFramesForExport(int sidePx)
{
    if (!chartFrameAvailable_ || layout_ == nullptr) {
        return true;
    }
    const auto plan = miacode::cover_export::CoverFrameExportPlan::fromVisibleLayers(
        *layout_, activeLayerKey_, activeChartFrameSeconds());
    for (const auto& frame : plan.frames()) {
        auto* layer = layout_->layer(frame.key);
        if (layer == nullptr) {
            return false;
        }
        layer->setFrameSeconds(frame.seconds);
        if (!renderChartFrame(layer, sidePx, true)) {
            return false;
        }
    }
    return true;
}

void QmlCoverExportSession::rebindLiveChartScene()
{
    if (lastLiveChartScene_.isNull()) {
        return;
    }
    auto* layer = activeCoverLayer();
    if (!isActiveChartFrame(layer) || !layer->visible() || !chartFrameAvailable_) {
        if (auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(sceneBinder_->liveChartScene())) {
            liveScene->setFrameState(nullptr);
        }
        sceneBinder_->detachLiveChartScene();
        return;
    }
    bindLiveChartScene(lastLiveChartScene_.data());
}

void QmlCoverExportSession::stopAndDetachLiveChartScene()
{
    if (playback_ != nullptr) {
        playback_->pause();
        playback_->cancelInput();
    }
    if (sceneBinder_ != nullptr) {
        if (auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(lastLiveChartScene_.data())) {
            liveScene->setFrameState(nullptr);
        }
        sceneBinder_->detachLiveChartScene();
    }
}

bool QmlCoverExportSession::isActiveChartFrame(
    const miacode::cover_export::CoverLayer* layer) const
{
    return layer != nullptr && layer->kind() == QStringLiteral("chartFrame")
        && activeLayerKey_ == layer->key();
}

void QmlCoverExportSession::addChartFrameLayer()
{
    if (!chartFrameAvailable_ || layout_ == nullptr) {
        notifyError(UiText::text(QStringLiteral("cover.chart_frame")),
                    UiText::text(QStringLiteral("cover.this_difficulty_has_no_chart")));
        return;
    }
    auto* layer = layout_->addChartFrameLayer(frameRenderer_ != nullptr ? frameRenderer_->playheadSeconds() : 0.0);
    if (layer == nullptr) {
        return;
    }
    selectLayerKey(layer->key());
    renderChartFrame(layer);
    persistComposition();
}

void QmlCoverExportSession::addImageLayer()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("cover.choose_image"));
    request.nameFilters = {UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp"))};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (path.isEmpty() || layout_ == nullptr) return;
        if (auto* layer = layout_->addImageLayer(path)) {
            selectLayerKey(layer->key());
            persistComposition();
        }
    });
}

void QmlCoverExportSession::addTextLayer()
{
    if (layout_ == nullptr) return;
    if (auto* layer = layout_->addTextLayer(UiText::text(QStringLiteral("cover.text_layer_default")))) {
        selectLayerKey(layer->key());
        persistComposition();
    }
}

void QmlCoverExportSession::duplicateActiveLayer()
{
    if (layout_ == nullptr) return;
    if (auto* layer = layout_->duplicateLayer(activeLayerKey_)) {
        selectLayerKey(layer->key());
        if (layer->kind() == QStringLiteral("chartFrame")) {
            renderChartFrame(layer);
        }
        persistComposition();
    }
}

void QmlCoverExportSession::removeActiveLayer()
{
    if (layout_ == nullptr || activeLayerKey_ == miacode::cover_export::CoverLayoutModel::cardKey()) return;
    if (isActiveChartFrame(activeCoverLayer())) {
        playback_->pause();
        playback_->cancelInput();
        commitActiveLayerFrameSeconds();
    }
    const QString next = layout_->selectionAfterRemoval(activeLayerKey_);
    if (layout_->removeLayer(activeLayerKey_)) {
        activeLayerKey_ = layout_->layer(next) != nullptr ? next : miacode::cover_export::CoverLayoutModel::cardKey();
        emit activeLayerChanged();
        syncPlaybackFromActiveLayer();
        rebindLiveChartScene();
        emit activeChartFrameSecondsChanged();
        persistComposition();
    }
}

void QmlCoverExportSession::bringActiveLayerToFront()
{
    if (layout_ == nullptr) return;
    layout_->bringToFront(layout_->indexOfKey(activeLayerKey_));
    persistComposition();
}

void QmlCoverExportSession::sendActiveLayerToBack()
{
    if (layout_ == nullptr) return;
    layout_->sendToBack(layout_->indexOfKey(activeLayerKey_));
    persistComposition();
}

void QmlCoverExportSession::raiseActiveLayer()
{
    if (layout_ == nullptr) return;
    layout_->raiseLayer(layout_->indexOfKey(activeLayerKey_));
    persistComposition();
}

void QmlCoverExportSession::lowerActiveLayer()
{
    if (layout_ == nullptr) return;
    layout_->lowerLayer(layout_->indexOfKey(activeLayerKey_));
    persistComposition();
}

void QmlCoverExportSession::browseActiveLayerImage()
{
    auto* layer = activeCoverLayer();
    if (layer == nullptr || layer->kind() != QStringLiteral("image")) return;
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("cover.choose_image"));
    request.startPath = QFileInfo(layer->imagePath()).absolutePath();
    request.nameFilters = {UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp"))};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (path.isEmpty()) return;
        if (auto* active = activeCoverLayer(); active != nullptr && active->kind() == QStringLiteral("image")) {
            active->setImagePath(path);
            persistComposition();
        }
    });
}

void QmlCoverExportSession::requestFont(bool displayFont, bool textLayerFont)
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("card_font.import"));
    request.nameFilters = {QStringLiteral("Font files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this, displayFont, textLayerFont](const QString& path) {
        if (path.isEmpty()) return;
        const auto result = miacode::video_export::importFontFileIntoLibrary(path);
        if (result.path.isEmpty()) {
            notifyError(UiText::text(QStringLiteral("card_font.import")),
                        UiText::text(result.failure == miacode::video_export::FontImportFailure::CopyFailed
                                         ? QStringLiteral("card_font.copy_failed")
                                         : QStringLiteral("card_font.invalid_font")));
            return;
        }
        if (textLayerFont) {
            if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("text")) {
                layer->setFontPath(result.path);
                persistComposition();
            }
        } else if (displayFont) {
            setCardFontDisplayPath(result.path);
        } else {
            setCardFontBodyPath(result.path);
        }
        emit fontLibraryChanged();
    });
}

void QmlCoverExportSession::importActiveLayerFont() { requestFont(false, true); }
void QmlCoverExportSession::importCardDisplayFont() { requestFont(true, false); }
void QmlCoverExportSession::importCardBodyFont() { requestFont(false, false); }

void QmlCoverExportSession::setActiveLayerVisible(bool visible)
{
    if (auto* layer = activeCoverLayer()) {
        layer->setVisible(visible);
        if (layer->kind() == QStringLiteral("chartFrame")) {
            if (!visible) {
                playback_->pause();
                playback_->cancelInput();
            } else {
                renderChartFrame(layer);
            }
            syncPlaybackFromActiveLayer();
            rebindLiveChartScene();
        }
        persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerLocked(bool locked)
{
    if (auto* layer = activeCoverLayer()) { layer->setLocked(locked); persistComposition(); }
}
void QmlCoverExportSession::setActiveLayerOpacity(double opacity)
{
    if (auto* layer = activeCoverLayer()) { layer->setOpacity(qBound(0.0, opacity, 1.0)); persistComposition(); }
}
void QmlCoverExportSession::setActiveLayerSizeFraction(double sizeFraction)
{
    if (auto* layer = activeCoverLayer()) { layer->setSizeFraction(qBound(0.05, sizeFraction, 2.0)); persistComposition(); }
}
void QmlCoverExportSession::setActiveLayerCenter(double nx, double ny)
{
    if (auto* layer = activeCoverLayer()) {
        layer->setNx(qBound(0.0, nx, 1.0));
        layer->setNy(qBound(0.0, ny, 1.0));
        persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerText(const QString& text)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("text")) {
        layer->setText(text); persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerTextColor(const QString& color)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("text")) {
        layer->setTextColor(color); persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerTextBold(bool bold)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("text")) {
        layer->setTextBold(bold); persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerFrameSeconds(double seconds)
{
    previewActiveLayerFrameSeconds(seconds);
    commitActiveLayerFrameSeconds();
}

void QmlCoverExportSession::previewActiveLayerFrameSeconds(double seconds)
{
    auto* layer = activeCoverLayer();
    if (!isActiveChartFrame(layer) || playback_ == nullptr) {
        return;
    }
    playback_->setSeconds(qBound(0.0, seconds, chartFrameDuration_));
    // `setSeconds` emits synchronously when the value changes. Keep this
    // explicit path for a no-op drag at the same value as well.
    onPlaybackSecondsChanged();
}

void QmlCoverExportSession::commitActiveLayerFrameSeconds()
{
    auto* layer = activeCoverLayer();
    if (!isActiveChartFrame(layer)) {
        return;
    }
    layer->setFrameSeconds(qBound(0.0, layer->frameSeconds(), chartFrameDuration_));
    renderChartFrame(layer);
    persistComposition();
    emit activeChartFrameSecondsChanged();
}

void QmlCoverExportSession::toggleActiveLayerPlayback()
{
    if (!isActiveChartFrame(activeCoverLayer()) || playback_ == nullptr || !chartFrameAvailable_) {
        return;
    }
    syncPlaybackFromActiveLayer();
    playback_->toggle();
}

void QmlCoverExportSession::beginActiveLayerKeySeek(int direction)
{
    if (!isActiveChartFrame(activeCoverLayer()) || playback_ == nullptr || !chartFrameAvailable_) {
        return;
    }
    syncPlaybackFromActiveLayer();
    playback_->beginKeySeek(direction);
}

void QmlCoverExportSession::endActiveLayerKeySeek()
{
    if (playback_ == nullptr) {
        return;
    }
    playback_->endKeySeek();
    commitActiveLayerFrameSeconds();
}

void QmlCoverExportSession::cancelActiveLayerInput()
{
    if (playback_ == nullptr) {
        return;
    }
    playback_->pause();
    playback_->cancelInput();
}

void QmlCoverExportSession::commitActiveLayerGeometry()
{
    persistComposition();
}

void QmlCoverExportSession::commitCompositionChanges()
{
    persistComposition();
}

void QmlCoverExportSession::bindLiveChartScene(QObject* scene)
{
    auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(scene);
    if (liveScene == nullptr) {
        if (scene != nullptr) {
            return;
        }
        if (sceneBinder_ != nullptr) {
            if (auto* current = qobject_cast<PreviewQuickSceneRoot*>(sceneBinder_->liveChartScene())) {
                current->setFrameState(nullptr);
            }
            sceneBinder_->detachLiveChartScene();
        }
        lastLiveChartScene_.clear();
        return;
    }
    lastLiveChartScene_ = liveScene;
    liveScene->setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    liveScene->setFrameState(
        frameRenderer_ != nullptr && chartFrameAvailable_ ? frameRenderer_->frameState() : nullptr);
    sceneBinder_->setFrameState(
        frameRenderer_ != nullptr && chartFrameAvailable_ ? frameRenderer_->frameState() : nullptr);
    sceneBinder_->bindLiveChartScene(liveScene);
}

void QmlCoverExportSession::unbindLiveChartScene(QObject* scene)
{
    if (scene == nullptr || sceneBinder_ == nullptr) {
        return;
    }
    if (auto* liveScene = qobject_cast<PreviewQuickSceneRoot*>(scene)) {
        liveScene->setFrameState(nullptr);
    }
    sceneBinder_->unbindLiveChartScene(scene);
    if (lastLiveChartScene_.data() == scene) {
        lastLiveChartScene_.clear();
    }
}
void QmlCoverExportSession::setActiveLayerFrameBackgroundMode(const QString& mode)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        layer->setFrameBgMode(mode == QStringLiteral("transparent") ? mode : QStringLiteral("image"));
        persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerFrameBackgroundBrightness(double brightness)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        layer->setFrameBgBrightness(qBound(0.0, brightness, 1.0)); persistComposition();
    }
}
void QmlCoverExportSession::setActiveLayerFrameBackgroundTransparency(double transparency)
{
    if (auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        layer->setFrameBgTransparency(qBound(0.0, transparency, 1.0)); persistComposition();
    }
}

void QmlCoverExportSession::setBackgroundMode(int mode)
{
    const auto next = static_cast<miacode::cover_export::CoverBackgroundMode>(qBound(0, mode, 2));
    if (backgroundMode_ == next) return;
    backgroundMode_ = next; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setBlurBackground(bool enabled)
{
    if (blurBackground_ == enabled) return;
    blurBackground_ = enabled; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setBackgroundBrightness(double value)
{
    const double next = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(backgroundBrightness_, next)) return;
    backgroundBrightness_ = next; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setCardShadow(bool enabled)
{
    if (cardShadow_ == enabled) return;
    cardShadow_ = enabled; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setCardMode(const QString& mode)
{
    const QString next = isAutoIntroBannerMode(mode)
        ? QStringLiteral("auto") : normalizedIntroBannerMode(mode);
    if (cardMode_ == next) return;
    cardMode_ = next; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setLevelTextRender(bool enabled)
{
    if (levelTextRender_ == enabled) return;
    levelTextRender_ = enabled; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setLongTextMode(const QString& mode)
{
    const QString next = mode == QStringLiteral("ellipsis") ? mode : QStringLiteral("shrink");
    if (longTextMode_ == next) return;
    longTextMode_ = next; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setCardFontDisplayPath(const QString& path)
{
    if (cardFontDisplayPath_ == path) return;
    cardFontDisplayPath_ = path; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setCardFontBodyPath(const QString& path)
{
    if (cardFontBodyPath_ == path) return;
    cardFontBodyPath_ = path; emit inputsChanged(); persistComposition();
}
void QmlCoverExportSession::setResolutionIndex(int index)
{
    const int next = qBound(0, index, static_cast<int>(std::size(kCoverResolutionPresets)) - 1);
    if (resolutionIndex_ == next) return;
    resolutionIndex_ = next; emit outputChanged(); persistComposition();
}
void QmlCoverExportSession::setOutputDirectory(const QString& path)
{
    const QString next = QDir::cleanPath(path.trimmed());
    if (next.isEmpty() || outputDirectory_ == next) return;
    outputDirectory_ = next; emit outputChanged(); persistComposition();
}

void QmlCoverExportSession::browseBackgroundImage()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("cover.choose_background_image"));
    request.startPath = QFileInfo(backgroundPath_).absolutePath();
    request.nameFilters = {UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp"))};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (path.isEmpty()) return;
        backgroundPath_ = path;
        backgroundMode_ = miacode::cover_export::CoverBackgroundMode::Custom;
        emit inputsChanged();
        persistComposition();
    });
}

void QmlCoverExportSession::resetLayout()
{
    if (layout_ == nullptr || uiRequests_ == nullptr) return;
    // Reset throws away every layer and every position, so it asks first — the
    // same question v1's 布局 ▾ menu asked before it.
    uiRequests_->requestConfirmation(
        UiText::text(QStringLiteral("cover.reset_layout")),
        UiText::text(QStringLiteral("cover.reset_discards_all_current_layers")),
        UiText::text(QStringLiteral("cover.reset_layout")),
        [this](bool accepted) {
            if (!accepted || layout_ == nullptr) return;
            layout_->resetLayout();
            // The default layout carries a chart frame; a difficulty with no
            // renderable notes must not get it back through the reset.
            if (!chartFrameAvailable_) {
                for (auto* layer : layout_->chartFrameLayers()) {
                    layer->setVisible(false);
                }
            }
            activeLayerKey_ = miacode::cover_export::CoverLayoutModel::cardKey();
            emit activeLayerChanged();
            persistComposition();
        });
}

QJsonObject QmlCoverExportSession::compositionJson() const
{
    miacode::cover_export::CoverCompositionState state;
    state.size = QSize(outputWidth(), outputHeight());
    state.background = {{QStringLiteral("mode"), backgroundMode_ == miacode::cover_export::CoverBackgroundMode::Custom ? QStringLiteral("custom")
                          : backgroundMode_ == miacode::cover_export::CoverBackgroundMode::Transparent ? QStringLiteral("transparent")
                                                                                                           : QStringLiteral("jacket")},
                        {QStringLiteral("customPath"), backgroundPath_},
                        {QStringLiteral("blur"), blurBackground_},
                        {QStringLiteral("brightness"), backgroundBrightness_}};
    state.card = {{QStringLiteral("mode"), cardMode_},
                  {QStringLiteral("shadow"), cardShadow_},
                  {QStringLiteral("levelTextRender"), levelTextRender_},
                  {QStringLiteral("longText"), longTextMode_},
                  {QStringLiteral("fontDisplay"), cardFontDisplayPath_},
                  {QStringLiteral("fontBody"), cardFontBodyPath_}};
    state.layout = layout_ != nullptr ? layout_->toJson() : QJsonObject{};
    state.outputDirectory = outputDirectory_;
    return state.toJson();
}

QJsonObject QmlCoverExportSession::sharedCompositionJson() const
{
    QJsonObject root = compositionJson();
    // A saved .miacover travels to other charts and other machines. The output
    // folder is a property of this installation, not of the look being shared,
    // so it stays in the local preferences blob and out of the file.
    root.remove(QStringLiteral("output"));
    return root;
}

QJsonObject QmlCoverExportSession::presetCompositionJson() const
{
    QJsonObject root = sharedCompositionJson();
    // A preset also keeps the current canvas rather than carrying its own.
    root.remove(QStringLiteral("size"));
    return root;
}

QJsonObject QmlCoverExportSession::builtinPresetComposition(const QString& id) const
{
    if (id != QStringLiteral("card")
        && id != QStringLiteral("card_chart_frame")
        && id != QStringLiteral("dual_chart_frames")
        && id != QStringLiteral("pure_chart_frame")) {
        return {};
    }

    miacode::cover_export::CoverLayoutModel model;
    auto* card = model.layer(miacode::cover_export::CoverLayoutModel::cardKey());
    if (card == nullptr) {
        return {};
    }
    auto setGeometry = [](miacode::cover_export::CoverLayer* layer,
                          double nx, double ny, double size, int z, bool visible) {
        if (layer == nullptr) return;
        layer->setNx(nx);
        layer->setNy(ny);
        layer->setSizeFraction(size);
        layer->setZ(z);
        layer->setVisible(visible);
    };

    setGeometry(card, 0.5, 0.5, 0.85, 1, true);
    if (id == QStringLiteral("card")) {
        model.normalizeZOrder();
    } else if (id == QStringLiteral("card_chart_frame")) {
        auto* frame = model.addChartFrameLayer(0.0);
        setGeometry(card, 0.64, 0.5, 0.78, 1, true);
        setGeometry(frame, 0.32, 0.5, 0.82, 0, true);
        model.normalizeZOrder();
    } else if (id == QStringLiteral("dual_chart_frames")) {
        auto* first = model.addChartFrameLayer(0.0);
        auto* second = model.addChartFrameLayer(0.0);
        setGeometry(card, 0.5, 0.5, 0.85, 0, false);
        setGeometry(first, 0.30, 0.40, 0.56, 1, true);
        setGeometry(second, 0.66, 0.60, 0.56, 0, true);
        model.normalizeZOrder();
    } else {
        auto* frame = model.addChartFrameLayer(0.0);
        setGeometry(card, 0.5, 0.5, 0.85, 0, false);
        setGeometry(frame, 0.5, 0.5, 0.92, 1, true);
        model.normalizeZOrder();
    }

    QJsonObject root = presetCompositionJson();
    root.remove(QStringLiteral("size"));
    root.insert(QStringLiteral("layout"), model.toJson());
    return root;
}

bool QmlCoverExportSession::applyCompositionJsonInternal(const QJsonObject& root,
                                                         bool reportErrors,
                                                         bool renderChartFrames)
{
    miacode::cover_export::CoverCompositionState state;
    QString error;
    if (!miacode::cover_export::CoverCompositionState::fromJson(root, &state, &error)) {
        if (reportErrors) notifyError(UiText::text(QStringLiteral("cover.import_layout_2")),
                                      UiText::text(QStringLiteral("cover.the_layout_file_is_not")), error);
        return false;
    }
    const QSize size = state.size;
    for (int index = 0; index < std::size(kCoverResolutionPresets); ++index) {
        if (QSize(kCoverResolutionPresets[index].width, kCoverResolutionPresets[index].height) == size) {
            resolutionIndex_ = index;
            break;
        }
    }
    const QJsonObject background = state.background;
    const QString mode = background.value(QStringLiteral("mode")).toString(QStringLiteral("jacket"));
    backgroundMode_ = mode == QStringLiteral("custom") ? miacode::cover_export::CoverBackgroundMode::Custom
                    : mode == QStringLiteral("transparent") ? miacode::cover_export::CoverBackgroundMode::Transparent
                                                              : miacode::cover_export::CoverBackgroundMode::Jacket;
    backgroundPath_ = background.value(QStringLiteral("customPath")).toString();
    if (backgroundMode_ == miacode::cover_export::CoverBackgroundMode::Custom
        && !QFileInfo::exists(backgroundPath_)) {
        backgroundMode_ = miacode::cover_export::CoverBackgroundMode::Jacket;
        if (reportErrors) {
            notifyError(UiText::text(QStringLiteral("cover.background")),
                        UiText::text(QStringLiteral("cover.the_custom_background_image_was")));
        }
    }
    blurBackground_ = background.value(QStringLiteral("blur")).toBool(true);
    backgroundBrightness_ = qBound(0.0, background.value(QStringLiteral("brightness")).toDouble(0.45), 1.0);
    const QJsonObject card = state.card;
    cardMode_ = card.value(QStringLiteral("mode")).toString(QStringLiteral("auto"));
    cardShadow_ = card.value(QStringLiteral("shadow")).toBool(false);
    levelTextRender_ = card.value(QStringLiteral("levelTextRender")).toBool(false);
    longTextMode_ = card.value(QStringLiteral("longText")).toString(QStringLiteral("shrink"));
    cardFontDisplayPath_ = card.value(QStringLiteral("fontDisplay")).toString();
    cardFontBodyPath_ = card.value(QStringLiteral("fontBody")).toString();
    // Presets and pre-2026-08-31 layouts carry no folder; keep the current one
    // rather than blanking the field.
    if (const QString savedOutput = state.outputDirectory.trimmed(); !savedOutput.isEmpty()) {
        outputDirectory_ = savedOutput;
    }
    if (layout_ != nullptr) {
        layout_->fromJson(state.layout);
        for (auto* frame : layout_->chartFrameLayers()) {
            layout_->clearLayerImage(frame->key());
        }
        if (!chartFrameAvailable_) {
            for (auto* frame : layout_->chartFrameLayers()) {
                frame->setVisible(false);
            }
        } else if (renderChartFrames) {
            bool framesReady = true;
            for (auto* frame : layout_->visibleChartFrameLayers()) {
                framesReady = renderChartFrame(frame, 0, reportErrors) && framesReady;
            }
            if (!framesReady) {
                return false;
            }
        }
        const auto visibleChartFrames = layout_->visibleChartFrameLayers();
        const QString firstVisibleFrameKey = !visibleChartFrames.isEmpty()
            ? visibleChartFrames.constFirst()->key() : QString();
        if (!firstVisibleFrameKey.isEmpty()) {
            activeLayerKey_ = firstVisibleFrameKey;
        } else {
            activeLayerKey_ = miacode::cover_export::CoverLayoutModel::cardKey();
        }
    } else {
        activeLayerKey_ = miacode::cover_export::CoverLayoutModel::cardKey();
    }
    if (playback_ != nullptr) {
        playback_->pause();
        playback_->cancelInput();
    }
    emit activeLayerChanged();
    syncPlaybackFromActiveLayer();
    rebindLiveChartScene();
    emit activeChartFrameSecondsChanged();
    emit inputsChanged();
    emit outputChanged();
    return true;
}

bool QmlCoverExportSession::applyCompositionJson(const QJsonObject& root, bool reportErrors)
{
    // Applying a layout also swaps the live chart-frame stills. Keep the old
    // composition and images until every new frame has rendered successfully;
    // a transient Quick capture failure must not leave the editor half-applied.
    const QJsonObject previous = compositionJson();
    const QString previousActiveLayerKey = activeLayerKey_;
    QHash<QString, QImage> previousFrameImages;
    if (layout_ != nullptr) {
        for (auto* frame : layout_->chartFrameLayers()) {
            previousFrameImages.insert(frame->key(), frame->frameImage());
        }
    }

    if (applyCompositionJsonInternal(root, reportErrors, true)) {
        return true;
    }

    if (!previous.isEmpty()) {
        applyCompositionJsonInternal(previous, false, false);
        if (layout_ != nullptr) {
            for (auto* frame : layout_->chartFrameLayers()) {
                const QImage image = previousFrameImages.value(frame->key());
                if (!image.isNull()) {
                    layout_->setLayerImage(frame->key(), image);
                }
            }
            if (layout_->layer(previousActiveLayerKey) != nullptr) {
                activeLayerKey_ = previousActiveLayerKey;
                emit activeLayerChanged();
                syncPlaybackFromActiveLayer();
                rebindLiveChartScene();
                emit activeChartFrameSecondsChanged();
            }
        }
    }
    return false;
}

void QmlCoverExportSession::persistComposition()
{
    miacode::cover_export::CoverCompositionState::savePreferences(compositionJson());
}

void QmlCoverExportSession::saveLayout()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("cover.save_cover_layout"));
    request.startPath = QStringLiteral("cover-layout.miacover");
    request.nameFilters = {UiText::text(QStringLiteral("cover.cover_layout_miacover"))};
    request.saveMode = true;
    uiRequests_->requestFile(request, [this](QString path) {
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".miacover"), Qt::CaseInsensitive)) path += QStringLiteral(".miacover");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(QJsonDocument(sharedCompositionJson()).toJson(QJsonDocument::Indented)) < 0) {
            notifyError(UiText::text(QStringLiteral("cover.save_layout_2")),
                        UiText::text(QStringLiteral("cover.could_not_write_the_layout")), path);
            return;
        }
        miacode::cover_export::CoverCompositionState::pushRecentFile(path);
        refreshSavedLists();
    });
}

void QmlCoverExportSession::importLayout()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("cover.import_cover_layout"));
    request.nameFilters = {UiText::text(QStringLiteral("cover.cover_layout_miacover_legacy_json"))};
    uiRequests_->requestFile(request, [this](const QString& path) { openRecentLayout(path); });
}

void QmlCoverExportSession::openRecentLayout(const QString& path)
{
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        notifyError(UiText::text(QStringLiteral("cover.import_layout_2")),
                    UiText::text(QStringLiteral("cover.could_not_read_the_layout")), path);
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() || document.object().value(QStringLiteral("kind")).toString()
                                    != QStringLiteral("miacode-cover-composition")) {
        notifyError(UiText::text(QStringLiteral("cover.import_layout_2")),
                    UiText::text(QStringLiteral("cover.this_file_is_not_a")), path);
        return;
    }
    if (applyCompositionJson(document.object(), true)) {
        miacode::cover_export::CoverCompositionState::pushRecentFile(path);
        persistComposition();
        refreshSavedLists();
    }
}

void QmlCoverExportSession::refreshSavedLists()
{
    recentLayoutFiles_ = miacode::cover_export::CoverCompositionState::loadRecentFiles();
    presets_.clear();
    for (const auto& preset : miacode::cover_export::CoverCompositionState::loadUserPresets()) {
        presets_.append(QVariantMap{{QStringLiteral("name"), preset.name}});
    }
    emit recentLayoutFilesChanged();
    emit presetsChanged();
}

void QmlCoverExportSession::clearRecentLayouts()
{
    miacode::cover_export::CoverCompositionState::clearRecentFiles();
    refreshSavedLists();
}
void QmlCoverExportSession::savePreset(const QString& name)
{
    miacode::cover_export::CoverCompositionState::saveUserPreset(name, presetCompositionJson());
    refreshSavedLists();
}

void QmlCoverExportSession::applyBuiltinPreset(const QString& id)
{
    if (id != QStringLiteral("card") && !chartFrameAvailable_) {
        notifyError(UiText::text(QStringLiteral("cover.apply_preset")),
                    UiText::text(QStringLiteral("cover.this_preset_needs_a_renderable")));
        return;
    }
    const QJsonObject composition = builtinPresetComposition(id);
    if (!composition.isEmpty() && applyCompositionJson(composition, true)) {
        persistComposition();
    }
}

void QmlCoverExportSession::renamePreset(const QString& oldName, const QString& newName)
{
    const QString trimmedOld = oldName.trimmed();
    const QString trimmedNew = newName.trimmed();
    if (trimmedOld.isEmpty() || trimmedNew.isEmpty() || trimmedOld == trimmedNew) {
        return;
    }
    miacode::cover_export::CoverCompositionState::renameUserPreset(trimmedOld, trimmedNew);
    refreshSavedLists();
}
void QmlCoverExportSession::applyPreset(const QString& name)
{
    for (const auto& preset : miacode::cover_export::CoverCompositionState::loadUserPresets()) {
        if (preset.name == name && applyCompositionJson(preset.composition, true)) {
            persistComposition();
            return;
        }
    }
}
void QmlCoverExportSession::removePreset(const QString& name)
{
    miacode::cover_export::CoverCompositionState::removeUserPreset(name);
    refreshSavedLists();
}

void QmlCoverExportSession::browseOutputDirectory()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("net.choose_output_directory"));
    request.startPath = outputDirectory_;
    request.selectFolder = true;
    uiRequests_->requestFile(request, [this](const QString& path) { setOutputDirectory(path); });
}

miacode::cover_export::CoverComposerInputs QmlCoverExportSession::buildInputs() const
{
    miacode::cover_export::CoverComposerInputs inputs;
    inputs.templateMap = templateMap();
    inputs.trackOverrides = trackOverrides();
    inputs.jacketPath = task_.intro.jacketPath;
    inputs.backgroundPath = backgroundPath_;
    inputs.backgroundMode = backgroundMode_;
    inputs.blurBackground = blurBackground_;
    inputs.coverBgBrightness = backgroundBrightness_;
    inputs.cardShadow = cardShadow_;
    if (const auto* layer = activeCoverLayer(); layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        inputs.chartFrameBackground = layer->frameBgMode() == QStringLiteral("image");
        inputs.chartFrameBgBrightness = layer->frameBgBrightness();
        inputs.chartFrameBgTransparency = layer->frameBgTransparency();
    }
    inputs.chartFrameDiskDiameter = chartFrameDiskDiameter();
    return inputs;
}

void QmlCoverExportSession::exportCover()
{
    if (layout_ == nullptr || busy_) {
        return;
    }
    // v1's onExportCover refused the same way before it ever opened the
    // Widgets cover dialog; the QML route dropped the user-visible half of
    // that guard and just emitted regardless.
    if (!containsDifficulty(selectedDifficultyId_)) {
        notifyError(UiText::text(QStringLiteral("cover.export_cover")),
                    UiText::text(QStringLiteral("cover.no_difficulty_selected")));
        return;
    }
    if (outputDirectory_.isEmpty()) {
        notifyError(UiText::text(QStringLiteral("cover.export_cover")),
                    UiText::text(QStringLiteral("cover.no_output_directory")));
        return;
    }

    // The coordinator is the single playback authority now; a preview left
    // running under the synchronous render below would keep its audio going
    // under a frozen UI. Only toggle when it is actually playing —
    // togglePlayback() is a toggle, so calling it on a paused/stopped preview
    // would start it instead.
    if (miacode::v2::PlaybackControl* const control = playbackControl(); control != nullptr
        && control->playbackSnapshot().transportState == miacode::v2::PlaybackTransportState::Playing) {
        control->togglePlayback();
    }

    setBusy(true);
    // renderVisibleChartFramesForExport()/exportCoverComposite() below are
    // synchronous in-process QSG work with no progress callback, so nothing
    // yields back to the event loop once they start. Give it one turn here,
    // purely so CoverExportPage's BusyIndicator gets to paint before the
    // render blocks the UI thread.
    QCoreApplication::processEvents();
    const auto plan = miacode::cover_export::CoverFrameExportPlan::fromVisibleLayers(
        *layout_, activeLayerKey_, activeChartFrameSeconds());
    const bool wasPlaying = chartFramePlaying();
    const QString savedActiveKey = plan.activeLayerKey();
    const double savedActiveSeconds = plan.activeLayerSeconds();
    playback_->pause();
    playback_->cancelInput();
    commitActiveLayerFrameSeconds();
    stopAndDetachLiveChartScene();
    const int frameSide = qBound(512, qMax(outputWidth(), outputHeight()), 4096);
    const bool framesReady = renderVisibleChartFramesForExport(frameSide);
    persistComposition();
    const auto result = framesReady
        ? miacode::cover_export::exportCoverComposite(
              layout_.get(), buildInputs(), QSize(outputWidth(), outputHeight()), outputDirectory_)
        : miacode::cover_export::CoverExportResult{
              false, QString(), QStringLiteral("could not render one or more chart frames")};
    if (layout_->layer(savedActiveKey) != nullptr) {
        activeLayerKey_ = savedActiveKey;
        emit activeLayerChanged();
    }
    if (isActiveChartFrame(activeCoverLayer())) {
        previewActiveLayerFrameSeconds(savedActiveSeconds);
        commitActiveLayerFrameSeconds();
    }
    syncPlaybackFromActiveLayer();
    rebindLiveChartScene();
    if (wasPlaying && isActiveChartFrame(activeCoverLayer())) {
        playback_->play();
    }
    setBusy(false);
    if (!result.success) {
        notifyError(UiText::text(QStringLiteral("cover.export_cover")),
                    UiText::text(QStringLiteral("cover.cover_export_failed_1")).arg(result.errorMessage),
                    result.errorMessage);
        return;
    }
    uiRequests_->postNotice(miacode::v2::NoticeSeverity::Information,
                            UiText::text(QStringLiteral("cover.export_cover")),
                            UiText::text(QStringLiteral("cover.cover_export_completed")),
                            result.outputPath);
}

void QmlCoverExportSession::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged();
}

void QmlCoverExportSession::notifyError(const QString& title, const QString& text, const QString& details) const
{
    if (uiRequests_ != nullptr) {
        uiRequests_->postNotice(miacode::v2::NoticeSeverity::Error, title, text, details);
    }
}
