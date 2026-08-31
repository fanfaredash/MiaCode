#pragma once

#include "app/v2/UiRequestService.h"
#include "tools/cover_export/CoverCompositeRenderer.h"
#include "tools/video_export/VideoExportController.h"

#include <QObject>
#include <QJsonObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <iterator>

class QmlExportSession;

namespace miacode::cover_export {
class CoverLayoutModel;
class CoverLayer;
class SceneFrameRenderer;
}

// The v2 cover-export session owns no QWidget. It projects the reusable cover
// layout model into CoverExportPage.qml and delegates all file/user feedback to
// UiRequestService, while the final image stays an in-process Quick render.
class QmlCoverExportSession final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* uiRequests READ uiRequests CONSTANT)
    Q_PROPERTY(bool pageSessionActive READ pageSessionActive NOTIFY pageSessionActiveChanged)
    Q_PROPERTY(int selectedDifficultyId READ selectedDifficultyId NOTIFY selectedDifficultyIdChanged)
    Q_PROPERTY(QVariantList difficulties READ difficulties NOTIFY difficultiesChanged)
    Q_PROPERTY(QObject* layoutModel READ layoutModel CONSTANT)
    Q_PROPERTY(QObject* activeLayer READ activeLayer NOTIFY activeLayerChanged)
    Q_PROPERTY(QString activeLayerKey READ activeLayerKey NOTIFY activeLayerChanged)
    Q_PROPERTY(QVariantMap templateMap READ templateMap NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap trackOverrides READ trackOverrides NOTIFY inputsChanged)
    Q_PROPERTY(QUrl jacketImage READ jacketImage NOTIFY inputsChanged)
    Q_PROPERTY(QUrl backgroundImage READ backgroundImage NOTIFY inputsChanged)
    Q_PROPERTY(int backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY inputsChanged)
    Q_PROPERTY(bool blurBackground READ blurBackground WRITE setBlurBackground NOTIFY inputsChanged)
    Q_PROPERTY(double backgroundBrightness READ backgroundBrightness WRITE setBackgroundBrightness NOTIFY inputsChanged)
    Q_PROPERTY(bool cardShadow READ cardShadow WRITE setCardShadow NOTIFY inputsChanged)
    Q_PROPERTY(QString cardMode READ cardMode WRITE setCardMode NOTIFY inputsChanged)
    Q_PROPERTY(bool levelTextRender READ levelTextRender WRITE setLevelTextRender NOTIFY inputsChanged)
    Q_PROPERTY(QString longTextMode READ longTextMode WRITE setLongTextMode NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList fontLibraryOptions READ fontLibraryOptions NOTIFY fontLibraryChanged)
    Q_PROPERTY(QString cardFontDisplayPath READ cardFontDisplayPath WRITE setCardFontDisplayPath NOTIFY inputsChanged)
    Q_PROPERTY(QString cardFontBodyPath READ cardFontBodyPath WRITE setCardFontBodyPath NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList resolutionOptions READ resolutionOptions CONSTANT)
    Q_PROPERTY(int resolutionIndex READ resolutionIndex WRITE setResolutionIndex NOTIFY outputChanged)
    Q_PROPERTY(int outputWidth READ outputWidth NOTIFY outputChanged)
    Q_PROPERTY(int outputHeight READ outputHeight NOTIFY outputChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputChanged)
    Q_PROPERTY(bool chartFrameAvailable READ chartFrameAvailable NOTIFY chartFrameAvailabilityChanged)
    Q_PROPERTY(double chartFrameDuration READ chartFrameDuration NOTIFY chartFrameAvailabilityChanged)
    Q_PROPERTY(double chartFrameDiskDiameter READ chartFrameDiskDiameter NOTIFY chartFrameAvailabilityChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QStringList recentLayoutFiles READ recentLayoutFiles NOTIFY recentLayoutFilesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    QmlCoverExportSession(QmlExportSession& exportSession,
                          miacode::v2::UiRequestService& uiRequests,
                          QObject* parent = nullptr);
    ~QmlCoverExportSession() override;

    QObject* uiRequests() const;
    bool pageSessionActive() const { return pageSessionActive_; }
    int selectedDifficultyId() const { return selectedDifficultyId_; }
    QVariantList difficulties() const { return difficulties_; }
    QObject* layoutModel() const;
    // Typed hand-off for the application QML engine's `coverchart` image
    // provider, which serves the chart-frame stills the page displays. The
    // QML-facing property above stays QObject* for the page's bindings.
    miacode::cover_export::CoverLayoutModel* coverLayout() const { return layout_.get(); }
    QObject* activeLayer() const;
    QString activeLayerKey() const { return activeLayerKey_; }
    QVariantMap templateMap() const;
    QVariantMap trackOverrides() const;
    QUrl jacketImage() const;
    QUrl backgroundImage() const;
    int backgroundMode() const;
    bool blurBackground() const { return blurBackground_; }
    double backgroundBrightness() const { return backgroundBrightness_; }
    bool cardShadow() const { return cardShadow_; }
    QString cardMode() const { return cardMode_; }
    bool levelTextRender() const { return levelTextRender_; }
    QString longTextMode() const { return longTextMode_; }
    QVariantList fontLibraryOptions() const;
    QString cardFontDisplayPath() const { return cardFontDisplayPath_; }
    QString cardFontBodyPath() const { return cardFontBodyPath_; }
    QVariantList resolutionOptions() const;
    int resolutionIndex() const { return resolutionIndex_; }
    int outputWidth() const;
    int outputHeight() const;
    QString outputDirectory() const { return outputDirectory_; }
    bool chartFrameAvailable() const { return chartFrameAvailable_; }
    double chartFrameDuration() const { return chartFrameDuration_; }
    double chartFrameDiskDiameter() const;
    QVariantList presets() const { return presets_; }
    QStringList recentLayoutFiles() const { return recentLayoutFiles_; }
    bool busy() const { return busy_; }

    void enter(int preferredDifficultyId);
    void leave();

    Q_INVOKABLE void selectDifficulty(int difficultyId);
    Q_INVOKABLE void selectLayerKey(const QString& key);
    Q_INVOKABLE void addChartFrameLayer();
    Q_INVOKABLE void addImageLayer();
    Q_INVOKABLE void addTextLayer();
    Q_INVOKABLE void duplicateActiveLayer();
    Q_INVOKABLE void removeActiveLayer();
    Q_INVOKABLE void bringActiveLayerToFront();
    Q_INVOKABLE void sendActiveLayerToBack();
    Q_INVOKABLE void raiseActiveLayer();
    Q_INVOKABLE void lowerActiveLayer();
    Q_INVOKABLE void browseActiveLayerImage();
    Q_INVOKABLE void importActiveLayerFont();
    Q_INVOKABLE void setActiveLayerVisible(bool visible);
    Q_INVOKABLE void setActiveLayerLocked(bool locked);
    Q_INVOKABLE void setActiveLayerOpacity(double opacity);
    Q_INVOKABLE void setActiveLayerSizeFraction(double sizeFraction);
    Q_INVOKABLE void setActiveLayerCenter(double nx, double ny);
    Q_INVOKABLE void setActiveLayerText(const QString& text);
    Q_INVOKABLE void setActiveLayerTextColor(const QString& color);
    Q_INVOKABLE void setActiveLayerTextBold(bool bold);
    Q_INVOKABLE void setActiveLayerFrameSeconds(double seconds);
    Q_INVOKABLE void setActiveLayerFrameBackgroundMode(const QString& mode);
    Q_INVOKABLE void setActiveLayerFrameBackgroundBrightness(double brightness);
    Q_INVOKABLE void setActiveLayerFrameBackgroundTransparency(double transparency);
    Q_INVOKABLE void browseBackgroundImage();
    Q_INVOKABLE void importCardDisplayFont();
    Q_INVOKABLE void importCardBodyFont();
    Q_INVOKABLE void resetLayout();
    Q_INVOKABLE void saveLayout();
    Q_INVOKABLE void importLayout();
    Q_INVOKABLE void openRecentLayout(const QString& path);
    Q_INVOKABLE void clearRecentLayouts();
    Q_INVOKABLE void savePreset(const QString& name);
    Q_INVOKABLE void applyPreset(const QString& name);
    Q_INVOKABLE void removePreset(const QString& name);
    Q_INVOKABLE void browseOutputDirectory();
    Q_INVOKABLE void exportCover();

    void setBackgroundMode(int mode);
    void setBlurBackground(bool enabled);
    void setBackgroundBrightness(double value);
    void setCardShadow(bool enabled);
    void setCardMode(const QString& mode);
    void setLevelTextRender(bool enabled);
    void setLongTextMode(const QString& mode);
    void setCardFontDisplayPath(const QString& path);
    void setCardFontBodyPath(const QString& path);
    void setResolutionIndex(int index);
    void setOutputDirectory(const QString& path);

signals:
    void pageSessionActiveChanged();
    void selectedDifficultyIdChanged();
    void difficultiesChanged();
    void activeLayerChanged();
    void inputsChanged();
    void fontLibraryChanged();
    void outputChanged();
    void chartFrameAvailabilityChanged();
    void presetsChanged();
    void recentLayoutFilesChanged();
    void busyChanged();

private:
    miacode::cover_export::CoverLayer* activeCoverLayer() const;
    bool containsDifficulty(int difficultyId) const;
    int defaultDifficultyId(int preferredDifficultyId) const;
    void seedFromDifficulty(int difficultyId);
    void rebuildFromExportSession();
    void refreshSavedLists();
    void renderChartFrame(miacode::cover_export::CoverLayer* layer, int sidePx = 0);
    miacode::cover_export::CoverComposerInputs buildInputs() const;
    QJsonObject compositionJson() const;
    QJsonObject sharedCompositionJson() const;
    QJsonObject presetCompositionJson() const;
    bool applyCompositionJson(const QJsonObject& root, bool reportErrors);
    void persistComposition();
    void requestFont(bool displayFont, bool textLayerFont);
    void setBusy(bool busy);
    void notifyError(const QString& title, const QString& text, const QString& details = QString()) const;

    QmlExportSession* exportSession_ = nullptr;
    miacode::v2::UiRequestService* uiRequests_ = nullptr;
    std::unique_ptr<miacode::cover_export::CoverLayoutModel> layout_;
    std::unique_ptr<miacode::cover_export::SceneFrameRenderer> frameRenderer_;
    VideoExportTask task_;
    QVariantMap bannerTemplate_;
    QVariantList difficulties_;
    QVariantList presets_;
    QStringList recentLayoutFiles_;
    QString activeLayerKey_;
    QString outputDirectory_;
    QString backgroundPath_;
    QString cardMode_ = QStringLiteral("auto");
    QString longTextMode_ = QStringLiteral("shrink");
    QString cardFontDisplayPath_;
    QString cardFontBodyPath_;
    int selectedDifficultyId_ = 0;
    int resolutionIndex_ = 4;
    bool pageSessionActive_ = false;
    bool hasLoadedPreferences_ = false;
    bool blurBackground_ = true;
    bool cardShadow_ = false;
    bool levelTextRender_ = false;
    bool chartFrameAvailable_ = false;
    bool busy_ = false;
    double backgroundBrightness_ = 0.45;
    double chartFrameDuration_ = 0.0;
    miacode::cover_export::CoverBackgroundMode backgroundMode_ =
        miacode::cover_export::CoverBackgroundMode::Jacket;
};
