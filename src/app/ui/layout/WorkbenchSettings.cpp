#include "layout/WorkbenchSettings.h"

#include "runtime/Shared.h"
#include "AppVersion.h"
#include "ui/preferences/PreferenceDocument.h"
#include "ui/theme/ThemeVariantResolver.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <QDate>
#include <QStringList>
#include <QSettings>
#include <QSysInfo>
#include <QtGlobal>


namespace miacode::ui {
namespace {
constexpr auto kUiSection = "ui";
constexpr auto kSidebarVisible = "sidebar_visible";
constexpr auto kSidebarWidth = "sidebar_width";
constexpr auto kBottomPanelVisible = "bottom_panel_visible";
constexpr auto kBottomPanelHeightRatio = "bottom_panel_height_ratio";
constexpr auto kPreviewWidthRatio = "preview_width_ratio";
constexpr auto kPreviewCanvasFreeAspect = "preview_canvas_free_aspect";
constexpr auto kFontSize = "ui_font_size";
constexpr auto kEditorScrollPastEnd = "editor_scroll_past_end";
constexpr auto kEditorSelectionBeatDisplay = "editor_selection_beat_display";

constexpr auto kLegacySidebarVisible = "ui/sidebarVisible";
constexpr auto kLegacySidebarWidth = "ui/sidebarWidth";
constexpr auto kLegacyBottomPanelVisible = "ui/bottomPanelVisible";
constexpr auto kLegacyBottomPanelHeightRatio = "ui/bottomPanelHeightRatio";
constexpr auto kLegacyPreviewWidthRatio = "ui/previewWidthRatio";
constexpr auto kLegacyPreviewCanvasFreeAspect = "ui/previewCanvasFreeAspect";
constexpr auto kLegacyFontSize = "appearance/fontSize";

QJsonObject loadUiObject()
{
    return PreferenceDocument::loadPreferencesObject().value(QLatin1String(kUiSection)).toObject();
}

void storeUiValue(const char* key, const QJsonValue& value)
{
    QJsonObject root = PreferenceDocument::loadPreferencesObject();
    QJsonObject ui = root.value(QLatin1String(kUiSection)).toObject();
    ui.insert(QLatin1String(key), value);
    root.insert(QLatin1String(kUiSection), ui);
    PreferenceDocument::savePreferencesObject(root);
}

bool jsonBool(const QJsonObject& ui, const char* key, QSettings& legacy, const char* legacyKey, bool fallback)
{
    if (ui.contains(QLatin1String(key))) {
        return ui.value(QLatin1String(key)).toBool(fallback);
    }
    return legacy.value(QLatin1String(legacyKey), fallback).toBool();
}

int jsonInt(const QJsonObject& ui, const char* key, QSettings& legacy, const char* legacyKey, int fallback)
{
    if (ui.contains(QLatin1String(key))) {
        return ui.value(QLatin1String(key)).toInt(fallback);
    }
    return legacy.value(QLatin1String(legacyKey), fallback).toInt();
}

double jsonDouble(const QJsonObject& ui, const char* key, QSettings& legacy, const char* legacyKey, double fallback)
{
    if (ui.contains(QLatin1String(key))) {
        return ui.value(QLatin1String(key)).toDouble(fallback);
    }
    return legacy.value(QLatin1String(legacyKey), fallback).toDouble();
}
}

WorkbenchSettings::WorkbenchSettings(QObject* parent)
    : QObject(parent)
{
    uiFontFamily_ = QGuiApplication::font().family();

    // 启动时读取并约束到界面可接受范围。无 json 键时回退到旧 QSettings，供 macOS 上已有记录迁入。
    const QJsonObject ui = loadUiObject();
    QSettings legacySettings;
    sidebarVisible_ = jsonBool(ui, kSidebarVisible, legacySettings, kLegacySidebarVisible, true);
    sidebarWidth_ = qBound(kSidebarMinimumContentWidth,
                           jsonInt(ui, kSidebarWidth, legacySettings, kLegacySidebarWidth, 190),
                           kSidebarMaximumContentWidth);
    bottomPanelVisible_ = jsonBool(ui, kBottomPanelVisible, legacySettings, kLegacyBottomPanelVisible, true);
    bottomPanelHeightRatio_ = qBound(kBottomPanelMinimumHeightRatio,
                                     jsonDouble(ui, kBottomPanelHeightRatio, legacySettings,
                                                kLegacyBottomPanelHeightRatio, 0.35),
                                     kBottomPanelMaximumHeightRatio);
    previewWidthRatio_ = qBound(kPreviewMinimumWidthRatio,
                                jsonDouble(ui, kPreviewWidthRatio, legacySettings,
                                           kLegacyPreviewWidthRatio, 0.5),
                                kPreviewMaximumWidthRatio);
    previewCanvasFreeAspect_ = jsonBool(ui, kPreviewCanvasFreeAspect, legacySettings,
                                        kLegacyPreviewCanvasFreeAspect, false);
    fontSize_ = qBound(12, jsonInt(ui, kFontSize, legacySettings, kLegacyFontSize, 13), 14);
    if (!ui.contains(QLatin1String(kSidebarVisible))
        || !ui.contains(QLatin1String(kSidebarWidth))
        || !ui.contains(QLatin1String(kBottomPanelVisible))
        || !ui.contains(QLatin1String(kBottomPanelHeightRatio))
        || !ui.contains(QLatin1String(kPreviewWidthRatio))
        || !ui.contains(QLatin1String(kFontSize))) {
        QJsonObject root = PreferenceDocument::loadPreferencesObject();
        QJsonObject nextUi = root.value(QLatin1String(kUiSection)).toObject();
        nextUi.insert(QLatin1String(kSidebarVisible), sidebarVisible_);
        nextUi.insert(QLatin1String(kSidebarWidth), sidebarWidth_);
        nextUi.insert(QLatin1String(kBottomPanelVisible), bottomPanelVisible_);
        nextUi.insert(QLatin1String(kBottomPanelHeightRatio), bottomPanelHeightRatio_);
        nextUi.insert(QLatin1String(kPreviewWidthRatio), previewWidthRatio_);
        nextUi.insert(QLatin1String(kFontSize), fontSize_);
        root.insert(QLatin1String(kUiSection), nextUi);
        PreferenceDocument::savePreferencesObject(root);
    }
    reloadTheme();
    if (QGuiApplication::styleHints() != nullptr) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &WorkbenchSettings::reloadTheme);
    }
    reloadEditorSettings();
}

