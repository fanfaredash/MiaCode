#include "app/qml_ui/preferences/QmlAppBackgroundModel.h"

#include "app/ui/UiText.h"
#include "app/v2/UiRequestService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <utility>

namespace miacode::qml_ui {

namespace {

QString sizeModeLabel(const QString& value)
{
    QString key = QStringLiteral("dialog.preferences.background.scale.cover");
    if (value == QStringLiteral("contain")) {
        key = QStringLiteral("dialog.preferences.background.scale.contain");
    } else if (value == QStringLiteral("stretch")) {
        key = QStringLiteral("dialog.preferences.background.scale.stretch");
    } else if (value == QStringLiteral("center")) {
        key = QStringLiteral("dialog.preferences.background.scale.center");
    } else if (value == QStringLiteral("repeat")) {
        key = QStringLiteral("dialog.preferences.background.scale.repeat");
    }
    return UiText::text(key);
}

QString positionLabel(const QString& value)
{
    QString key = QStringLiteral("dialog.preferences.background.position.center");
    if (value == QStringLiteral("left")) {
        key = QStringLiteral("dialog.preferences.background.position.left");
    } else if (value == QStringLiteral("right")) {
        key = QStringLiteral("dialog.preferences.background.position.right");
    } else if (value == QStringLiteral("top")) {
        key = QStringLiteral("dialog.preferences.background.position.top");
    } else if (value == QStringLiteral("bottom")) {
        key = QStringLiteral("dialog.preferences.background.position.bottom");
    } else if (value == QStringLiteral("left_top")) {
        key = QStringLiteral("dialog.preferences.background.position.left_top");
    } else if (value == QStringLiteral("right_top")) {
        key = QStringLiteral("dialog.preferences.background.position.right_top");
    } else if (value == QStringLiteral("left_bottom")) {
        key = QStringLiteral("dialog.preferences.background.position.left_bottom");
    } else if (value == QStringLiteral("right_bottom")) {
        key = QStringLiteral("dialog.preferences.background.position.right_bottom");
    }
    return UiText::text(key);
}

} // namespace

QmlAppBackgroundModel::QmlAppBackgroundModel(miacode::v2::UiRequestService* uiRequests,
                                             LoadPreferences loadPreferences,
                                             SavePreferences savePreferences,
                                             QObject* parent)
    : QObject(parent)
    , uiRequests_(uiRequests)
    , loadPreferences_(loadPreferences ? std::move(loadPreferences) : [] { return UiText::loadPreferencesObject(); })
    , savePreferences_(savePreferences ? std::move(savePreferences) : [](const QJsonObject& root) {
        return UiText::savePreferencesObject(root);
    })
{
    reload();
}

bool QmlAppBackgroundModel::enabled() const { return settings_.enabled; }
QString QmlAppBackgroundModel::imagePath() const { return settings_.imagePath; }
QString QmlAppBackgroundModel::sourceUrl() const { return sourceUrl_; }
bool QmlAppBackgroundModel::imageReadable() const { return imageReadable_; }
double QmlAppBackgroundModel::opacity() const { return settings_.opacity; }
int QmlAppBackgroundModel::blur() const { return settings_.blur; }
QString QmlAppBackgroundModel::sizeMode() const { return miacode::ui::appBackgroundSizeModeToken(settings_.sizeMode); }
QString QmlAppBackgroundModel::position() const { return miacode::ui::appBackgroundPositionToken(settings_.position); }

void QmlAppBackgroundModel::setEnabled(bool value)
{
    Settings next = settings_;
    next.enabled = value;
    commit(next);
}

QString QmlAppBackgroundModel::cleanImagePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool QmlAppBackgroundModel::isReadableFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}

QString QmlAppBackgroundModel::imageError()
{
    return UiText::text(QStringLiteral("dialog.preferences.background.image_error"));
}

void QmlAppBackgroundModel::setImagePath(const QString& path)
{
    const QString cleaned = cleanImagePath(path);
    if (!cleaned.isEmpty() && !isReadableFile(cleaned)) {
        updateImageProjection(imageError());
        return;
    }
    Settings next = settings_;
    next.imagePath = cleaned;
    commit(next);
}

void QmlAppBackgroundModel::clearImage()
{
    setImagePath({});
}

void QmlAppBackgroundModel::chooseImage()
{
    if (uiRequests_ == nullptr) {
        updateImageProjection(UiText::text(QStringLiteral("dialog.preferences.background.file_picker_unavailable")));
        return;
    }
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("dialog.preferences.background.choose"));
    request.nameFilters = {UiText::text(QStringLiteral("dialog.preferences.background.image_filter"))};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setImagePath(path);
        }
    });
}

void QmlAppBackgroundModel::setOpacity(double value)
{
    Settings next = settings_;
    next.opacity = value;
    commit(next);
}

