#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "app/services/PreferencesStore.h"

namespace miacode::ui {
class WorkbenchSettings;

// QML-facing projection of preferences. Store accessors keep the durable
// values; this model combines related values into the interaction modes shown
// by the settings page and reports what changed.
//
// Language is applied live through LocaleService (QTranslator + retranslate).
// Theme still needs a restart for full effect; restartRequired tracks theme only.
class PreferencesModel final : public QObject
{
    Q_OBJECT

public:
    enum EditorInputHandlingMode {
        CorrectFullWidthOnly = 0,
        BlockInputMethodsAndCorrectFullWidth = 1,
        LeaveInputUnchanged = 2,
    };
    Q_ENUM(EditorInputHandlingMode)

    // Interface
    Q_PROPERTY(QVariantList languageOptions READ languageOptions NOTIFY interfaceChanged)
    Q_PROPERTY(QString languageToken READ languageToken WRITE setLanguageToken NOTIFY interfaceChanged)
    Q_PROPERTY(QVariantList themeOptions READ themeOptions NOTIFY interfaceChanged)
    Q_PROPERTY(QString themeToken READ themeToken WRITE setThemeToken NOTIFY interfaceChanged)
    Q_PROPERTY(bool previewOnLeft READ previewOnLeft WRITE setPreviewOnLeft NOTIFY interfaceChanged)

    // Editor
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorChanged)
    Q_PROPERTY(int editorFontSizeMinimum READ editorFontSizeMinimum CONSTANT)
    Q_PROPERTY(int editorFontSizeMaximum READ editorFontSizeMaximum CONSTANT)
    Q_PROPERTY(QVariantList lineSpacingOptions READ lineSpacingOptions NOTIFY interfaceChanged)
    Q_PROPERTY(double editorLineSpacing READ editorLineSpacing WRITE setEditorLineSpacing NOTIFY editorChanged)
    Q_PROPERTY(bool editorAutoCompletion READ editorAutoCompletion WRITE setEditorAutoCompletion NOTIFY editorChanged)
    Q_PROPERTY(int editorInputHandlingMode READ editorInputHandlingMode WRITE setEditorInputHandlingMode NOTIFY editorChanged)
    Q_PROPERTY(bool editorScrollPastEnd READ editorScrollPastEnd WRITE setEditorScrollPastEnd NOTIFY editorChanged)
    Q_PROPERTY(bool editorSelectionBeatDisplay READ editorSelectionBeatDisplay WRITE setEditorSelectionBeatDisplay NOTIFY editorChanged)

    // Performance
    Q_PROPERTY(bool videoDecodePrefersSoftware READ videoDecodePrefersSoftware WRITE setVideoDecodePrefersSoftware NOTIFY performanceChanged)
    Q_PROPERTY(double displayRefreshRate READ displayRefreshRate CONSTANT)
    Q_PROPERTY(QVariantList canvasFrameRateOptions READ canvasFrameRateOptions NOTIFY performanceChanged)
    Q_PROPERTY(QVariantList appFrameRateOptions READ appFrameRateOptions NOTIFY performanceChanged)
    Q_PROPERTY(int canvasFrameRateMode READ canvasFrameRateMode WRITE setCanvasFrameRateMode NOTIFY performanceChanged)
    Q_PROPERTY(int stageMediaFrameRateMode READ stageMediaFrameRateMode WRITE setStageMediaFrameRateMode NOTIFY performanceChanged)
    Q_PROPERTY(int timelineFrameRateMode READ timelineFrameRateMode WRITE setTimelineFrameRateMode NOTIFY performanceChanged)

    // True once a change was made that only takes effect after a restart.
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY interfaceChanged)

public:
    // No MainWindow: every value the page shows or writes goes through the
    // preferences store.
    explicit PreferencesModel(miacode::PreferencesStore*& storeSlot,
                                 WorkbenchSettings& settings, QObject* parent = nullptr);

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
    int editorInputHandlingMode() const;
    void setEditorInputHandlingMode(int mode);
    bool editorHalfWidthInput() const;
    void setEditorHalfWidthInput(bool enabled);
    bool editorImeDisabled() const;
    void setEditorImeDisabled(bool disabled);
    bool editorScrollPastEnd() const;
    void setEditorScrollPastEnd(bool enabled);
    bool editorSelectionBeatDisplay() const;
    void setEditorSelectionBeatDisplay(bool enabled);

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

    // Bound to the assembly's slot, not a snapshot.
    miacode::PreferencesStore** storeSlot_ = nullptr;
    miacode::PreferencesStore* store() const
    {
        return storeSlot_ != nullptr ? *storeSlot_ : nullptr;
    }
    WorkbenchSettings* settings_ = nullptr;
    bool restartRequired_ = false;
};

}  // namespace miacode::ui