QVariantMap WorkbenchSettings::aboutInfo() const
{
    QString version = QString::fromLatin1(MIACODE_VERSION_STRING).trimmed();
    const QStringList compilerDate = QStringLiteral(__DATE__).simplified().split(QLatin1Char(' '));
    const QStringList monthNames = {
        QStringLiteral("Jan"), QStringLiteral("Feb"), QStringLiteral("Mar"),
        QStringLiteral("Apr"), QStringLiteral("May"), QStringLiteral("Jun"),
        QStringLiteral("Jul"), QStringLiteral("Aug"), QStringLiteral("Sep"),
        QStringLiteral("Oct"), QStringLiteral("Nov"), QStringLiteral("Dec")
    };
    if (compilerDate.size() == 3) {
        const int month = monthNames.indexOf(compilerDate.at(0)) + 1;
        const QDate buildDate(compilerDate.at(2).toInt(), month, compilerDate.at(1).toInt());
        if (buildDate.isValid()) {
            version += QStringLiteral(" (") + buildDate.toString(QStringLiteral("yyyyMMdd"))
                + QStringLiteral(")");
        }
    }
    if (version.isEmpty()) {
        version = QCoreApplication::applicationVersion().trimmed();
    }
    if (version.isEmpty()) {
        version = QStringLiteral("0.0.0");
    }
    return QVariantMap{
        {QStringLiteral("version"), version},
        {QStringLiteral("platform"), QStringLiteral("%1 / %2 / %3")
                                         .arg(QSysInfo::productType())
                                         .arg(QSysInfo::currentCpuArchitecture())
                                         .arg(QSysInfo::buildAbi())},
        {QStringLiteral("buildType"),
#ifdef NDEBUG
         QStringLiteral("Release")
#else
         QStringLiteral("Debug")
#endif
        },
        {QStringLiteral("platformLabel"), qtTrId("about.platform")},
        {QStringLiteral("buildTypeLabel"), qtTrId("about.build_type")},
        {QStringLiteral("title"), qtTrId("action.about")},
    };
}

