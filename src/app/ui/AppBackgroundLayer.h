#pragma once

#include "app/ui/AppBackgroundSettings.h"

#include <QFrame>
#include <QImage>
#include <QMenuBar>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

class QPainter;

namespace miacode::ui {

void paintAppBackgroundForWidget(QWidget* surface, QPainter& painter);

class AppBackgroundLayer final : public QWidget {
    Q_OBJECT

public:
    explicit AppBackgroundLayer(QWidget* parent = nullptr);

    AppBackgroundSettings settings() const;
    void setSettings(const AppBackgroundSettings& settings);
    void paintBackgroundForSurface(QWidget* surface, QPainter& painter);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void invalidateCache();
    bool ensureSourceLoaded();
    QImage blurredImage(const QImage& image) const;
    QSize canvasSize() const;
    QRect targetRectForImage(const QSize& imageSize, const QSize& canvasSize) const;
    QPoint alignedTopLeft(const QSize& drawSize, const QSize& canvasSize) const;
    QPixmap renderedPixmap(const QSize& canvasSize);

    AppBackgroundSettings settings_;
    QString loadedPath_;
    QImage sourceImage_;
    QPixmap cachedPixmap_;
    QSize cachedWidgetSize_;
    AppBackgroundSettings cachedSettings_;
};

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
