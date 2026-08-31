#pragma once

#include <QFont>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantMap>

// 桌面工作台的持久化边界。QML 在拖动期间维护临时几何，只在用户完成
// 操作后写入这里，从而避免分隔线移动时连续刷新配置文件。
// QML preference models own interaction; MainWindow only persists and applies
// their backend-neutral values for preview/runtime consumers.
class QmlUiSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY sidebarVisibleChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(int sidebarMinimumContentWidth READ sidebarMinimumContentWidth CONSTANT)
    Q_PROPERTY(int sidebarMaximumContentWidth READ sidebarMaximumContentWidth CONSTANT)
    Q_PROPERTY(bool bottomPanelVisible READ bottomPanelVisible WRITE setBottomPanelVisible NOTIFY bottomPanelVisibleChanged)
    Q_PROPERTY(double bottomPanelHeightRatio READ bottomPanelHeightRatio WRITE setBottomPanelHeightRatio NOTIFY bottomPanelHeightRatioChanged)
    Q_PROPERTY(double bottomPanelMinimumHeightRatio READ bottomPanelMinimumHeightRatio CONSTANT)
    Q_PROPERTY(double bottomPanelMaximumHeightRatio READ bottomPanelMaximumHeightRatio CONSTANT)
    Q_PROPERTY(double previewWidthRatio READ previewWidthRatio WRITE setPreviewWidthRatio NOTIFY previewWidthRatioChanged)
    Q_PROPERTY(double previewMinimumWidthRatio READ previewMinimumWidthRatio CONSTANT)
    Q_PROPERTY(double previewMaximumWidthRatio READ previewMaximumWidthRatio CONSTANT)
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily CONSTANT)
    Q_PROPERTY(QString themeToken READ themeToken CONSTANT)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY themeChanged)
    Q_PROPERTY(QFont codeFont READ codeFont NOTIFY editorSettingsChanged)
    Q_PROPERTY(int editorBlockSpacing READ editorBlockSpacing NOTIFY editorSettingsChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool editorHalfWidthInputEnabled READ editorHalfWidthInputEnabled NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorOverwriteModeEnabled READ editorOverwriteModeEnabled NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorAutoCompletionEnabled READ editorAutoCompletionEnabled NOTIFY editorSettingsChanged)
    Q_PROPERTY(bool editorImeInputDisabled READ editorImeInputDisabled NOTIFY editorSettingsChanged)

public:
    // Localized lookup for QML that holds a UiText key rather than a string.
    Q_INVOKABLE QString localizedText(const QString& key) const;
    // What 关于 MiaCode shows: version, platform triple and build type. Read
    // from the build's own macros so the page cannot drift from the binary.
    Q_INVOKABLE QVariantMap aboutInfo() const;
    explicit QmlUiSettings(QObject* parent = nullptr);

    bool sidebarVisible() const;
    int sidebarWidth() const;
    int sidebarMinimumContentWidth() const;
    int sidebarMaximumContentWidth() const;
    bool bottomPanelVisible() const;
    double bottomPanelHeightRatio() const;
    double bottomPanelMinimumHeightRatio() const;
    double bottomPanelMaximumHeightRatio() const;
    double previewWidthRatio() const;
    double previewMinimumWidthRatio() const;
    double previewMaximumWidthRatio() const;
    QString uiFontFamily() const;
    QString themeToken() const;
    bool darkTheme() const;
    QFont codeFont() const;
    int editorBlockSpacing() const;
    int fontSize() const;
    bool editorHalfWidthInputEnabled() const;
    bool editorOverwriteModeEnabled() const;
    bool editorAutoCompletionEnabled() const;
    bool editorImeInputDisabled() const;

    void setSidebarVisible(bool value);
    void setSidebarWidth(int value);
    void setBottomPanelVisible(bool value);
    void setBottomPanelHeightRatio(double value);
    void setPreviewWidthRatio(double value);
    void setFontSize(int value);
    void reloadEditorSettings();
    void reloadTheme();
    void setThemeToken(const QString& token);
    void setEditorAppearance(int pointSize, double lineSpacingFactor);

signals:
    void sidebarVisibleChanged();
    void sidebarWidthChanged();
    void bottomPanelVisibleChanged();
    void bottomPanelHeightRatioChanged();
    void previewWidthRatioChanged();
    void fontSizeChanged();
    void editorSettingsChanged();
    void themeChanged();

private:
    static constexpr int kSidebarMinimumContentWidth = 120;
    static constexpr int kSidebarMaximumContentWidth = 272;
    static constexpr double kBottomPanelMinimumHeightRatio = 0.2;
    static constexpr double kBottomPanelMaximumHeightRatio = 0.65;
    static constexpr double kPreviewMinimumWidthRatio = 0.3;
    static constexpr double kPreviewMaximumWidthRatio = 0.5;

    QSettings settings_;
    bool sidebarVisible_ = true;
    int sidebarWidth_ = 190;
    bool bottomPanelVisible_ = true;
    double bottomPanelHeightRatio_ = 0.35;
    double previewWidthRatio_ = 0.5;
    QString uiFontFamily_;
    QFont codeFont_;
    int editorBlockSpacing_ = 0;
    int fontSize_ = 13;
    bool editorHalfWidthInputEnabled_ = true;
    bool editorOverwriteModeEnabled_ = false;
    bool editorAutoCompletionEnabled_ = true;
    bool editorImeInputDisabled_ = true;
    bool darkTheme_ = true;
};
