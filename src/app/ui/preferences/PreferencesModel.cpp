#include "preferences/PreferencesModel.h"

#include "layout/WorkbenchSettings.h"
#include "app/services/PreferencesStore.h"
#include "runtime/Shared.h"
#include "ui/preferences/LocaleService.h"
#include "ui/preferences/PreferenceDocument.h"

#include <QVariantMap>
#include <QCoreApplication>

namespace miacode::ui {

namespace {

QVariantMap option(const QVariant& value, const QString& label)
{
    QVariantMap row;
    row.insert(QStringLiteral("value"), value);
    row.insert(QStringLiteral("label"), label);
    return row;
}

}  // namespace

PreferencesModel::PreferencesModel(miacode::PreferencesStore*& storeSlot,
                                         WorkbenchSettings& settings, QObject* parent)
    : QObject(parent)
    , storeSlot_(&storeSlot)
    , settings_(&settings)
{
    connect(&miacode::LocaleService::instance(), &miacode::LocaleService::languageChanged,
            this, [this](const QString&) {
                emit interfaceChanged();
                emit performanceChanged();
            });
}

QVariantList PreferencesModel::languageOptions() const
{
    QVariantList rows;
    // Extensions can add language packs, so this is read fresh rather than
    // cached — the Widgets dialog rebuilt its combo for the same reason.
    for (const PreferenceDocument::LanguageOption& available : PreferenceDocument::availableLanguageOptions()) {
        rows.append(option(available.id, available.label));
    }
    return rows;
}

QString PreferencesModel::languageToken() const
{
    return PreferenceDocument::preferredLanguageToken();
}

void PreferencesModel::setLanguageToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty() || normalized == PreferenceDocument::preferredLanguageToken()) {
        return;
    }
    miacode::LocaleService::instance().setLanguageToken(normalized);
    emit interfaceChanged();
}

QVariantList PreferencesModel::themeOptions() const
{
    return themeModeOptions();
}

QVariantList PreferencesModel::themeModeOptions() const
{
    return QVariantList{
        option(QStringLiteral("system"), qtTrId("dialog.preferences.theme.system")),
        option(QStringLiteral("light"), qtTrId("dialog.preferences.theme.light")),
        option(QStringLiteral("dark"), qtTrId("dialog.preferences.theme.dark")),
    };
}

QString PreferencesModel::themeToken() const
{
    return themeModeToken();
}

QString PreferencesModel::themeModeToken() const
{
    return settings_ != nullptr ? settings_->themeModeToken() : QStringLiteral("system");
}

void PreferencesModel::setThemeToken(const QString& token)
{
    setThemeModeToken(token);
}

void PreferencesModel::setThemeModeToken(const QString& token)
{
    if (settings_ == nullptr || token.trimmed().toLower() == themeModeToken()) {
        return;
    }
    settings_->setThemeModeToken(token);
    restartRequired_ = true;
    emit interfaceChanged();
}

QVariantList PreferencesModel::themePaletteOptions() const
{
    return QVariantList{
        option(QStringLiteral("dark"), qtTrId("dialog.preferences.theme.dark")),
        option(QStringLiteral("light"), qtTrId("dialog.preferences.theme.light")),
        option(QStringLiteral("legacy"), qtTrId("dialog.preferences.theme.legacy")),
        option(QStringLiteral("legacy_light"), qtTrId("dialog.preferences.theme.legacy_light")),
    };
}

QString PreferencesModel::lightThemeToken() const
{
    return settings_ != nullptr ? settings_->lightThemeToken() : QStringLiteral("light");
}

void PreferencesModel::setLightThemeToken(const QString& token)
{
    if (settings_ == nullptr || token.trimmed().toLower() == lightThemeToken()) {
        return;
    }
    settings_->setLightThemeToken(token);
    restartRequired_ = true;
    emit interfaceChanged();
}

QString PreferencesModel::darkThemeToken() const
{
    return settings_ != nullptr ? settings_->darkThemeToken() : QStringLiteral("dark");
}

void PreferencesModel::setDarkThemeToken(const QString& token)
{
    if (settings_ == nullptr || token.trimmed().toLower() == darkThemeToken()) {
        return;
    }
    settings_->setDarkThemeToken(token);
    restartRequired_ = true;
    emit interfaceChanged();
}