void QmlAppBackgroundModel::setBlur(int value)
{
    Settings next = settings_;
    next.blur = value;
    commit(next);
}

void QmlAppBackgroundModel::setSizeMode(const QString& value)
{
    Settings next = settings_;
    next.sizeMode = miacode::ui::appBackgroundSizeModeFromToken(value);
    commit(next);
}

void QmlAppBackgroundModel::setPosition(const QString& value)
{
    Settings next = settings_;
    next.position = miacode::ui::appBackgroundPositionFromToken(value);
    commit(next);
}

int QmlAppBackgroundModel::toolbarAlphaDark() const { return settings_.overlays.toolbarAlphaDark; }
int QmlAppBackgroundModel::toolbarAlphaLight() const { return settings_.overlays.toolbarAlphaLight; }
int QmlAppBackgroundModel::statusAlphaDark() const { return settings_.overlays.statusAlphaDark; }
int QmlAppBackgroundModel::statusAlphaLight() const { return settings_.overlays.statusAlphaLight; }
int QmlAppBackgroundModel::panelAlphaDark() const { return settings_.overlays.panelAlphaDark; }
int QmlAppBackgroundModel::panelAlphaLight() const { return settings_.overlays.panelAlphaLight; }
int QmlAppBackgroundModel::cardAlphaDark() const { return settings_.overlays.cardAlphaDark; }
int QmlAppBackgroundModel::cardAlphaLight() const { return settings_.overlays.cardAlphaLight; }
int QmlAppBackgroundModel::editorHeaderAlphaDark() const { return settings_.overlays.editorHeaderAlphaDark; }
int QmlAppBackgroundModel::editorHeaderAlphaLight() const { return settings_.overlays.editorHeaderAlphaLight; }
int QmlAppBackgroundModel::inputAlphaDark() const { return settings_.overlays.inputAlphaDark; }
int QmlAppBackgroundModel::inputAlphaLight() const { return settings_.overlays.inputAlphaLight; }
int QmlAppBackgroundModel::codeEditorAlphaDark() const { return settings_.overlays.codeEditorAlphaDark; }
int QmlAppBackgroundModel::codeEditorAlphaLight() const { return settings_.overlays.codeEditorAlphaLight; }

#define BACKGROUND_ALPHA_SETTER(setter, field) \
    void QmlAppBackgroundModel::setter(int value) \
    { \
        Settings next = settings_; \
        next.overlays.field = value; \
        commit(next); \
    }
BACKGROUND_ALPHA_SETTER(setToolbarAlphaDark, toolbarAlphaDark)
BACKGROUND_ALPHA_SETTER(setToolbarAlphaLight, toolbarAlphaLight)
BACKGROUND_ALPHA_SETTER(setStatusAlphaDark, statusAlphaDark)
BACKGROUND_ALPHA_SETTER(setStatusAlphaLight, statusAlphaLight)
BACKGROUND_ALPHA_SETTER(setPanelAlphaDark, panelAlphaDark)
BACKGROUND_ALPHA_SETTER(setPanelAlphaLight, panelAlphaLight)
BACKGROUND_ALPHA_SETTER(setEditorHeaderAlphaDark, editorHeaderAlphaDark)
BACKGROUND_ALPHA_SETTER(setEditorHeaderAlphaLight, editorHeaderAlphaLight)
BACKGROUND_ALPHA_SETTER(setInputAlphaDark, inputAlphaDark)
BACKGROUND_ALPHA_SETTER(setInputAlphaLight, inputAlphaLight)
BACKGROUND_ALPHA_SETTER(setCodeEditorAlphaDark, codeEditorAlphaDark)
BACKGROUND_ALPHA_SETTER(setCodeEditorAlphaLight, codeEditorAlphaLight)
#undef BACKGROUND_ALPHA_SETTER

