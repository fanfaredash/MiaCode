#include "QmlPreferencesModel.h"

#include "../QmlUiSettings.h"
#include "runtime/Shared.h"
#include "ui/UiText.h"

#include <QVariantMap>

namespace miacode::qml_ui {

namespace {

QVariantMap option(const QVariant& value, const QString& label)
{
    QVariantMap row;
    row.insert(QStringLiteral("value"), value);
    row.insert(QStringLiteral("label"), label);
    return row;
}

}  // namespace

QmlPreferencesModel::QmlPreferencesModel(miacode::v2::PreferencesStore*& storeSlot,
                                         QmlUiSettings& settings, QObject* parent)
    : QObject(parent)
    , storeSlot_(&storeSlot)
    , settings_(&settings)
{
}

QVariantList QmlPreferencesModel::languageOptions() const
{
    QVariantList rows;
    // Extensions can add language packs, so this is read fresh rather than
    // cached — the Widgets dialog rebuilt its combo for the same reason.
    for (const UiText::LanguageOption& available : UiText::availableLanguageOptions()) {
        rows.append(option(available.id, available.label));
    }
    return rows;
}

QString QmlPreferencesModel::languageToken() const
{
    return UiText::preferredLanguageToken();
}

void QmlPreferencesModel::setLanguageToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty() || normalized == UiText::preferredLanguageToken()) {
        return;
    }
    UiText::setPreferredLanguageToken(normalized);
    restartRequired_ = true;
    emit interfaceChanged();
}

QVariantList QmlPreferencesModel::themeOptions() const
{
    return QVariantList{
        option(QStringLiteral("system"), UiText::text(QStringLiteral("dialog.preferences.theme.system"))),
        option(QStringLiteral("light"), UiText::text(QStringLiteral("dialog.preferences.theme.light"))),
        option(QStringLiteral("dark"), UiText::text(QStringLiteral("dialog.preferences.theme.dark"))),
    };
}

QString QmlPreferencesModel::themeToken() const
{
    return settings_ != nullptr ? settings_->themeToken() : QStringLiteral("system");
}

void QmlPreferencesModel::setThemeToken(const QString& token)
{
    if (settings_ == nullptr || token.trimmed().toLower() == themeToken()) {
        return;
    }
    settings_->setThemeToken(token);
    restartRequired_ = true;
    emit interfaceChanged();
}

bool QmlPreferencesModel::previewOnLeft() const
{
    return store() != nullptr && store()->workspacePanelsSwapped();
}

void QmlPreferencesModel::setPreviewOnLeft(bool onLeft)
{
    if (store() == nullptr || onLeft == previewOnLeft()) {
        return;
    }
    store()->setWorkspacePanelsSwapped(onLeft, true);
    emit interfaceChanged();
}

int QmlPreferencesModel::editorFontSize() const
{
    return store() != nullptr ? store()->editorTextFontSize() : 0;
}

void QmlPreferencesModel::setEditorFontSize(int pointSize)
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

QVariantList QmlPreferencesModel::lineSpacingOptions() const
{
    QVariantList rows;
    for (double factor : miacode::runtime::shared::kEditorLineSpacingFactorOptions) {
        rows.append(option(
            factor, miacode::runtime::shared::editorLineSpacingFactorLabel(factor)));
    }
    return rows;
}

double QmlPreferencesModel::editorLineSpacing() const
{
    return store() != nullptr
        ? store()->editorLineSpacingFactor()
        : miacode::runtime::shared::kEditorLineSpacingFactorDefault;
}

