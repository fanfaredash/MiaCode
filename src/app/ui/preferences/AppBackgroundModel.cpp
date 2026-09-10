#include "app/ui/preferences/AppBackgroundModel.h"

#include "app/ui/preferences/LocaleService.h"
#include "app/ui/preferences/PreferenceDocument.h"
#include "app/services/UiRequestService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <utility>
#include <QCoreApplication>

namespace miacode::ui {

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
    return qtTrId(key.toUtf8().constData());
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
    return qtTrId(key.toUtf8().constData());
}

} // namespace

AppBackgroundModel::AppBackgroundModel(miacode::UiRequestService* uiRequests,
                                             LoadPreferences loadPreferences,
                                             SavePreferences savePreferences,
                                             QObject* parent)
    : QObject(parent)
    , uiRequests_(uiRequests)
    , loadPreferences_(loadPreferences ? std::move(loadPreferences) : [] { return PreferenceDocument::loadPreferencesObject(); })
    , savePreferences_(savePreferences ? std::move(savePreferences) : [](const QJsonObject& root) {
        return PreferenceDocument::savePreferencesObject(root);
    })
{
    connect(&miacode::LocaleService::instance(), &miacode::LocaleService::languageChanged,
            this, [this](const QString&) {
                emit localeLabelsChanged();
                updateImageProjection();
            });
    reload();
}

bool AppBackgroundModel::enabled() const { return settings_.enabled; }
QString AppBackgroundModel::imagePath() const { return settings_.imagePath; }
QString AppBackgroundModel::sourceUrl() const { return sourceUrl_; }
bool AppBackgroundModel::imageReadable() const { return imageReadable_; }
double AppBackgroundModel::opacity() const { return settings_.opacity; }
int AppBackgroundModel::blur() const { return settings_.blur; }
QString AppBackgroundModel::sizeMode() const { return miacode::ui::appBackgroundSizeModeToken(settings_.sizeMode); }
QString AppBackgroundModel::position() const { return miacode::ui::appBackgroundPositionToken(settings_.position); }

void AppBackgroundModel::setEnabled(bool value)
{
    Settings next = settings_;
    next.enabled = value;
    commit(next);
}

QString AppBackgroundModel::cleanImagePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool AppBackgroundModel::isReadableFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}

QString AppBackgroundModel::imageError()
{
    return qtTrId("dialog.preferences.background.image_error");
}

void AppBackgroundModel::setImagePath(const QString& path)
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

void AppBackgroundModel::clearImage()
{
    setImagePath({});
}

void AppBackgroundModel::chooseImage()
{
    if (uiRequests_ == nullptr) {
        updateImageProjection(qtTrId("dialog.preferences.background.file_picker_unavailable"));
        return;
    }
    miacode::FileRequest request;
    request.title = qtTrId("dialog.preferences.background.choose");
    request.nameFilters = {qtTrId("dialog.preferences.background.image_filter")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setImagePath(path);
        }
    });
}

void AppBackgroundModel::setOpacity(double value)
{
    Settings next = settings_;
    next.opacity = value;
    commit(next);
}

void AppBackgroundModel::setBlur(int value)
{
    Settings next = settings_;
    next.blur = value;
    commit(next);
}

void AppBackgroundModel::setSizeMode(const QString& value)
{
    Settings next = settings_;
    next.sizeMode = miacode::ui::appBackgroundSizeModeFromToken(value);
    commit(next);
}

void AppBackgroundModel::setPosition(const QString& value)
{
    Settings next = settings_;
    next.position = miacode::ui::appBackgroundPositionFromToken(value);
    commit(next);
}

