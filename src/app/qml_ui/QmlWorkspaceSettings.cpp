#include "QmlWorkspaceSettings.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QtGlobal>

namespace {
constexpr auto kSidebarVisible = "workbench/sidebarVisible";
constexpr auto kSidebarWidth = "workbench/sidebarWidth";
constexpr auto kBottomPanelVisible = "workbench/bottomPanelVisible";
constexpr auto kBottomPanelHeight = "workbench/bottomPanelHeight";
constexpr auto kPreviewVisible = "workbench/previewVisible";
constexpr auto kPreviewWidthRatio = "workbench/previewWidthRatio";
constexpr auto kFontSize = "appearance/fontSize";
constexpr auto kTimelineLength = "timeline/length";
constexpr auto kTimelineAutoCenter = "timeline/autoCenter";
}

QmlWorkspaceSettings::QmlWorkspaceSettings(QObject* parent)
    : QObject(parent)
{
    uiFontFamily_ = QGuiApplication::font().family();

    // The QML source editor uses the platform fixed-pitch font so glyph
    // metrics remain consistent across every supported desktop platform.
    codeFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codeFont_.setStyleHint(QFont::Monospace);
    codeFont_.setFixedPitch(true);
    codeFont_.setPixelSize(14);

    // 所有值在程序启动时一次性恢复并约束到界面可接受范围。配置文件中的
    // 旧值即使来自不同尺寸的显示器，也不会让关键工作区完全离开可见范围。
    sidebarVisible_ = settings_.value(kSidebarVisible, true).toBool();
    sidebarWidth_ = qBound(120, settings_.value(kSidebarWidth, 190).toInt(), 320);
    bottomPanelVisible_ = settings_.value(kBottomPanelVisible, true).toBool();
    bottomPanelHeight_ = qBound(120, settings_.value(kBottomPanelHeight, 215).toInt(), 340);
    previewVisible_ = settings_.value(kPreviewVisible, true).toBool();
    previewWidthRatio_ = qBound(0.2, settings_.value(kPreviewWidthRatio, 0.5).toDouble(), 0.5);
    fontSize_ = qBound(12, settings_.value(kFontSize, 13).toInt(), 14);
    timelineLength_ = qBound(1200, settings_.value(kTimelineLength, 2400).toInt(), 4800);
    timelineAutoCenter_ = settings_.value(kTimelineAutoCenter, true).toBool();
}

bool QmlWorkspaceSettings::sidebarVisible() const { return sidebarVisible_; }
int QmlWorkspaceSettings::sidebarWidth() const { return sidebarWidth_; }
bool QmlWorkspaceSettings::bottomPanelVisible() const { return bottomPanelVisible_; }
int QmlWorkspaceSettings::bottomPanelHeight() const { return bottomPanelHeight_; }
bool QmlWorkspaceSettings::previewVisible() const { return previewVisible_; }
double QmlWorkspaceSettings::previewWidthRatio() const { return previewWidthRatio_; }
QString QmlWorkspaceSettings::uiFontFamily() const { return uiFontFamily_; }
QFont QmlWorkspaceSettings::codeFont() const { return codeFont_; }
int QmlWorkspaceSettings::fontSize() const { return fontSize_; }
int QmlWorkspaceSettings::timelineLength() const { return timelineLength_; }
bool QmlWorkspaceSettings::timelineAutoCenter() const { return timelineAutoCenter_; }

void QmlWorkspaceSettings::setSidebarVisible(bool value)
{
    if (sidebarVisible_ == value) return;
    sidebarVisible_ = value;
    settings_.setValue(kSidebarVisible, value);
    emit sidebarVisibleChanged();
}

void QmlWorkspaceSettings::setSidebarWidth(int value)
{
    value = qBound(120, value, 320);
    if (sidebarWidth_ == value) return;
    sidebarWidth_ = value;
    settings_.setValue(kSidebarWidth, value);
    emit sidebarWidthChanged();
}

void QmlWorkspaceSettings::setBottomPanelVisible(bool value)
{
    if (bottomPanelVisible_ == value) return;
    bottomPanelVisible_ = value;
    settings_.setValue(kBottomPanelVisible, value);
    emit bottomPanelVisibleChanged();
}

void QmlWorkspaceSettings::setBottomPanelHeight(int value)
{
    value = qBound(120, value, 340);
    if (bottomPanelHeight_ == value) return;
    bottomPanelHeight_ = value;
    settings_.setValue(kBottomPanelHeight, value);
    emit bottomPanelHeightChanged();
}

void QmlWorkspaceSettings::setPreviewVisible(bool value)
{
    if (previewVisible_ == value) return;
    previewVisible_ = value;
    settings_.setValue(kPreviewVisible, value);
    emit previewVisibleChanged();
}

void QmlWorkspaceSettings::setPreviewWidthRatio(double value)
{
    value = qBound(0.2, value, 0.5);
    if (qFuzzyCompare(previewWidthRatio_, value)) return;
    previewWidthRatio_ = value;
    settings_.setValue(kPreviewWidthRatio, value);
    emit previewWidthRatioChanged();
}

void QmlWorkspaceSettings::setFontSize(int value)
{
    value = qBound(12, value, 14);
    if (fontSize_ == value) return;
    fontSize_ = value;
    settings_.setValue(kFontSize, value);
    emit fontSizeChanged();
}

void QmlWorkspaceSettings::setTimelineLength(int value)
{
    value = qBound(1200, value, 4800);
    if (timelineLength_ == value) return;
    timelineLength_ = value;
    settings_.setValue(kTimelineLength, value);
    emit timelineLengthChanged();
}

void QmlWorkspaceSettings::setTimelineAutoCenter(bool value)
{
    if (timelineAutoCenter_ == value) return;
    timelineAutoCenter_ = value;
    settings_.setValue(kTimelineAutoCenter, value);
    emit timelineAutoCenterChanged();
}

void QmlWorkspaceSettings::resetToDefaults()
{
    setSidebarVisible(true);
    setSidebarWidth(190);
    setBottomPanelVisible(true);
    setBottomPanelHeight(215);
    setPreviewVisible(true);
    setPreviewWidthRatio(0.5);
    setFontSize(13);
    setTimelineLength(2400);
    setTimelineAutoCenter(true);
}