void QmlPreferencesModel::setEditorLineSpacing(double factor)
{
    if (store() == nullptr || qFuzzyCompare(factor, editorLineSpacing())) {
        return;
    }
    store()->applyEditorLineSpacingFactor(factor, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorAutoCompletion() const
{
    return store() != nullptr && store()->editorAutoCompletionEnabled();
}

void QmlPreferencesModel::setEditorAutoCompletion(bool enabled)
{
    if (store() == nullptr || enabled == editorAutoCompletion()) {
        return;
    }
    store()->applyEditorAutoCompletionEnabled(enabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorHalfWidthInput() const
{
    return store() != nullptr && store()->editorHalfWidthInputEnabled();
}

void QmlPreferencesModel::setEditorHalfWidthInput(bool enabled)
{
    if (store() == nullptr || enabled == editorHalfWidthInput()) {
        return;
    }
    store()->applyEditorHalfWidthInputEnabled(enabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorImeDisabled() const
{
    return store() != nullptr && store()->editorImeInputDisabled();
}

void QmlPreferencesModel::setEditorImeDisabled(bool disabled)
{
    if (store() == nullptr || disabled == editorImeDisabled()) {
        return;
    }
    store()->applyEditorImeInputDisabled(disabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorScrollPastEnd() const
{
    return settings_ != nullptr && settings_->editorScrollPastEnd();
}

void QmlPreferencesModel::setEditorScrollPastEnd(bool enabled)
{
    if (settings_ == nullptr || enabled == editorScrollPastEnd()) {
        return;
    }
    settings_->setEditorScrollPastEnd(enabled);
    emit editorChanged();
}

bool QmlPreferencesModel::editorSelectionBeatDisplay() const
{
    return settings_ != nullptr && settings_->editorSelectionBeatDisplay();
}

void QmlPreferencesModel::setEditorSelectionBeatDisplay(bool enabled)
{
    if (settings_ == nullptr || enabled == editorSelectionBeatDisplay()) {
        return;
    }
    settings_->setEditorSelectionBeatDisplay(enabled);
    emit editorChanged();
}

bool QmlPreferencesModel::videoDecodePrefersSoftware() const
{
    return store() != nullptr && store()->videoDecodePrefersSoftware();
}

void QmlPreferencesModel::setVideoDecodePrefersSoftware(bool preferSoftware)
{
    if (store() == nullptr || preferSoftware == videoDecodePrefersSoftware()) {
        return;
    }
    store()->setVideoDecodePrefersSoftware(preferSoftware, true);
    emit performanceChanged();
}

double QmlPreferencesModel::displayRefreshRate() const
{
    return store() != nullptr ? store()->previewCanvasRefreshRate() : 0.0;
}

QVariantList QmlPreferencesModel::frameRateOptions(bool includeDisplayRefresh) const
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
                      .arg(UiText::text(
                          QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display")))
                      .arg(refresh, 0, 'f', refresh >= 100.0 ? 0 : 1)
                : UiText::text(
                      QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display"))));
    }
    rows.append(option(
        static_cast<int>(PreviewCanvasFrameRateMode::Fps30),
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.30"))));
    rows.append(option(
        static_cast<int>(PreviewCanvasFrameRateMode::Fps60),
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.60"))));
    // Only expose 120 FPS on a display that can sustain it. The backend
    // clamps Fps120 to display refresh at runtime (see
    // PlaybackCoordinator::previewCanvasTargetFrameIntervalNs), so leaving
    // it in the menu on a sub-120 Hz panel would advertise a setting that
    // silently degrades to display refresh. Epsilon (119.5) tolerates
    // panels that report 119.88 Hz (common OEM round-down of true 120 Hz).
    if (refresh >= 119.5) {
        rows.append(option(
            static_cast<int>(PreviewCanvasFrameRateMode::Fps120),
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.120"))));
    }
    return rows;
}

QVariantList QmlPreferencesModel::canvasFrameRateOptions() const
{
    return frameRateOptions(/*includeDisplayRefresh=*/true);
}

QVariantList QmlPreferencesModel::appFrameRateOptions() const
{
    return frameRateOptions(/*includeDisplayRefresh=*/true);
}

int QmlPreferencesModel::canvasFrameRateMode() const
{
    return store() != nullptr ? static_cast<int>(store()->previewCanvasFrameRateMode()) : 0;
}

void QmlPreferencesModel::setCanvasFrameRateMode(int mode)
{
    if (store() == nullptr || mode == canvasFrameRateMode()) {
        return;
    }
    store()->setPreviewCanvasFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int QmlPreferencesModel::stageMediaFrameRateMode() const
{
    return store() != nullptr
        ? static_cast<int>(store()->previewStageMediaFrameRateMode())
        : 0;
}

void QmlPreferencesModel::setStageMediaFrameRateMode(int mode)
{
    if (store() == nullptr || mode == stageMediaFrameRateMode()) {
        return;
    }
    store()->setPreviewStageMediaFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int QmlPreferencesModel::timelineFrameRateMode() const
{
    return store() != nullptr ? static_cast<int>(store()->timelineFrameRateMode()) : 0;
}

void QmlPreferencesModel::setTimelineFrameRateMode(int mode)
{
    if (store() == nullptr || mode == timelineFrameRateMode()) {
        return;
    }
    store()->setTimelineFrameRateMode(
        static_cast<PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

}  // namespace miacode::qml_ui
