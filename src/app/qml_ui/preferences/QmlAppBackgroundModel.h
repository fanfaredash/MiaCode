#pragma once

#include "app/ui/AppBackgroundSettings.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace miacode::v2 {
class UiRequestService;
}

namespace miacode::qml_ui {

class QmlAppBackgroundModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY imagePathChanged)
    Q_PROPERTY(QString sourceUrl READ sourceUrl NOTIFY sourceUrlChanged)
    Q_PROPERTY(bool imageReadable READ imageReadable NOTIFY imageReadableChanged)
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(int blur READ blur WRITE setBlur NOTIFY blurChanged)
    Q_PROPERTY(QString sizeMode READ sizeMode WRITE setSizeMode NOTIFY sizeModeChanged)
    Q_PROPERTY(QString position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(int toolbarAlphaDark READ toolbarAlphaDark WRITE setToolbarAlphaDark NOTIFY overlayChanged)
    Q_PROPERTY(int toolbarAlphaLight READ toolbarAlphaLight WRITE setToolbarAlphaLight NOTIFY overlayChanged)
    Q_PROPERTY(int statusAlphaDark READ statusAlphaDark WRITE setStatusAlphaDark NOTIFY overlayChanged)
    Q_PROPERTY(int statusAlphaLight READ statusAlphaLight WRITE setStatusAlphaLight NOTIFY overlayChanged)
    Q_PROPERTY(int panelAlpha READ panelAlpha WRITE setPanelAlpha NOTIFY overlayChanged)
    Q_PROPERTY(int cardAlphaDark READ cardAlphaDark CONSTANT)
    Q_PROPERTY(int cardAlphaLight READ cardAlphaLight CONSTANT)
    Q_PROPERTY(int editorHeaderAlphaDark READ editorHeaderAlphaDark WRITE setEditorHeaderAlphaDark NOTIFY overlayChanged)
    Q_PROPERTY(int editorHeaderAlphaLight READ editorHeaderAlphaLight WRITE setEditorHeaderAlphaLight NOTIFY overlayChanged)
    Q_PROPERTY(int inputAlphaDark READ inputAlphaDark WRITE setInputAlphaDark NOTIFY overlayChanged)
    Q_PROPERTY(int inputAlphaLight READ inputAlphaLight WRITE setInputAlphaLight NOTIFY overlayChanged)
    Q_PROPERTY(int codeEditorAlphaDark READ codeEditorAlphaDark WRITE setCodeEditorAlphaDark NOTIFY overlayChanged)
    Q_PROPERTY(int codeEditorAlphaLight READ codeEditorAlphaLight WRITE setCodeEditorAlphaLight NOTIFY overlayChanged)
    Q_PROPERTY(QVariantList sizeModeOptions READ sizeModeOptions CONSTANT)
    Q_PROPERTY(QVariantList positionOptions READ positionOptions CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    using LoadPreferences = std::function<QJsonObject()>;
    using SavePreferences = std::function<bool(const QJsonObject&)>;

    explicit QmlAppBackgroundModel(miacode::v2::UiRequestService* uiRequests = nullptr,
                                   LoadPreferences loadPreferences = {},
                                   SavePreferences savePreferences = {},
                                   QObject* parent = nullptr);

    bool enabled() const;
    void setEnabled(bool value);
    QString imagePath() const;
    void setImagePath(const QString& path);
    QString sourceUrl() const;
    bool imageReadable() const;
    double opacity() const;
    void setOpacity(double value);
    int blur() const;
    void setBlur(int value);
    QString sizeMode() const;
    void setSizeMode(const QString& value);
    QString position() const;
    void setPosition(const QString& value);

    int toolbarAlphaDark() const;
    int toolbarAlphaLight() const;
    int statusAlphaDark() const;
    int statusAlphaLight() const;
    int panelAlpha() const;
    int cardAlphaDark() const;
    int cardAlphaLight() const;
    int editorHeaderAlphaDark() const;
    int editorHeaderAlphaLight() const;
    int inputAlphaDark() const;
    int inputAlphaLight() const;
    int codeEditorAlphaDark() const;
    int codeEditorAlphaLight() const;
    void setToolbarAlphaDark(int value);
    void setToolbarAlphaLight(int value);
    void setStatusAlphaDark(int value);
    void setStatusAlphaLight(int value);
    void setPanelAlpha(int value);
    void setEditorHeaderAlphaDark(int value);
    void setEditorHeaderAlphaLight(int value);
    void setInputAlphaDark(int value);
    void setInputAlphaLight(int value);
    void setCodeEditorAlphaDark(int value);
    void setCodeEditorAlphaLight(int value);

    QVariantList sizeModeOptions() const;
    QVariantList positionOptions() const;
    QString errorMessage() const;

    Q_INVOKABLE void clearImage();
    Q_INVOKABLE void chooseImage();
    Q_INVOKABLE void reload();

signals:
    void enabledChanged();
    void imagePathChanged();
    void sourceUrlChanged();
    void imageReadableChanged();
    void opacityChanged();
    void blurChanged();
    void sizeModeChanged();
    void positionChanged();
    void overlayChanged();
    void errorChanged();

private:
    using Settings = miacode::ui::AppBackgroundSettings;

    bool commit(const Settings& candidate);
    void updateImageProjection(const QString& error = {});
    static bool isReadableFile(const QString& path);
    static QString cleanImagePath(const QString& path);
    static QString imageError();
    static QVariantMap option(const QString& value, const QString& label);

    LoadPreferences loadPreferences_;
    SavePreferences savePreferences_;
    miacode::v2::UiRequestService* uiRequests_ = nullptr;
    Settings settings_;
    QString sourceUrl_;
    bool imageReadable_ = false;
    QString errorMessage_;
};

} // namespace miacode::qml_ui