bool WorkbenchSettings::sidebarVisible() const { return sidebarVisible_; }
int WorkbenchSettings::sidebarWidth() const { return sidebarWidth_; }
int WorkbenchSettings::sidebarMinimumContentWidth() const { return kSidebarMinimumContentWidth; }
int WorkbenchSettings::sidebarMaximumContentWidth() const { return kSidebarMaximumContentWidth; }
bool WorkbenchSettings::bottomPanelVisible() const { return bottomPanelVisible_; }
double WorkbenchSettings::bottomPanelHeightRatio() const { return bottomPanelHeightRatio_; }
double WorkbenchSettings::bottomPanelMinimumHeightRatio() const { return kBottomPanelMinimumHeightRatio; }
double WorkbenchSettings::bottomPanelMaximumHeightRatio() const { return kBottomPanelMaximumHeightRatio; }
double WorkbenchSettings::previewWidthRatio() const { return previewWidthRatio_; }
double WorkbenchSettings::previewMinimumWidthRatio() const { return kPreviewMinimumWidthRatio; }
double WorkbenchSettings::previewMaximumWidthRatio() const { return kPreviewMaximumWidthRatio; }
bool WorkbenchSettings::previewCanvasFreeAspect() const { return previewCanvasFreeAspect_; }
QString WorkbenchSettings::uiFontFamily() const { return uiFontFamily_; }
QString WorkbenchSettings::themeModeToken() const
{
    return PreferenceDocument::themePreferenceToken(PreferenceDocument::preferredTheme());
}
QString WorkbenchSettings::lightThemeToken() const
{
    return PreferenceDocument::themePaletteToken(PreferenceDocument::preferredLightTheme());
}
QString WorkbenchSettings::darkThemeToken() const
{
    return PreferenceDocument::themePaletteToken(PreferenceDocument::preferredDarkTheme());
}
QString WorkbenchSettings::activeThemeToken() const
{
    if (PreferenceDocument::preferredTheme() == PreferenceDocument::ThemePreference::Legacy) {
        return QStringLiteral("legacy");
    }
    const bool darkAppearance = miacode::ui::ThemeVariantResolver::resolve(
        PreferenceDocument::preferredTheme()) == miacode::ui::ThemeVariant::Dark;
    return darkAppearance ? darkThemeToken() : lightThemeToken();
}
bool WorkbenchSettings::darkTheme() const { return darkTheme_; }
QFont WorkbenchSettings::codeFont() const { return codeFont_; }
int WorkbenchSettings::editorBlockSpacing() const { return editorBlockSpacing_; }
int WorkbenchSettings::fontSize() const { return fontSize_; }
bool WorkbenchSettings::editorHalfWidthInputEnabled() const { return editorHalfWidthInputEnabled_; }
bool WorkbenchSettings::editorOverwriteModeEnabled() const { return editorOverwriteModeEnabled_; }
bool WorkbenchSettings::editorAutoCompletionEnabled() const { return editorAutoCompletionEnabled_; }
bool WorkbenchSettings::editorImeInputDisabled() const { return editorImeInputDisabled_; }
bool WorkbenchSettings::editorScrollPastEnd() const { return editorScrollPastEnd_; }
bool WorkbenchSettings::editorSelectionBeatDisplay() const { return editorSelectionBeatDisplay_; }

