#include "tools/video_export/FontLibrary.h"

#include "UiText.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUrl>

namespace miacode::video_export {

QString fontLibraryDirPath()
{
    const QFileInfo preferencesInfo(UiText::preferencesFilePath());
    return preferencesInfo.absoluteDir().filePath(QStringLiteral("fonts"));
}

QString fontFamilyForFile(const QString& path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId < 0) {
        return QString();
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    return families.isEmpty() ? QString() : families.first();
}

QVector<FontLibraryEntry> fontLibraryEntries(bool includeDefault, const QString& defaultLabel)
{
    QVector<FontLibraryEntry> entries;
    if (includeDefault) {
        entries.push_back({
            defaultLabel.isEmpty() ? QStringLiteral("Default font") : defaultLabel,
            QString(),
            QString()
        });
    }

    QDir dir(fontLibraryDirPath());
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
        entries.push_back({
            QStringLiteral("%1 (%2)").arg(family, file.fileName()),
            path,
            family
        });
    }
    return entries;
}

namespace {

QString uniqueFontLibraryPath(const QFileInfo& sourceInfo)
{
    QDir dir(fontLibraryDirPath());
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

}  // namespace

QString importFontIntoLibrary(QWidget* parent)
{
    const QString title = UiText::text(QStringLiteral("card_font.import"));
    const QString selected = QFileDialog::getOpenFileName(
        parent,
        title,
        QString(),
        QStringLiteral("Font Files (*.ttf *.otf)")
    );
    if (selected.isEmpty()) {
        return QString();
    }
    const QFileInfo info(selected);
    const QString suffix = info.suffix().toLower();
    if (!info.isFile() || (suffix != QStringLiteral("ttf") && suffix != QStringLiteral("otf"))) {
        QMessageBox::warning(parent, title, UiText::text(QStringLiteral("card_font.invalid_font")));
        return QString();
    }
    // Validate it actually loads / carries a family before copying.
    const int fontId = QFontDatabase::addApplicationFont(info.absoluteFilePath());
    const QStringList families = fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId) : QStringList();
    if (families.isEmpty()) {
        QMessageBox::warning(parent, title, UiText::text(QStringLiteral("card_font.invalid_font")));
        return QString();
    }
    const QString targetPath = uniqueFontLibraryPath(info);
    if (!QFile::copy(info.absoluteFilePath(), targetPath)) {
        QMessageBox::warning(parent, title, UiText::text(QStringLiteral("card_font.copy_failed")));
        return QString();
    }
    return targetPath;
}

void populateFontCombo(QComboBox* combo,
                       const QString& selectedPath,
                       bool includeDefault,
                       const QString& defaultLabel)
{
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    const QVector<FontLibraryEntry> entries = fontLibraryEntries(includeDefault, defaultLabel);
    const QString normalizedSelected = selectedPath.isEmpty()
        ? QString()
        : QFileInfo(selectedPath).absoluteFilePath();
    int selectedIndex = 0;
    for (int i = 0; i < entries.size(); ++i) {
        combo->addItem(entries[i].label, entries[i].path);
        if (!normalizedSelected.isEmpty()
            && QFileInfo(entries[i].path).absoluteFilePath() == normalizedSelected) {
            selectedIndex = i;
        }
    }
    if (combo->count() > 0) {
        combo->setCurrentIndex(selectedIndex);
    }
}

void applyBannerFontOverride(QVariantMap& templateMap,
                             const QString& displayPath,
                             const QString& bodyPath)
{
    QVariantMap fonts = templateMap.value(QStringLiteral("fonts")).toMap();
    if (!displayPath.isEmpty() && QFileInfo::exists(displayPath)) {
        fonts.insert(QStringLiteral("display"), QUrl::fromLocalFile(displayPath).toString());
    }
    if (!bodyPath.isEmpty() && QFileInfo::exists(bodyPath)) {
        fonts.insert(QStringLiteral("body"), QUrl::fromLocalFile(bodyPath).toString());
    }
    templateMap.insert(QStringLiteral("fonts"), fonts);
}

}  // namespace miacode::video_export
