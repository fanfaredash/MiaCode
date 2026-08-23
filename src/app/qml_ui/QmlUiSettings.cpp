#include "QmlUiSettings.h"

#include "mainwindow/MainWindowShared.h"

#include <QGuiApplication>
#include <QtGlobal>

namespace {
constexpr auto kSidebarVisible = "ui/sidebarVisible";
constexpr auto kSidebarWidth = "ui/sidebarWidth";
constexpr auto kBottomPanelVisible = "ui/bottomPanelVisible";
constexpr auto kPreviewVisible = "ui/previewVisible";
constexpr auto kPreviewWidthRatio = "ui/previewWidthRatio";
constexpr auto kFontSize = "appearance/fontSize";
}

QmlUiSettings::QmlUiSettings(QObject* parent)
    : QObject(parent)
{
    uiFontFamily_ = QGuiApplication::font().family();

    // Match v1 editorFont() platform defaults: Consolas 10 pt on Windows,
    // SF Mono / Menlo 13 pt on macOS (see MainWindowShared.cpp).
    codeFont_ = miacode::mainwindow::shared::editorFont();

    // 启动时读取并约束到界面可接受范围。
    sidebarVisible_ = settings_.value(kSidebarVisible, true).toBool();
    sidebarWidth_ = qBound(120, settings_.value(kSidebarWidth, 190).toInt(), 320);
    bottomPanelVisible_ = settings_.value(kBottomPanelVisible, true).toBool();
    previewVisible_ = settings_.value(kPreviewVisible, true).toBool();
    previewWidthRatio_ = qBound(0.2, settings_.value(kPreviewWidthRatio, 0.5).toDouble(), 0.5);
    fontSize_ = qBound(12, settings_.value(kFontSize, 13).toInt(), 14);
}

bool QmlUiSettings::sidebarVisible() const { return sidebarVisible_; }
int QmlUiSettings::sidebarWidth() const { return sidebarWidth_; }
bool QmlUiSettings::bottomPanelVisible() const { return bottomPanelVisible_; }
bool QmlUiSettings::previewVisible() const { return previewVisible_; }
double QmlUiSettings::previewWidthRatio() const { return previewWidthRatio_; }
QString QmlUiSettings::uiFontFamily() const { return uiFontFamily_; }
QFont QmlUiSettings::codeFont() const { return codeFont_; }
int QmlUiSettings::fontSize() const { return fontSize_; }

void QmlUiSettings::setSidebarVisible(bool value)
{
    if (sidebarVisible_ == value) return;
    sidebarVisible_ = value;
    settings_.setValue(kSidebarVisible, value);
    emit sidebarVisibleChanged();
}

void QmlUiSettings::setSidebarWidth(int value)
{
    value = qBound(120, value, 320);
    if (sidebarWidth_ == value) return;
    sidebarWidth_ = value;
    settings_.setValue(kSidebarWidth, value);
    emit sidebarWidthChanged();
}

void QmlUiSettings::setBottomPanelVisible(bool value)
{
    if (bottomPanelVisible_ == value) return;
    bottomPanelVisible_ = value;
    settings_.setValue(kBottomPanelVisible, value);
    emit bottomPanelVisibleChanged();
}

void QmlUiSettings::setPreviewVisible(bool value)
{
    if (previewVisible_ == value) return;
    previewVisible_ = value;
    settings_.setValue(kPreviewVisible, value);
    emit previewVisibleChanged();
}

void QmlUiSettings::setPreviewWidthRatio(double value)
{
    value = qBound(0.2, value, 0.5);
    if (qFuzzyCompare(previewWidthRatio_, value)) return;
    previewWidthRatio_ = value;
    settings_.setValue(kPreviewWidthRatio, value);
    emit previewWidthRatioChanged();
}

void QmlUiSettings::setFontSize(int value)
{
    value = qBound(12, value, 14);
    if (fontSize_ == value) return;
    fontSize_ = value;
    settings_.setValue(kFontSize, value);
    emit fontSizeChanged();
}
