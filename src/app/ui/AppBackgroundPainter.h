#pragma once

#include "app/ui/AppBackgroundSettings.h"

#include <QFrame>
#include <QImage>
#include <QMenuBar>
#include <QObject>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

class QEvent;
class QPainter;

namespace miacode::ui {

class AppBackgroundPainter final : public QObject {
public:
    explicit AppBackgroundPainter(QWidget* window);
    ~AppBackgroundPainter() override;

    AppBackgroundSettings settings() const;
    void setSettings(const AppBackgroundSettings& settings);
    void invalidateCache();
    bool paintBackgroundForSurface(QWidget* surface, QPainter& painter);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool ensureSourceLoaded();
    QImage blurredImage(const QImage& image) const;
    QSize canvasSize() const;
    QRect targetRectForImage(const QSize& imageSize, const QSize& canvasSize) const;
    QPoint alignedTopLeft(const QSize& drawSize, const QSize& canvasSize) const;
    QPixmap renderedPixmap(const QSize& canvasSize);
    void updateApplicationActiveFlag() const;
    void requestSurfaceUpdates() const;

    QWidget* window_ = nullptr;
    AppBackgroundSettings settings_;
    QString loadedPath_;
    QImage sourceImage_;
    QPixmap cachedPixmap_;
    QSize cachedWidgetSize_;
    AppBackgroundSettings cachedSettings_;
};

void installAppBackgroundPainter(QWidget* window, AppBackgroundPainter* painter);
AppBackgroundPainter* appBackgroundPainterForWidget(QWidget* surface);
bool paintAppBackgroundForWidget(QWidget* surface, QPainter& painter);
bool appBackgroundIsActiveForTheme();

class AppBackgroundSurfaceWidget : public QWidget {
public:
    explicit AppBackgroundSurfaceWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AppBackgroundSurfaceFrame : public QFrame {
public:
    explicit AppBackgroundSurfaceFrame(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AppBackgroundSurfaceTabWidget : public QTabWidget {
public:
    explicit AppBackgroundSurfaceTabWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AppBackgroundSurfaceMenuBar : public QMenuBar {
public:
    explicit AppBackgroundSurfaceMenuBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AppBackgroundSurfaceToolBar : public QToolBar {
public:
    explicit AppBackgroundSurfaceToolBar(const QString& title, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class AppBackgroundSurfaceStatusBar : public QStatusBar {
public:
    explicit AppBackgroundSurfaceStatusBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

}  // namespace miacode::ui
