#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class MainWindow;
class QmlUiSettings;

namespace miacode::qml_ui {

// QML-facing projection of 偏好设置. Every setting already had an
// apply/set accessor taking a "persist" flag on MainWindow, so this adds no
// policy of its own — it only names the settings and reports what changed.
//
// Language and theme are deliberately not live-applied: both need a restart to
// take full effect, and the Widgets dialog prompted for one. The QML page shows
// the same prompt, so the model just records the choice.
class QmlPreferencesModel final : public QObject
{
    Q_OBJECT

    // Interface
    Q_PROPERTY(QVariantList languageOptions READ languageOptions NOTIFY interfaceChanged)
    Q_PROPERTY(QString languageToken READ languageToken WRITE setLanguageToken NOTIFY interfaceChanged)
    Q_PROPERTY(QVariantList themeOptions READ themeOptions CONSTANT)
    Q_PROPERTY(QString themeToken READ themeToken WRITE setThemeToken NOTIFY interfaceChanged)
    Q_PROPERTY(bool previewOnLeft READ previewOnLeft WRITE setPreviewOnLeft NOTIFY interfaceChanged)

    // Editor
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorChanged)
    Q_PROPERTY(int editorFontSizeMinimum READ editorFontSizeMinimum CONSTANT)
    Q_PROPERTY(int editorFontSizeMaximum READ editorFontSizeMaximum CONSTANT)
    Q_PROPERTY(QVariantList lineSpacingOptions READ lineSpacingOptions CONSTANT)
    Q_PROPERTY(double editorLineSpacing READ editorLineSpacing WRITE setEditorLineSpacing NOTIFY editorChanged)
    Q_PROPERTY(bool editorAutoCompletion READ editorAutoCompletion WRITE setEditorAutoCompletion NOTIFY editorChanged)
    Q_PROPERTY(bool editorHalfWidthInput READ editorHalfWidthInput WRITE setEditorHalfWidthInput NOTIFY editorChanged)
    Q_PROPERTY(bool editorImeDisabled READ editorImeDisabled WRITE setEditorImeDisabled NOTIFY editorChanged)

    // Performance
    Q_PROPERTY(bool videoDecodePrefersSoftware READ videoDecodePrefersSoftware WRITE setVideoDecodePrefersSoftware NOTIFY performanceChanged)
    Q_PROPERTY(double displayRefreshRate READ displayRefreshRate CONSTANT)
    Q_PROPERTY(QVariantList canvasFrameRateOptions READ canvasFrameRateOptions CONSTANT)
    Q_PROPERTY(QVariantList appFrameRateOptions READ appFrameRateOptions CONSTANT)
    Q_PROPERTY(int canvasFrameRateMode READ canvasFrameRateMode WRITE setCanvasFrameRateMode NOTIFY performanceChanged)
    Q_PROPERTY(int stageMediaFrameRateMode READ stageMediaFrameRateMode WRITE setStageMediaFrameRateMode NOTIFY performanceChanged)
    Q_PROPERTY(int timelineFrameRateMode READ timelineFrameRateMode WRITE setTimelineFrameRateMode NOTIFY performanceChanged)

    // True once a change was made that only takes effect after a restart.
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY interfaceChanged)

public:
    explicit QmlPreferencesModel(MainWindow& backend, QmlUiSettings& settings, QObject* parent = nullptr);

    QVariantList languageOptions() const;
    QString languageToken() const;
    void setLanguageToken(const QString& token);
    QVariantList themeOptions() const;
    QString themeToken() const;
    void setThemeToken(const QString& token);
    bool previewOnLeft() const;
    void setPreviewOnLeft(bool onLeft);

    int editorFontSize() const;
    void setEditorFontSize(int pointSize);
    int editorFontSizeMinimum() const { return kEditorFontSizeMinimum; }
    int editorFontSizeMaximum() const { return kEditorFontSizeMaximum; }
    QVariantList lineSpacingOptions() const;
    double editorLineSpacing() const;
    void setEditorLineSpacing(double factor);
    bool editorAutoCompletion() const;
    void setEditorAutoCompletion(bool enabled);
    bool editorHalfWidthInput() const;
    void setEditorHalfWidthInput(bool enabled);
    bool editorImeDisabled() const;
    void setEditorImeDisabled(bool disabled);

    bool videoDecodePrefersSoftware() const;
    void setVideoDecodePrefersSoftware(bool preferSoftware);
    double displayRefreshRate() const;
    QVariantList canvasFrameRateOptions() const;
    QVariantList appFrameRateOptions() const;
    int canvasFrameRateMode() const;
    void setCanvasFrameRateMode(int mode);
    int stageMediaFrameRateMode() const;
    void setStageMediaFrameRateMode(int mode);
    int timelineFrameRateMode() const;
    void setTimelineFrameRateMode(int mode);

    bool restartRequired() const { return restartRequired_; }

signals:
    void interfaceChanged();
    void editorChanged();
    void performanceChanged();

private:
    static constexpr int kEditorFontSizeMinimum = 8;
    static constexpr int kEditorFontSizeMaximum = 28;

    QVariantList frameRateOptions(bool includeDisplayRefresh) const;

    MainWindow* backend_ = nullptr;
    QmlUiSettings* settings_ = nullptr;
    bool restartRequired_ = false;
};

}  // namespace miacode::qml_ui
