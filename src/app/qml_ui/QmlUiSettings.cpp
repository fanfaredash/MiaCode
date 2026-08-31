#include "QmlUiSettings.h"

#include "mainwindow/MainWindowShared.h"
#include "AppVersion.h"
#include "ui/UiText.h"
#include "ui/ThemeVariantResolver.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSysInfo>
#include <QtGlobal>

namespace {
constexpr auto kSidebarVisible = "ui/sidebarVisible";
constexpr auto kSidebarWidth = "ui/sidebarWidth";
constexpr auto kBottomPanelVisible = "ui/bottomPanelVisible";
constexpr auto kBottomPanelHeightRatio = "ui/bottomPanelHeightRatio";
constexpr auto kPreviewWidthRatio = "ui/previewWidthRatio";
constexpr auto kFontSize = "appearance/fontSize";
}

QmlUiSettings::QmlUiSettings(QObject* parent)
    : QObject(parent)
{
    uiFontFamily_ = QGuiApplication::font().family();

    // 启动时读取并约束到界面可接受范围。
    sidebarVisible_ = settings_.value(kSidebarVisible, true).toBool();
    sidebarWidth_ = qBound(kSidebarMinimumContentWidth,
                           settings_.value(kSidebarWidth, 190).toInt(),
                           kSidebarMaximumContentWidth);
    bottomPanelVisible_ = settings_.value(kBottomPanelVisible, true).toBool();
    bottomPanelHeightRatio_ = qBound(kBottomPanelMinimumHeightRatio,
                                     settings_.value(kBottomPanelHeightRatio, 0.35).toDouble(),
                                     kBottomPanelMaximumHeightRatio);
    previewWidthRatio_ = qBound(kPreviewMinimumWidthRatio,
                                settings_.value(kPreviewWidthRatio, 0.5).toDouble(),
                                kPreviewMaximumWidthRatio);
    fontSize_ = qBound(12, settings_.value(kFontSize, 13).toInt(), 14);
    reloadTheme();
    if (QGuiApplication::styleHints() != nullptr) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &QmlUiSettings::reloadTheme);
    }
    reloadEditorSettings();
}

QVariantMap QmlUiSettings::aboutInfo() const
{
    QString version = QString::fromLatin1(MIACODE_DISPLAY_VERSION_STRING).trimmed();
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
        {QStringLiteral("platformLabel"), UiText::text(QStringLiteral("about.platform"))},
        {QStringLiteral("buildTypeLabel"), UiText::text(QStringLiteral("about.build_type"))},
        {QStringLiteral("title"), UiText::text(QStringLiteral("action.about"))},
    };
}

QString QmlUiSettings::localizedText(const QString& key) const
{
    const QString value = UiText::text(key);
    if (value != key) {
        return value;
    }
    // QML originally supplied Chinese source strings rather than keys. Resolve
    // them through the same canonical three-language catalog; no QTranslator or
    // Qt Widgets UI path participates in this boundary.
    return UiText::textForQmlSource(key);
}

bool QmlUiSettings::sidebarVisible() const { return sidebarVisible_; }
int QmlUiSettings::sidebarWidth() const { return sidebarWidth_; }
int QmlUiSettings::sidebarMinimumContentWidth() const { return kSidebarMinimumContentWidth; }
int QmlUiSettings::sidebarMaximumContentWidth() const { return kSidebarMaximumContentWidth; }
bool QmlUiSettings::bottomPanelVisible() const { return bottomPanelVisible_; }
double QmlUiSettings::bottomPanelHeightRatio() const { return bottomPanelHeightRatio_; }
double QmlUiSettings::bottomPanelMinimumHeightRatio() const { return kBottomPanelMinimumHeightRatio; }
double QmlUiSettings::bottomPanelMaximumHeightRatio() const { return kBottomPanelMaximumHeightRatio; }
double QmlUiSettings::previewWidthRatio() const { return previewWidthRatio_; }
double QmlUiSettings::previewMinimumWidthRatio() const { return kPreviewMinimumWidthRatio; }
double QmlUiSettings::previewMaximumWidthRatio() const { return kPreviewMaximumWidthRatio; }
QString QmlUiSettings::uiFontFamily() const { return uiFontFamily_; }
QString QmlUiSettings::themeToken() const
{
    switch (UiText::preferredTheme()) {
    case UiText::ThemePreference::Light: return QStringLiteral("light");
    case UiText::ThemePreference::Dark: return QStringLiteral("dark");
    case UiText::ThemePreference::System: return QStringLiteral("system");
    }
    return QStringLiteral("system");
}
bool QmlUiSettings::darkTheme() const { return darkTheme_; }
QFont QmlUiSettings::codeFont() const { return codeFont_; }
int QmlUiSettings::editorBlockSpacing() const { return editorBlockSpacing_; }
int QmlUiSettings::fontSize() const { return fontSize_; }
bool QmlUiSettings::editorHalfWidthInputEnabled() const { return editorHalfWidthInputEnabled_; }
bool QmlUiSettings::editorOverwriteModeEnabled() const { return editorOverwriteModeEnabled_; }
bool QmlUiSettings::editorAutoCompletionEnabled() const { return editorAutoCompletionEnabled_; }
bool QmlUiSettings::editorImeInputDisabled() const { return editorImeInputDisabled_; }