bool PreferencesModel::previewOnLeft() const
{
    return store() != nullptr && store()->workspacePanelsSwapped();
}

void PreferencesModel::setPreviewOnLeft(bool onLeft)
{
    if (store() == nullptr || onLeft == previewOnLeft()) {
        return;
    }
    store()->setWorkspacePanelsSwapped(onLeft, true);
    emit interfaceChanged();
}

int PreferencesModel::editorFontSize() const
{
    return store() != nullptr ? store()->editorTextFontSize() : 0;
}

void PreferencesModel::setEditorFontSize(int pointSize)
{
    if (store() == nullptr) {
        return;
    }
    const int clamped = qBound(kEditorFontSizeMinimum, pointSize, kEditorFontSizeMaximum);
    if (clamped == editorFontSize()) {
        return;
    }
    store()->applyEditorTextFontSize(clamped, true);
    emit editorChanged();
}

QVariantList PreferencesModel::lineSpacingOptions() const
{
    QVariantList rows;
    for (double factor : miacode::runtime::shared::kEditorLineSpacingFactorOptions) {
        rows.append(option(
            factor, miacode::runtime::shared::editorLineSpacingFactorLabel(factor)));
    }
    return rows;
}

double PreferencesModel::editorLineSpacing() const
{
    return store() != nullptr
        ? store()->editorLineSpacingFactor()
        : miacode::runtime::shared::kEditorLineSpacingFactorDefault;
}

void PreferencesModel::setEditorLineSpacing(double factor)
{
    if (store() == nullptr || qFuzzyCompare(factor, editorLineSpacing())) {
        return;
    }
    store()->applyEditorLineSpacingFactor(factor, true);
    emit editorChanged();
}

bool PreferencesModel::editorAutoCompletion() const
{
    return store() != nullptr && store()->editorAutoCompletionEnabled();
}

void PreferencesModel::setEditorAutoCompletion(bool enabled)
{
    if (store() == nullptr || enabled == editorAutoCompletion()) {
        return;
    }
    store()->applyEditorAutoCompletionEnabled(enabled, true);
    emit editorChanged();
}

int PreferencesModel::editorInputHandlingMode() const
{
    if (editorImeDisabled()) {
        return BlockInputMethodsAndCorrectFullWidth;
    }
    return editorHalfWidthInput() ? CorrectFullWidthOnly : LeaveInputUnchanged;
}

void PreferencesModel::setEditorInputHandlingMode(int mode)
{
    if (store() == nullptr || mode < CorrectFullWidthOnly || mode > LeaveInputUnchanged) {
        return;
    }
    const bool correctFullWidth = mode != LeaveInputUnchanged;
    const bool blockInputMethods = mode == BlockInputMethodsAndCorrectFullWidth;
    if (editorHalfWidthInput() == correctFullWidth
        && editorImeDisabled() == blockInputMethods) {
        return;
    }
    store()->applyEditorHalfWidthInputEnabled(correctFullWidth, false);
    store()->applyEditorImeInputDisabled(blockInputMethods, true);
    emit editorChanged();
}

bool PreferencesModel::editorHalfWidthInput() const
{
    return store() != nullptr && store()->editorHalfWidthInputEnabled();
}

void PreferencesModel::setEditorHalfWidthInput(bool enabled)
{
    if (store() == nullptr || enabled == editorHalfWidthInput()) {
        return;
    }
    store()->applyEditorHalfWidthInputEnabled(enabled, true);
    emit editorChanged();
}

bool PreferencesModel::editorImeDisabled() const
{
    return store() != nullptr && store()->editorImeInputDisabled();
}

void PreferencesModel::setEditorImeDisabled(bool disabled)
{
    if (store() == nullptr || disabled == editorImeDisabled()) {
        return;
    }
    store()->applyEditorImeInputDisabled(disabled, true);
    emit editorChanged();
}

bool PreferencesModel::editorScrollPastEnd() const
{
    return settings_ != nullptr && settings_->editorScrollPastEnd();
}

void PreferencesModel::setEditorScrollPastEnd(bool enabled)
{
    if (settings_ == nullptr || enabled == editorScrollPastEnd()) {
        return;
    }
    settings_->setEditorScrollPastEnd(enabled);
    emit editorChanged();
}

