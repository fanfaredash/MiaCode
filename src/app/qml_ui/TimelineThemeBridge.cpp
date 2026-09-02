#include "TimelineThemeBridge.h"

#include "common/TimelineThemeConfig.h"

TimelineThemeBridge::TimelineThemeBridge(QObject* parent)
    : QObject(parent)
{
}

#define MIACODE_TIMELINE_THEME_SETTER(name, member, signalName) \
    void TimelineThemeBridge::set##name(QColor value) \
    { \
        if (member == value) { \
            return; \
        } \
        member = value; \
        pushChrome(); \
        emit signalName(); \
    }

MIACODE_TIMELINE_THEME_SETTER(WindowColor, window_, windowColorChanged)
MIACODE_TIMELINE_THEME_SETTER(HeaderColor, header_, headerColorChanged)
MIACODE_TIMELINE_THEME_SETTER(SidebarColor, sidebar_, sidebarColorChanged)
MIACODE_TIMELINE_THEME_SETTER(BaseColor, base_, baseColorChanged)
MIACODE_TIMELINE_THEME_SETTER(BorderColor, border_, borderColorChanged)
MIACODE_TIMELINE_THEME_SETTER(AxisColor, axis_, axisColorChanged)
MIACODE_TIMELINE_THEME_SETTER(GridMajorColor, gridMajor_, gridMajorColorChanged)
MIACODE_TIMELINE_THEME_SETTER(GridSubdivisionColor, gridSubdivision_, gridSubdivisionColorChanged)
MIACODE_TIMELINE_THEME_SETTER(GridMinorColor, gridMinor_, gridMinorColorChanged)
MIACODE_TIMELINE_THEME_SETTER(LaneEvenColor, laneEven_, laneEvenColorChanged)
MIACODE_TIMELINE_THEME_SETTER(LaneOddColor, laneOdd_, laneOddColorChanged)
MIACODE_TIMELINE_THEME_SETTER(LabelColor, label_, labelColorChanged)
MIACODE_TIMELINE_THEME_SETTER(TextSecondaryColor, textSecondary_, textSecondaryColorChanged)
MIACODE_TIMELINE_THEME_SETTER(WaveStrokeColor, waveStroke_, waveStrokeColorChanged)

#undef MIACODE_TIMELINE_THEME_SETTER

void TimelineThemeBridge::pushChrome()
{
    miacode::timeline::TimelineChromeColors chrome;
    chrome.window = window_;
    chrome.header = header_;
    chrome.sidebar = sidebar_;
    chrome.base = base_;
    chrome.border = border_;
    chrome.axis = axis_;
    chrome.gridMajor = gridMajor_;
    chrome.gridSubdivision = gridSubdivision_;
    chrome.gridMinor = gridMinor_;
    chrome.laneEven = laneEven_;
    chrome.laneOdd = laneOdd_;
    chrome.label = label_;
    chrome.textSecondary = textSecondary_;
    chrome.waveStroke = waveStroke_;
    miacode::timeline::setTimelineChromeColors(chrome);
}
