#include "tools/video_export/HudFontSettings.h"

#include "UiText.h"
#include "UiTheme.h"
#include "core/scene/PreviewHudState.h"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace miacode::video_export {
namespace {

// (Moved verbatim from VideoExportDialog.cpp, 2026-06-10, so the 视频设置
// dialog can host the same font page without depending on the export dialog.)

QString uiText(const char* key, const QString& fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? fallback : translated;
}

struct HudFontChoice {
    QString label;
    QString path;
    QString family;
};

QString hudFontLibraryDirPath()
{
    const QFileInfo preferencesInfo(UiText::preferencesFilePath());
    return preferencesInfo.absoluteDir().filePath(QStringLiteral("fonts"));
}

QString currentHudFontPath()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    const QJsonObject videoExport = app.value(QStringLiteral("video_export")).toObject();
    const QString path = videoExport.value(QStringLiteral("hud_font_path")).toString();
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
}

QString uniqueHudFontLibraryPath(const QFileInfo& sourceInfo)
{
    QDir dir(hudFontLibraryDirPath());
    dir.mkpath(QStringLiteral("."));
    const QString baseName = sourceInfo.completeBaseName().isEmpty()
        ? QStringLiteral("font")
        : sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix().isEmpty() ? QStringLiteral("ttf") : sourceInfo.suffix();
    QString candidate = dir.filePath(baseName + QLatin1Char('.') + suffix);
    int copyIndex = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QStringLiteral("%1_%2.%3").arg(baseName).arg(copyIndex).arg(suffix));
        ++copyIndex;
    }
    return QFileInfo(candidate).absoluteFilePath();
}

QString fontFamilyForFile(const QString& path)
{
    const int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId < 0) {
        return QString();
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    return families.isEmpty() ? QString() : families.first();
}

QVector<HudFontChoice> hudFontChoices()
{
    QVector<HudFontChoice> choices;
    choices.push_back({
        UiText::text(QStringLiteral("dialog.video_export.option.hud_font_default")).isEmpty()
            ? QStringLiteral("Default font")
            : UiText::text(QStringLiteral("dialog.video_export.option.hud_font_default")),
        QString(),
        QString()
    });

    QDir dir(hudFontLibraryDirPath());
    const QFileInfoList files = dir.entryInfoList(
        QStringList{QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase
    );
    for (const QFileInfo& file : files) {
        const QString path = file.absoluteFilePath();
        const QString family = fontFamilyForFile(path);
        if (family.isEmpty()) {
            continue;
        }
        choices.push_back({
            QStringLiteral("%1 (%2)").arg(family, file.fileName()),
            path,
            family
        });
    }
    return choices;
}

void populateHudFontCombo(QComboBox* combo, const QString& selectedPath)
{
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    const QVector<HudFontChoice> choices = hudFontChoices();
    int selectedIndex = 0;
    const QString normalizedSelected = selectedPath.isEmpty()
        ? QString()
        : QFileInfo(selectedPath).absoluteFilePath();
    for (int i = 0; i < choices.size(); ++i) {
        combo->addItem(choices[i].label, choices[i].path);
        if (!normalizedSelected.isEmpty()
            && QFileInfo(choices[i].path).absoluteFilePath() == normalizedSelected) {
            selectedIndex = i;
        }
    }
    combo->setCurrentIndex(selectedIndex);
}

void notifyFontChanged(const std::function<void()>& onFontChanged)
{
    if (onFontChanged) {
        onFontChanged();
    }
}

// Pick a .ttf/.otf, copy it into the font library, and make it the active HUD
// font. Returns the library path of the imported font (empty on cancel/error).
QString importHudFontFromUser(QWidget* parent, const std::function<void()>& onFontChanged)
{
    const QString selected = QFileDialog::getOpenFileName(
        parent,
        uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
        QString(),
        QStringLiteral("Font Files (*.ttf *.otf)")
    );
    if (selected.isEmpty()) {
        return QString();
    }
    const QFileInfo info(selected);
    const QString suffix = info.suffix().toLower();
    if (!info.isFile() || (suffix != QStringLiteral("ttf") && suffix != QStringLiteral("otf"))) {
        QMessageBox::warning(
            parent,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.invalid_hud_font", QStringLiteral("Please select a .ttf or .otf font file."))
        );
        return QString();
    }

    const int fontId = QFontDatabase::addApplicationFont(info.absoluteFilePath());
    const QStringList families = fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId) : QStringList();
    if (families.isEmpty()) {
        QMessageBox::warning(
            parent,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.invalid_hud_font", QStringLiteral("Please select a .ttf or .otf font file."))
        );
        return QString();
    }
    const QString targetPath = uniqueHudFontLibraryPath(info);
    if (!QFile::copy(info.absoluteFilePath(), targetPath)) {
        QMessageBox::warning(
            parent,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.copy_hud_font_failed", QStringLiteral("Failed to copy the font into the font library."))
        );
        return QString();
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(targetPath);
    notifyFontChanged(onFontChanged);
    return targetPath;
}

}  // namespace