bool PreferencesModel::editorSelectionBeatDisplay() const
{
    return settings_ != nullptr && settings_->editorSelectionBeatDisplay();
}

void PreferencesModel::setEditorSelectionBeatDisplay(bool enabled)
{
    if (settings_ == nullptr || enabled == editorSelectionBeatDisplay()) {
        return;
    }
    settings_->setEditorSelectionBeatDisplay(enabled);
    emit editorChanged();
}

bool PreferencesModel::videoDecodePrefersSoftware() const
{
    return store() != nullptr && store()->videoDecodePrefersSoftware();
}

void PreferencesModel::setVideoDecodePrefersSoftware(bool preferSoftware)
{
    if (store() == nullptr || preferSoftware == videoDecodePrefersSoftware()) {
        return;
    }
    store()->setVideoDecodePrefersSoftware(preferSoftware, true);
    emit performanceChanged();
}

double PreferencesModel::displayRefreshRate() const
{
    return store() != nullptr ? store()->previewCanvasRefreshRate() : 0.0;
}

QVariantList PreferencesModel::frameRateOptions(bool includeDisplayRefresh) const
{
    QVariantList rows;
    // Read once: also gates the Fps120 option below (v1 parity, d5a604b7),
    // not just the DisplayRefresh label.
    const double refresh = displayRefreshRate();
    if (includeDisplayRefresh) {
        rows.append(option(
            static_cast<int>(PreviewCanvasFrameRateMode::DisplayRefresh),
            refresh > 0.0
                ? QStringLiteral("%1 (%2 Hz)")
                      .arg(qtTrId("dialog.render_settings.preview.canvas_frame_rate.display"))
                      .arg(refresh, 0, 'f', refresh >= 100.0 ? 0 : 1)
                : qtTrId("dialog.render_settings.preview.canvas_frame_rate.display")));
    }
    rows.append(option(
        static_cast<int>(PreviewCanvasFrameRateMode::Fps30),
        qtTrId("dialog.render_settings.preview.canvas_frame_rate.30")));
    rows.append(option(
        static_cast<int>(PreviewCanvasFrameRateMode::Fps60),
        qtTrId("dialog.render_settings.preview.canvas_frame_rate.60")));
    // Only expose 120 FPS on a display that can sustain it. The backend
    // clamps Fps120 to display refresh at runtime (see
    // PlaybackCoordinator::previewCanvasTargetFrameIntervalNs), so leaving
    // it in the menu on a sub-120 Hz panel would advertise a setting that
    // silently degrades to display refresh. Epsilon (119.5) tolerates
    // panels that report 119.88 Hz (common OEM round-down of true 120 Hz).
    if (refresh >= 119.5) {
        rows.append(option(
            static_cast<int>(PreviewCanvasFrameRateMode::Fps120),
            qtTrId("dialog.render_settings.preview.canvas_frame_rate.120")));
    }
    return rows;
}

QVariantList PreferencesModel::canvasFrameRateOptions() const
{
    return frameRateOptions(/*includeDisplayRefresh=*/true);
}

QVariantList PreferencesModel::appFrameRateOptions() const
{
    return frameRateOptions(/*includeDisplayRefresh=*/true);
}

int PreferencesModel::canvasFrameRateMode() const
{
    return store() != nullptr ? static_cast<int>(store()->previewCanvasFrameRateMode()) : 0;
}

void PreferencesModel::setCanvasFrameRateMode(int mode)
{
    if (store() == nullptr || mode == canvasFrameRateMode()) {
        return;
    }
    store()->setPreviewCanvasFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int PreferencesModel::stageMediaFrameRateMode() const
{
    return store() != nullptr
        ? static_cast<int>(store()->previewStageMediaFrameRateMode())
        : 0;
}

void PreferencesModel::setStageMediaFrameRateMode(int mode)
{
    if (store() == nullptr || mode == stageMediaFrameRateMode()) {
        return;
    }
    store()->setPreviewStageMediaFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int PreferencesModel::timelineFrameRateMode() const
{
    return store() != nullptr ? static_cast<int>(store()->timelineFrameRateMode()) : 0;
}

void PreferencesModel::setTimelineFrameRateMode(int mode)
{
    if (store() == nullptr || mode == timelineFrameRateMode()) {
        return;
    }
    store()->setTimelineFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

}  // namespace miacode::ui