void WorkbenchSettings::reloadEditorSettings()
{
    const QJsonObject editorUi = PreferenceDocument::loadPreferencesObject().value(QStringLiteral("ui")).toObject();
    const int fontPointSize = qBound(
        miacode::runtime::shared::kEditorTextFontSizeMin,
        editorUi.value(QStringLiteral("editor_text_font_size")).toInt(
            miacode::runtime::shared::editorFont().pointSize()),
        miacode::runtime::shared::kEditorTextFontSizeMax);
    const double lineSpacingFactor = miacode::runtime::shared::normalizeEditorLineSpacingFactor(
        editorUi.value(QStringLiteral("editor_line_spacing_factor")).toDouble(
            miacode::runtime::shared::kEditorLineSpacingFactorDefault));
    const QFont codeFont = miacode::runtime::shared::editorFont(fontPointSize);
    const int blockSpacing = miacode::runtime::shared::blockSpacingPixelsForPointSize(
        fontPointSize, lineSpacingFactor);
    const bool halfWidth = editorUi.value(QStringLiteral("editor_half_width_input")).toBool(true);
    const bool overwrite = editorUi.value(QStringLiteral("editor_overwrite_mode")).toBool(false);
    const bool autoCompletion = editorUi.value(QStringLiteral("editor_auto_completion")).toBool(true);
    const bool imeDisabled = editorUi.value(QStringLiteral("editor_ime_input_disabled")).toBool(true);
    const bool scrollPastEnd = editorUi.value(QLatin1String(kEditorScrollPastEnd)).toBool(true);
    const bool selectionBeatDisplay =
        editorUi.value(QLatin1String(kEditorSelectionBeatDisplay)).toBool(true);
    if (codeFont_ == codeFont
        && editorBlockSpacing_ == blockSpacing
        && editorHalfWidthInputEnabled_ == halfWidth
        && editorOverwriteModeEnabled_ == overwrite
        && editorAutoCompletionEnabled_ == autoCompletion
        && editorImeInputDisabled_ == imeDisabled
        && editorScrollPastEnd_ == scrollPastEnd
        && editorSelectionBeatDisplay_ == selectionBeatDisplay) {
        return;
    }
    codeFont_ = codeFont;
    editorBlockSpacing_ = blockSpacing;
    editorHalfWidthInputEnabled_ = halfWidth;
    editorOverwriteModeEnabled_ = overwrite;
    editorAutoCompletionEnabled_ = autoCompletion;
    editorImeInputDisabled_ = imeDisabled;
    editorScrollPastEnd_ = scrollPastEnd;
    editorSelectionBeatDisplay_ = selectionBeatDisplay;
    emit editorSettingsChanged();
}

void WorkbenchSettings::reloadTheme()
{
    const bool darkAppearance = miacode::ui::ThemeVariantResolver::resolve(
        PreferenceDocument::preferredTheme()) == miacode::ui::ThemeVariant::Dark;
    const PreferenceDocument::ThemePalette activePalette =
        PreferenceDocument::preferredTheme() == PreferenceDocument::ThemePreference::Legacy
        ? PreferenceDocument::ThemePalette::Legacy
        : (darkAppearance
               ? PreferenceDocument::preferredDarkTheme()
               : PreferenceDocument::preferredLightTheme());
    const bool next = PreferenceDocument::themePaletteIsDark(activePalette);
    const QString nextModeToken = themeModeToken();
    const QString nextLightThemeToken = lightThemeToken();
    const QString nextDarkThemeToken = darkThemeToken();
    if (darkTheme_ == next
        && publishedThemeModeToken_ == nextModeToken
        && publishedLightThemeToken_ == nextLightThemeToken
        && publishedDarkThemeToken_ == nextDarkThemeToken) {
        return;
    }
    darkTheme_ = next;
    publishedThemeModeToken_ = nextModeToken;
    publishedLightThemeToken_ = nextLightThemeToken;
    publishedDarkThemeToken_ = nextDarkThemeToken;
    emit themeChanged();
}

void WorkbenchSettings::setThemeModeToken(const QString& token)
{
    const PreferenceDocument::ThemePreference next = PreferenceDocument::themePreferenceFromToken(token);
    if (next == PreferenceDocument::preferredTheme()) {
        return;
    }
    PreferenceDocument::setPreferredTheme(next);
    // PreferenceDocument only stores and persists — it notifies nobody. Without this the
    // stored preference changes and the timeline follows it (a QSG item reading
    // UiTheme::colors() on its next repaint), while every QML surface stays on
    // the old palette until the next launch. The OS colour-scheme change
    // already ends here; the user's own choice has to as well.
    reloadTheme();
}

