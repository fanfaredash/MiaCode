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
#include <QVBoxLayout>
#include <QVector>

namespace miacode::video_export {
namespace {

// (Moved verbatim from VideoExportDialog.cpp, 2026-06-10, so the 视频设置
// dialog can host the same font page without depending on the export dialog.)

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
        UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
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
            UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
            UiText::text(QStringLiteral("dialog.video_export.error.invalid_hud_font"))
        );
        return QString();
    }

    const int fontId = QFontDatabase::addApplicationFont(info.absoluteFilePath());
    const QStringList families = fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId) : QStringList();
    if (families.isEmpty()) {
        QMessageBox::warning(
            parent,
            UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
            UiText::text(QStringLiteral("dialog.video_export.error.invalid_hud_font"))
        );
        return QString();
    }
    const QString targetPath = uniqueHudFontLibraryPath(info);
    if (!QFile::copy(info.absoluteFilePath(), targetPath)) {
        QMessageBox::warning(
            parent,
            UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
            UiText::text(QStringLiteral("dialog.video_export.error.copy_hud_font_failed"))
        );
        return QString();
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(targetPath);
    notifyFontChanged(onFontChanged);
    return targetPath;
}

}  // namespace

QWidget* createHudFontSettingsWidget(
    QWidget* parent,
    const std::function<void()>& onFontChanged,
    std::function<void()>* refreshOut)
{
    auto* root = new QWidget(parent);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* fontCombo = new QComboBox(root);
    fontCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    UiTheme::styleDialogComboBox(fontCombo, 12);
    auto* sampleLabel = new QLabel(root);
    sampleLabel->setAlignment(Qt::AlignCenter);
    sampleLabel->setText(QStringLiteral("12:34:567  TAP  HOLD  SLIDE  101.0000%"));
    sampleLabel->setStyleSheet(QStringLiteral(
        "QLabel { min-height: 44px; padding: 8px 10px; border: 1px solid rgba(128,128,128,80);"
        " border-radius: 8px; background: rgba(128,128,128,20); color: palette(text); }"
    ));

    const auto applySampleFont = [sampleLabel]() {
        sampleLabel->setFont(miacode::preview::scene::previewHudTimestampFont(13, QFont::DemiBold));
    };

    const auto refreshFromPreferences = [fontCombo, applySampleFont]() {
        populateHudFontCombo(fontCombo, currentHudFontPath());
        UiTheme::styleDialogComboBox(fontCombo, 12);
        applySampleFont();
    };

    refreshFromPreferences();
    if (refreshOut != nullptr) {
        *refreshOut = refreshFromPreferences;
    }

    auto* buttonRow = new QWidget(root);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    auto* importButton = new QPushButton(
        UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
        buttonRow
    );
    auto* resetButton = new QPushButton(
        UiText::text(QStringLiteral("dialog.video_export.option.reset_hud_font")),
        buttonRow
    );
    for (QPushButton* button : {importButton, resetButton}) {
        button->setObjectName(QStringLiteral("DialogAuxiliaryButton"));
        button->setProperty("miacodeAuxiliaryButton", true);
        button->setCursor(Qt::PointingHandCursor);
    }
    importButton->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
    resetButton->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
    buttonLayout->addWidget(importButton, 0);
    buttonLayout->addWidget(resetButton, 0);
    buttonLayout->addStretch(1);

    layout->addWidget(fontCombo, 0);
    layout->addWidget(sampleLabel, 0);
    layout->addWidget(buttonRow, 0);

    QObject::connect(fontCombo, qOverload<int>(&QComboBox::currentIndexChanged), root, [fontCombo, applySampleFont, onFontChanged](int index) {
        const QString selectedPath = fontCombo->itemData(index).toString();
        miacode::preview::scene::setPreviewHudCustomFontPath(selectedPath);
        notifyFontChanged(onFontChanged);
        applySampleFont();
    });
    QObject::connect(importButton, &QPushButton::clicked, root, [root, fontCombo, applySampleFont, onFontChanged]() {
        const QString importedPath = importHudFontFromUser(root, onFontChanged);
        if (!importedPath.isEmpty()) {
            populateHudFontCombo(fontCombo, importedPath);
            UiTheme::styleDialogComboBox(fontCombo, 12);
            applySampleFont();
        }
    });
    QObject::connect(resetButton, &QPushButton::clicked, root, [fontCombo, applySampleFont, onFontChanged]() {
        miacode::preview::scene::setPreviewHudCustomFontPath(QString());
        notifyFontChanged(onFontChanged);
        populateHudFontCombo(fontCombo, QString());
        UiTheme::styleDialogComboBox(fontCombo, 12);
        applySampleFont();
    });

    return root;
}

}  // namespace miacode::video_export
