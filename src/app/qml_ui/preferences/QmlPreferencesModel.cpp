#include "QmlPreferencesModel.h"

#include "mainwindow/MainWindow.h"
#include "../QmlUiSettings.h"
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

QmlPreferencesModel::QmlPreferencesModel(MainWindow& backend, QmlUiSettings& settings, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
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
    return backend_ != nullptr && backend_->currentWorkspacePanelsSwapped();
}

void QmlPreferencesModel::setPreviewOnLeft(bool onLeft)
{
    if (backend_ == nullptr || onLeft == previewOnLeft()) {
        return;
    }
    backend_->setWorkspacePanelsSwapped(onLeft, true);
    emit interfaceChanged();
}

int QmlPreferencesModel::editorFontSize() const
{
    return backend_ != nullptr ? backend_->currentEditorTextFontSize() : 0;
}

void QmlPreferencesModel::setEditorFontSize(int pointSize)
{
    if (backend_ == nullptr) {
        return;
    }
    const int clamped = qBound(kEditorFontSizeMinimum, pointSize, kEditorFontSizeMaximum);
    if (clamped == editorFontSize()) {
        return;
    }
    backend_->applyEditorTextFontSize(clamped, true);
    emit editorChanged();
}

QVariantList QmlPreferencesModel::lineSpacingOptions() const
{
    QVariantList rows;
    for (double factor : {1.0, 1.15, 1.3, 1.5, 1.75, 2.0}) {
        rows.append(option(factor, QStringLiteral("%1x").arg(factor, 0, 'g', 3)));
    }
    return rows;
}

double QmlPreferencesModel::editorLineSpacing() const
{
    return backend_ != nullptr ? backend_->currentEditorLineSpacingFactor() : 1.5;
}

void QmlPreferencesModel::setEditorLineSpacing(double factor)
{
    if (backend_ == nullptr || qFuzzyCompare(factor, editorLineSpacing())) {
        return;
    }
    backend_->applyEditorLineSpacingFactor(factor, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorAutoCompletion() const
{
    return backend_ != nullptr && backend_->currentEditorAutoCompletionEnabled();
}

void QmlPreferencesModel::setEditorAutoCompletion(bool enabled)
{
    if (backend_ == nullptr || enabled == editorAutoCompletion()) {
        return;
    }
    backend_->applyEditorAutoCompletionEnabled(enabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorHalfWidthInput() const
{
    return backend_ != nullptr && backend_->currentEditorHalfWidthInputEnabled();
}

void QmlPreferencesModel::setEditorHalfWidthInput(bool enabled)
{
    if (backend_ == nullptr || enabled == editorHalfWidthInput()) {
        return;
    }
    backend_->applyEditorHalfWidthInputEnabled(enabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::editorImeDisabled() const
{
    return backend_ != nullptr && backend_->currentEditorImeInputDisabled();
}

void QmlPreferencesModel::setEditorImeDisabled(bool disabled)
{
    if (backend_ == nullptr || disabled == editorImeDisabled()) {
        return;
    }
    backend_->applyEditorImeInputDisabled(disabled, true);
    emit editorChanged();
}

bool QmlPreferencesModel::videoDecodePrefersSoftware() const
{
    return backend_ != nullptr && backend_->currentVideoDecodePrefersSoftware();
}

void QmlPreferencesModel::setVideoDecodePrefersSoftware(bool preferSoftware)
{
    if (backend_ == nullptr || preferSoftware == videoDecodePrefersSoftware()) {
        return;
    }
    backend_->setVideoDecodePrefersSoftware(preferSoftware, true);
    emit performanceChanged();
}

double QmlPreferencesModel::displayRefreshRate() const
{
    return backend_ != nullptr ? backend_->currentPreviewCanvasRefreshRate() : 0.0;
}

QVariantList QmlPreferencesModel::frameRateOptions(bool includeDisplayRefresh) const
{
    QVariantList rows;
    if (includeDisplayRefresh) {
        const double refresh = displayRefreshRate();
        rows.append(option(
            static_cast<int>(MainWindow::PreviewCanvasFrameRateMode::DisplayRefresh),
            refresh > 0.0
                ? QStringLiteral("%1 (%2 Hz)")
                      .arg(UiText::text(
                          QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display")))
                      .arg(refresh, 0, 'f', 0)
                : UiText::text(
                      QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display"))));
    }
    rows.append(option(
        static_cast<int>(MainWindow::PreviewCanvasFrameRateMode::Fps30),
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.30"))));
    rows.append(option(
        static_cast<int>(MainWindow::PreviewCanvasFrameRateMode::Fps60),
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.60"))));
    rows.append(option(
        static_cast<int>(MainWindow::PreviewCanvasFrameRateMode::Fps120),
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.120"))));
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
    return backend_ != nullptr ? static_cast<int>(backend_->currentPreviewCanvasFrameRateMode()) : 0;
}

void QmlPreferencesModel::setCanvasFrameRateMode(int mode)
{
    if (backend_ == nullptr || mode == canvasFrameRateMode()) {
        return;
    }
    backend_->setPreviewCanvasFrameRateMode(
        static_cast<MainWindow::PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int QmlPreferencesModel::stageMediaFrameRateMode() const
{
    return backend_ != nullptr
        ? static_cast<int>(backend_->currentPreviewStageMediaFrameRateMode())
        : 0;
}

void QmlPreferencesModel::setStageMediaFrameRateMode(int mode)
{
    if (backend_ == nullptr || mode == stageMediaFrameRateMode()) {
        return;
    }
    backend_->setPreviewStageMediaFrameRateMode(
        static_cast<MainWindow::PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

int QmlPreferencesModel::timelineFrameRateMode() const
{
    return backend_ != nullptr ? static_cast<int>(backend_->currentTimelineFrameRateMode()) : 0;
}

void QmlPreferencesModel::setTimelineFrameRateMode(int mode)
{
    if (backend_ == nullptr || mode == timelineFrameRateMode()) {
        return;
    }
    backend_->setTimelineFrameRateMode(
        static_cast<MainWindow::PreviewCanvasFrameRateMode>(mode), true);
    emit performanceChanged();
}

}  // namespace miacode::qml_ui