void WorkbenchSettings::setLightThemeToken(const QString& token)
{
    const PreferenceDocument::ThemePalette next = PreferenceDocument::themePaletteFromToken(token);
    if (next == PreferenceDocument::preferredLightTheme()) {
        return;
    }
    PreferenceDocument::setPreferredLightTheme(next);
    reloadTheme();
}

void WorkbenchSettings::setDarkThemeToken(const QString& token)
{
    const PreferenceDocument::ThemePalette next = PreferenceDocument::themePaletteFromToken(token);
    if (next == PreferenceDocument::preferredDarkTheme()) {
        return;
    }
    PreferenceDocument::setPreferredDarkTheme(next);
    reloadTheme();
}

void WorkbenchSettings::setSidebarVisible(bool value)
{
    if (sidebarVisible_ == value) return;
    sidebarVisible_ = value;
    storeUiValue(kSidebarVisible, value);
    emit sidebarVisibleChanged();
}

void WorkbenchSettings::setSidebarWidth(int value)
{
    value = qBound(kSidebarMinimumContentWidth, value, kSidebarMaximumContentWidth);
    if (sidebarWidth_ == value) return;
    sidebarWidth_ = value;
    storeUiValue(kSidebarWidth, value);
    emit sidebarWidthChanged();
}

void WorkbenchSettings::setBottomPanelVisible(bool value)
{
    if (bottomPanelVisible_ == value) return;
    bottomPanelVisible_ = value;
    storeUiValue(kBottomPanelVisible, value);
    emit bottomPanelVisibleChanged();
}

void WorkbenchSettings::setBottomPanelHeightRatio(double value)
{
    value = qBound(kBottomPanelMinimumHeightRatio, value, kBottomPanelMaximumHeightRatio);
    if (qFuzzyCompare(bottomPanelHeightRatio_, value)) return;
    bottomPanelHeightRatio_ = value;
    storeUiValue(kBottomPanelHeightRatio, value);
    emit bottomPanelHeightRatioChanged();
}

void WorkbenchSettings::setPreviewWidthRatio(double value)
{
    value = qBound(kPreviewMinimumWidthRatio, value, kPreviewMaximumWidthRatio);
    if (qFuzzyCompare(previewWidthRatio_, value)) return;
    previewWidthRatio_ = value;
    storeUiValue(kPreviewWidthRatio, value);
    emit previewWidthRatioChanged();
}

void WorkbenchSettings::setPreviewCanvasFreeAspect(bool value)
{
    if (previewCanvasFreeAspect_ == value) return;
    previewCanvasFreeAspect_ = value;
    storeUiValue(kPreviewCanvasFreeAspect, value);
    emit previewCanvasFreeAspectChanged();
}

void WorkbenchSettings::setEditorAppearance(int pointSize, double lineSpacingFactor)
{
    const QFont font = miacode::runtime::shared::editorFont(pointSize);
    const int blockSpacing =
        miacode::runtime::shared::blockSpacingPixelsForPointSize(pointSize, lineSpacingFactor);
    if (codeFont_ == font && editorBlockSpacing_ == blockSpacing) {
        return;
    }
    codeFont_ = font;
    editorBlockSpacing_ = blockSpacing;
    emit editorSettingsChanged();
}

void WorkbenchSettings::setEditorScrollPastEnd(bool enabled)
{
    if (editorScrollPastEnd_ == enabled) {
        return;
    }
    editorScrollPastEnd_ = enabled;
    storeUiValue(kEditorScrollPastEnd, enabled);
    emit editorSettingsChanged();
}

void WorkbenchSettings::setEditorSelectionBeatDisplay(bool enabled)
{
    if (editorSelectionBeatDisplay_ == enabled) {
        return;
    }
    editorSelectionBeatDisplay_ = enabled;
    storeUiValue(kEditorSelectionBeatDisplay, enabled);
    emit editorSettingsChanged();
}

void WorkbenchSettings::setFontSize(int value)
{
    value = qBound(12, value, 14);
    if (fontSize_ == value) return;
    fontSize_ = value;
    storeUiValue(kFontSize, value);
    emit fontSizeChanged();
}

} // namespace miacode::ui