int AppBackgroundModel::toolbarAlphaDark() const { return settings_.overlays.toolbarAlphaDark; }
int AppBackgroundModel::toolbarAlphaLight() const { return settings_.overlays.toolbarAlphaLight; }
int AppBackgroundModel::statusAlphaDark() const { return settings_.overlays.statusAlphaDark; }
int AppBackgroundModel::statusAlphaLight() const { return settings_.overlays.statusAlphaLight; }
int AppBackgroundModel::panelAlpha() const { return settings_.overlays.panelAlpha; }
int AppBackgroundModel::cardAlphaDark() const { return settings_.overlays.cardAlphaDark; }
int AppBackgroundModel::cardAlphaLight() const { return settings_.overlays.cardAlphaLight; }
int AppBackgroundModel::editorHeaderAlphaDark() const { return settings_.overlays.editorHeaderAlphaDark; }
int AppBackgroundModel::editorHeaderAlphaLight() const { return settings_.overlays.editorHeaderAlphaLight; }
int AppBackgroundModel::inputAlphaDark() const { return settings_.overlays.inputAlphaDark; }
int AppBackgroundModel::inputAlphaLight() const { return settings_.overlays.inputAlphaLight; }
int AppBackgroundModel::codeEditorAlphaDark() const { return settings_.overlays.codeEditorAlphaDark; }
int AppBackgroundModel::codeEditorAlphaLight() const { return settings_.overlays.codeEditorAlphaLight; }

#define BACKGROUND_ALPHA_SETTER(setter, field) \
    void AppBackgroundModel::setter(int value) \
    { \
        Settings next = settings_; \
        next.overlays.field = value; \
        commit(next); \
    }
BACKGROUND_ALPHA_SETTER(setToolbarAlphaDark, toolbarAlphaDark)
BACKGROUND_ALPHA_SETTER(setToolbarAlphaLight, toolbarAlphaLight)
BACKGROUND_ALPHA_SETTER(setStatusAlphaDark, statusAlphaDark)
BACKGROUND_ALPHA_SETTER(setStatusAlphaLight, statusAlphaLight)
BACKGROUND_ALPHA_SETTER(setPanelAlpha, panelAlpha)
BACKGROUND_ALPHA_SETTER(setEditorHeaderAlphaDark, editorHeaderAlphaDark)
BACKGROUND_ALPHA_SETTER(setEditorHeaderAlphaLight, editorHeaderAlphaLight)
BACKGROUND_ALPHA_SETTER(setInputAlphaDark, inputAlphaDark)
BACKGROUND_ALPHA_SETTER(setInputAlphaLight, inputAlphaLight)
BACKGROUND_ALPHA_SETTER(setCodeEditorAlphaDark, codeEditorAlphaDark)
BACKGROUND_ALPHA_SETTER(setCodeEditorAlphaLight, codeEditorAlphaLight)
#undef BACKGROUND_ALPHA_SETTER

QVariantMap AppBackgroundModel::option(const QString& value, const QString& label)
{
    return {{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
}

QVariantList AppBackgroundModel::sizeModeOptions() const
{
    return {option(QStringLiteral("cover"), sizeModeLabel(QStringLiteral("cover"))),
            option(QStringLiteral("contain"), sizeModeLabel(QStringLiteral("contain"))),
            option(QStringLiteral("stretch"), sizeModeLabel(QStringLiteral("stretch"))),
            option(QStringLiteral("center"), sizeModeLabel(QStringLiteral("center"))),
            option(QStringLiteral("repeat"), sizeModeLabel(QStringLiteral("repeat")))};
}

QVariantList AppBackgroundModel::positionOptions() const
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

QString AppBackgroundModel::errorMessage() const { return errorMessage_; }

void AppBackgroundModel::updateImageProjection(const QString& error)
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

bool AppBackgroundModel::commit(const Settings& candidate)
{
    const Settings next = miacode::ui::normalizedAppBackgroundSettings(candidate);
    QJsonObject root = loadPreferences_ ? loadPreferences_() : QJsonObject{};
    QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    ui.insert(QStringLiteral("app_background"), miacode::ui::appBackgroundSettingsToJson(next));
    root.insert(QStringLiteral("ui"), ui);
    if (!savePreferences_ || !savePreferences_(root)) {
        errorMessage_ = qtTrId("dialog.preferences.background.save_error");
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
        || old.overlays.panelAlpha != settings_.overlays.panelAlpha
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

void AppBackgroundModel::reload()
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

} // namespace miacode::ui