void openHudFontSettingsDialog(QWidget* parent, const std::function<void()>& onFontChanged)
{
    QDialog dialog(parent);
    const QString title = uiText("dialog.video_export.option.hud_font_settings", QStringLiteral("Font Settings"));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowFlags((dialog.windowFlags() | Qt::FramelessWindowHint) & ~Qt::WindowContextHelpButtonHint);
    dialog.setStyleSheet(UiTheme::exportDialogStyleSheet());

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);

    auto* panel = new QFrame(&dialog);
    panel->setObjectName(QStringLiteral("VideoExportPrimaryPanel"));
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 12, 14, 14);
    panelLayout->setSpacing(10);

    auto* headerRow = new QWidget(panel);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    auto* titleLabel = new QLabel(title, headerRow);
    QFont titleFont = titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    auto* headerCloseButton = new QToolButton(headerRow);
    headerCloseButton->setText(QStringLiteral("×"));
    headerCloseButton->setFixedSize(28, 28);
    headerCloseButton->setAutoRaise(false);
    headerCloseButton->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    headerCloseButton->setToolTip(uiText("dialog.video_export.button.close", QStringLiteral("Close")));
    headerLayout->addWidget(titleLabel, 1);
    headerLayout->addWidget(headerCloseButton, 0);

    auto* currentLabel = new QLabel(panel);
    currentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* fontCombo = new QComboBox(panel);
    fontCombo->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    fontCombo->setMinimumWidth(320);
    auto* sampleLabel = new QLabel(panel);
    sampleLabel->setMinimumWidth(320);
    sampleLabel->setAlignment(Qt::AlignCenter);
    sampleLabel->setText(QStringLiteral("12:34:567  TAP  HOLD  SLIDE  101.0000%"));
    sampleLabel->setStyleSheet(QStringLiteral(
        "QLabel { min-height: 48px; padding: 8px 10px; border: 1px solid rgba(128,128,128,80);"
        " border-radius: 8px; background: rgba(128,128,128,20); color: palette(text); }"
    ));

    populateHudFontCombo(fontCombo, currentHudFontPath());

    const auto refreshDialogFont = [&](const QString& selectedPath = currentHudFontPath()) {
        populateHudFontCombo(fontCombo, selectedPath);
        const QString family = miacode::preview::scene::previewHudFontDisplayName();
        const QString displayName = family == QLatin1String("Default")
            ? uiText("dialog.video_export.option.hud_font_default", QStringLiteral("Default font"))
            : family;
        currentLabel->setText(
            uiText("dialog.video_export.option.hud_font_current", QStringLiteral("Current font: %1")).arg(displayName)
        );
        sampleLabel->setFont(miacode::preview::scene::previewHudTimestampFont(13, QFont::DemiBold));
    };
    refreshDialogFont();

    auto* buttonRow = new QWidget(panel);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    auto* importButton = new QPushButton(
        uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
        buttonRow
    );
    auto* resetButton = new QPushButton(
        uiText("dialog.video_export.option.reset_hud_font", QStringLiteral("Reset")),
        buttonRow
    );
    auto* closeButton = new QPushButton(
        uiText("dialog.video_export.button.close", QStringLiteral("Close")),
        buttonRow
    );
    importButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    resetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    buttonLayout->addWidget(importButton, 0);
    buttonLayout->addWidget(resetButton, 0);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton, 0);

    panelLayout->addWidget(headerRow, 0);
    panelLayout->addWidget(currentLabel, 0);
    panelLayout->addWidget(fontCombo, 0);
    panelLayout->addWidget(sampleLabel, 0);
    panelLayout->addWidget(buttonRow, 0);
    layout->addWidget(panel, 0);

    QObject::connect(fontCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        const QString selectedPath = fontCombo->itemData(index).toString();
        miacode::preview::scene::setPreviewHudCustomFontPath(selectedPath);
        notifyFontChanged(onFontChanged);
        const QString family = miacode::preview::scene::previewHudFontDisplayName();
        const QString displayName = family == QLatin1String("Default")
            ? uiText("dialog.video_export.option.hud_font_default", QStringLiteral("Default font"))
            : family;
        currentLabel->setText(
            uiText("dialog.video_export.option.hud_font_current", QStringLiteral("Current font: %1")).arg(displayName)
        );
        sampleLabel->setFont(miacode::preview::scene::previewHudTimestampFont(13, QFont::DemiBold));
    });
    QObject::connect(importButton, &QPushButton::clicked, &dialog, [&]() {
        const QString importedPath = importHudFontFromUser(&dialog, onFontChanged);
        if (!importedPath.isEmpty()) {
            refreshDialogFont(importedPath);
        }
    });
    QObject::connect(resetButton, &QPushButton::clicked, &dialog, [&]() {
        miacode::preview::scene::setPreviewHudCustomFontPath(QString());
        notifyFontChanged(onFontChanged);
        refreshDialogFont(QString());
    });
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(headerCloseButton, &QToolButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

}  // namespace miacode::video_export
