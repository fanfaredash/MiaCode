#pragma once

#include <QFont>
#include <QObject>
#include <QSettings>
#include <QString>

// 桌面工作台的持久化边界。QML 在拖动期间维护临时几何，只在用户完成
// 操作后写入这里，从而避免分隔线移动时连续刷新配置文件。
// Appearance / audio / preview prefs live in MainWindow::onPreferences().
class QmlWorkspaceSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY sidebarVisibleChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(bool bottomPanelVisible READ bottomPanelVisible WRITE setBottomPanelVisible NOTIFY bottomPanelVisibleChanged)
    Q_PROPERTY(int bottomPanelHeight READ bottomPanelHeight WRITE setBottomPanelHeight NOTIFY bottomPanelHeightChanged)
    Q_PROPERTY(bool previewVisible READ previewVisible WRITE setPreviewVisible NOTIFY previewVisibleChanged)
    Q_PROPERTY(double previewWidthRatio READ previewWidthRatio WRITE setPreviewWidthRatio NOTIFY previewWidthRatioChanged)
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily CONSTANT)
    Q_PROPERTY(QFont codeFont READ codeFont CONSTANT)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)

public:
    explicit QmlWorkspaceSettings(QObject* parent = nullptr);

    bool sidebarVisible() const;
    int sidebarWidth() const;
    bool bottomPanelVisible() const;
    int bottomPanelHeight() const;
    bool previewVisible() const;
    double previewWidthRatio() const;
    QString uiFontFamily() const;
    QFont codeFont() const;
    int fontSize() const;

    void setSidebarVisible(bool value);
    void setSidebarWidth(int value);
    void setBottomPanelVisible(bool value);
    void setBottomPanelHeight(int value);
    void setPreviewVisible(bool value);
    void setPreviewWidthRatio(double value);
    void setFontSize(int value);

signals:
    void sidebarVisibleChanged();
    void sidebarWidthChanged();
    void bottomPanelVisibleChanged();
    void bottomPanelHeightChanged();
    void previewVisibleChanged();
    void previewWidthRatioChanged();
    void fontSizeChanged();

private:
    QSettings settings_;
    bool sidebarVisible_ = true;
    int sidebarWidth_ = 190;
    bool bottomPanelVisible_ = true;
    int bottomPanelHeight_ = 215;
    bool previewVisible_ = true;
    double previewWidthRatio_ = 0.5;
    QString uiFontFamily_;
    QFont codeFont_;
    int fontSize_ = 13;
};