QVariantMap QmlAppBackgroundModel::option(const QString& value, const QString& label)
{
    return {{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
}

QVariantList QmlAppBackgroundModel::sizeModeOptions() const
{
    return {option(QStringLiteral("cover"), sizeModeLabel(QStringLiteral("cover"))),
            option(QStringLiteral("contain"), sizeModeLabel(QStringLiteral("contain"))),
            option(QStringLiteral("stretch"), sizeModeLabel(QStringLiteral("stretch"))),
            option(QStringLiteral("center"), sizeModeLabel(QStringLiteral("center"))),
            option(QStringLiteral("repeat"), sizeModeLabel(QStringLiteral("repeat")))};
}

QVariantList QmlAppBackgroundModel::positionOptions() const
{
    return {option(QStringLiteral("center"), positionLabel(QStringLiteral("center"))),
            option(QStringLiteral("left"), positionLabel(QStringLiteral("left"))),
            option(QStringLiteral("right"), positionLabel(QStringLiteral("right"))),
            option(QStringLiteral("top"), positionLabel(QStringLiteral("top"))),
            option(QStringLiteral("bottom"), positionLabel(QStringLiteral("bottom"))),
            option(QStringLiteral("left_top"), positionLabel(QStringLiteral("left_top"))),
            option(QStringLiteral("right_top"), positionLabel(QStringLiteral("right_top"))),
            option(QStringLiteral("left_bottom"), positionLabel(QStringLiteral("left_bottom"))),
            option(QStringLiteral("right_bottom"), positionLabel(QStringLiteral("right_bottom")))};
}

QString QmlAppBackgroundModel::errorMessage() const { return errorMessage_; }

void QmlAppBackgroundModel::updateImageProjection(const QString& error)
{
    const QString nextUrl = isReadableFile(settings_.imagePath)
        ? QUrl::fromLocalFile(settings_.imagePath).toString()
        : QString();
    const bool nextReadable = !nextUrl.isEmpty();
    const QString nextError = error.isNull()
        ? (settings_.imagePath.isEmpty() || nextReadable ? QString() : imageError())
        : error;
    const QString oldUrl = sourceUrl_;
    const bool oldReadable = imageReadable_;
    const QString oldError = errorMessage_;
    sourceUrl_ = nextUrl;
    imageReadable_ = nextReadable;
    errorMessage_ = nextError;
    if (oldUrl != sourceUrl_) emit sourceUrlChanged();
    if (oldReadable != imageReadable_) emit imageReadableChanged();
    if (oldError != errorMessage_) emit errorChanged();
}

bool QmlAppBackgroundModel::commit(const Settings& candidate)
{
    const Settings next = miacode::ui::normalizedAppBackgroundSettings(candidate);
    QJsonObject root = loadPreferences_ ? loadPreferences_() : QJsonObject{};
    QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    ui.insert(QStringLiteral("app_background"), miacode::ui::appBackgroundSettingsToJson(next));
    root.insert(QStringLiteral("ui"), ui);
    if (!savePreferences_ || !savePreferences_(root)) {
        errorMessage_ = UiText::text(QStringLiteral("dialog.preferences.background.save_error"));
        emit errorChanged();
        return false;
    }

    const Settings old = settings_;
    settings_ = next;
    if (old.enabled != settings_.enabled) emit enabledChanged();
    if (old.imagePath != settings_.imagePath) emit imagePathChanged();
    if (old.opacity != settings_.opacity) emit opacityChanged();
    if (old.blur != settings_.blur) emit blurChanged();
    if (old.sizeMode != settings_.sizeMode) emit sizeModeChanged();
    if (old.position != settings_.position) emit positionChanged();
    if (old.overlays.toolbarAlphaDark != settings_.overlays.toolbarAlphaDark
        || old.overlays.toolbarAlphaLight != settings_.overlays.toolbarAlphaLight
        || old.overlays.statusAlphaDark != settings_.overlays.statusAlphaDark
        || old.overlays.statusAlphaLight != settings_.overlays.statusAlphaLight
        || old.overlays.panelAlphaDark != settings_.overlays.panelAlphaDark
        || old.overlays.panelAlphaLight != settings_.overlays.panelAlphaLight
        || old.overlays.editorHeaderAlphaDark != settings_.overlays.editorHeaderAlphaDark
        || old.overlays.editorHeaderAlphaLight != settings_.overlays.editorHeaderAlphaLight
        || old.overlays.inputAlphaDark != settings_.overlays.inputAlphaDark
        || old.overlays.inputAlphaLight != settings_.overlays.inputAlphaLight
        || old.overlays.codeEditorAlphaDark != settings_.overlays.codeEditorAlphaDark
        || old.overlays.codeEditorAlphaLight != settings_.overlays.codeEditorAlphaLight) {
        emit overlayChanged();
    }
    updateImageProjection();
    return true;
}

void QmlAppBackgroundModel::reload()
{
    const QJsonObject root = loadPreferences_ ? loadPreferences_() : QJsonObject{};
    const Settings next = miacode::ui::appBackgroundSettingsFromJson(
        root.value(QStringLiteral("ui")).toObject().value(QStringLiteral("app_background")).toObject());
    const Settings old = settings_;
    settings_ = next;
    if (old.enabled != settings_.enabled) emit enabledChanged();
    if (old.imagePath != settings_.imagePath) emit imagePathChanged();
    if (old.opacity != settings_.opacity) emit opacityChanged();
    if (old.blur != settings_.blur) emit blurChanged();
    if (old.sizeMode != settings_.sizeMode) emit sizeModeChanged();
    if (old.position != settings_.position) emit positionChanged();
    updateImageProjection();
}

} // namespace miacode::qml_ui