void QmlUiSettings::reloadEditorSettings()
{
    const QJsonObject editorUi = UiText::loadPreferencesObject().value(QStringLiteral("ui")).toObject();
    const int fontPointSize = qBound(
        miacode::mainwindow::shared::kEditorTextFontSizeMin,
        editorUi.value(QStringLiteral("editor_text_font_size")).toInt(
            miacode::mainwindow::shared::editorFont().pointSize()),
        miacode::mainwindow::shared::kEditorTextFontSizeMax);
    const double lineSpacingFactor = miacode::mainwindow::shared::normalizeEditorLineSpacingFactor(
        editorUi.value(QStringLiteral("editor_line_spacing_factor")).toDouble(
            miacode::mainwindow::shared::kEditorLineSpacingFactorDefault));
    const QFont codeFont = miacode::mainwindow::shared::editorFont(fontPointSize);
    const int blockSpacing = miacode::mainwindow::shared::blockSpacingPixelsForPointSize(
        fontPointSize, lineSpacingFactor);
    const bool halfWidth = editorUi.value(QStringLiteral("editor_half_width_input")).toBool(true);
    const bool overwrite = editorUi.value(QStringLiteral("editor_overwrite_mode")).toBool(false);
    const bool autoCompletion = editorUi.value(QStringLiteral("editor_auto_completion")).toBool(true);
    const bool imeDisabled = editorUi.value(QStringLiteral("editor_ime_input_disabled")).toBool(true);
    if (codeFont_ == codeFont
        && editorBlockSpacing_ == blockSpacing
        && editorHalfWidthInputEnabled_ == halfWidth
        && editorOverwriteModeEnabled_ == overwrite
        && editorAutoCompletionEnabled_ == autoCompletion
        && editorImeInputDisabled_ == imeDisabled) {
        return;
    }
    codeFont_ = codeFont;
    editorBlockSpacing_ = blockSpacing;
    editorHalfWidthInputEnabled_ = halfWidth;
    editorOverwriteModeEnabled_ = overwrite;
    editorAutoCompletionEnabled_ = autoCompletion;
    editorImeInputDisabled_ = imeDisabled;
    emit editorSettingsChanged();
}

void QmlUiSettings::reloadTheme()
{
    const bool next = miacode::ui::ThemeVariantResolver::resolve(UiText::preferredTheme())
                      == miacode::ui::ThemeVariant::Dark;
    if (darkTheme_ == next) {
        return;
    }
    darkTheme_ = next;
    emit themeChanged();
}

void QmlUiSettings::setThemeToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    UiText::ThemePreference next = UiText::ThemePreference::System;
    if (normalized == QStringLiteral("light")) {
        next = UiText::ThemePreference::Light;
    } else if (normalized == QStringLiteral("dark")) {
        next = UiText::ThemePreference::Dark;
    }
    if (next == UiText::preferredTheme()) {
        return;
    }
    UiText::setPreferredTheme(next);
}

void QmlUiSettings::setSidebarVisible(bool value)
{
    if (sidebarVisible_ == value) return;
    sidebarVisible_ = value;
    settings_.setValue(kSidebarVisible, value);
    emit sidebarVisibleChanged();
}

void QmlUiSettings::setSidebarWidth(int value)
{
    value = qBound(kSidebarMinimumContentWidth, value, kSidebarMaximumContentWidth);
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

void QmlUiSettings::setBottomPanelHeightRatio(double value)
{
    value = qBound(kBottomPanelMinimumHeightRatio, value, kBottomPanelMaximumHeightRatio);
    if (qFuzzyCompare(bottomPanelHeightRatio_, value)) return;
    bottomPanelHeightRatio_ = value;
    settings_.setValue(kBottomPanelHeightRatio, value);
    emit bottomPanelHeightRatioChanged();
}

void QmlUiSettings::setPreviewWidthRatio(double value)
{
    value = qBound(kPreviewMinimumWidthRatio, value, kPreviewMaximumWidthRatio);
    if (qFuzzyCompare(previewWidthRatio_, value)) return;
    previewWidthRatio_ = value;
    settings_.setValue(kPreviewWidthRatio, value);
    emit previewWidthRatioChanged();
}

void QmlUiSettings::setEditorAppearance(int pointSize, double lineSpacingFactor)
{
    const QFont font = miacode::mainwindow::shared::editorFont(pointSize);
    const int blockSpacing =
        miacode::mainwindow::shared::blockSpacingPixelsForPointSize(pointSize, lineSpacingFactor);
    if (codeFont_ == font && editorBlockSpacing_ == blockSpacing) {
        return;
    }
    codeFont_ = font;
    editorBlockSpacing_ = blockSpacing;
    emit editorSettingsChanged();
}

void QmlUiSettings::setFontSize(int value)
{
    value = qBound(12, value, 14);
    if (fontSize_ == value) return;
    fontSize_ = value;
    settings_.setValue(kFontSize, value);
    emit fontSizeChanged();
}
