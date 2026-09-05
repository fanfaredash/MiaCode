#include "tools/video_export/FontLibrary.h"

#include "UiText.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
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
    // Resolving a family means QFontDatabase::addApplicationFont(path) — a disk
    // read + font parse. The export page rebuilds its font combos on every page
    // entry AND every badge switch (each combo re-enumerates the whole library,
    // several times per dialog), so the same files were re-parsed dozens of
    // times per switch — a dominant slice of the "切换到导出页很慢" cost. The
    // family name is a pure function of the file's content, so cache it keyed by
    // absolute path + (mtime, size); an import always writes a fresh unique path
    // (cache miss), and an edited file changes mtime/size (auto-invalidated).
    // GUI-thread only, so no locking is needed.
    struct CacheEntry {
        qint64 mtimeMs = 0;
        qint64 sizeBytes = -1;
        QString family;
    };
    static QHash<QString, CacheEntry> cache;

    const QFileInfo info(path);
    const QString absPath = info.absoluteFilePath();
    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
    const qint64 sizeBytes = info.size();
    const auto it = cache.constFind(absPath);
    if (it != cache.constEnd() && it->mtimeMs == mtimeMs && it->sizeBytes == sizeBytes) {
        return it->family;
    }

    QString family;
    const int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        family = families.isEmpty() ? QString() : families.first();
    }
    cache.insert(absPath, CacheEntry{mtimeMs, sizeBytes, family});
    return family;
}

QVector<FontLibraryEntry> fontLibraryEntries(bool includeDefault, const QString& defaultLabel)
{
    QDir dir(fontLibraryDirPath());
    const QFileInfoList files = dir.entryInfoList(
        QStringList{QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase
    );

    // The QML export page asks for the same model from several independent
    // combo-box bindings. Keep the directory enumeration as the cheap change
    // detector, but avoid rebuilding and re-registering the same entry list
    // for every getter call. mtime/size invalidates the cache after an import
    // or an edited library file; the localized default label is part of the
    // key because the application can switch languages without restarting in
    // tests and embedded shells.
    QString signature;
    signature.reserve(files.size() * 48);
    for (const QFileInfo& file : files) {
        signature += file.absoluteFilePath();
        signature += QLatin1Char('\0');
        signature += QString::number(file.lastModified().toMSecsSinceEpoch());
        signature += QLatin1Char(':');
        signature += QString::number(file.size());
        signature += QLatin1Char('\n');
    }
    static QString cachedSignature;
    static QString cachedDefaultLabel;
    static bool cachedIncludeDefault = false;
    static QVector<FontLibraryEntry> cachedEntries;
    if (cachedSignature == signature && cachedIncludeDefault == includeDefault
        && cachedDefaultLabel == defaultLabel) {
        return cachedEntries;
    }

    QVector<FontLibraryEntry> entries;
    if (includeDefault) {
        entries.push_back({
            defaultLabel.isEmpty() ? QStringLiteral("Default font") : defaultLabel,
            QString(),
            QString()
        });
    }
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
    cachedSignature = signature;
    cachedIncludeDefault = includeDefault;
    cachedDefaultLabel = defaultLabel;
    cachedEntries = entries;
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

FontImportResult importFontFileIntoLibrary(const QString& sourcePath)
{
    const QFileInfo sourceInfo(sourcePath);
    const QString suffix = sourceInfo.suffix().toLower();
    if (!sourceInfo.isFile() || (suffix != QStringLiteral("ttf") && suffix != QStringLiteral("otf"))) {
        return {{}, FontImportFailure::NotFontFile};
    }
    if (fontFamilyForFile(sourceInfo.absoluteFilePath()).isEmpty()) {
        return {{}, FontImportFailure::InvalidFont};
    }

    const QDir libraryDir(fontLibraryDirPath());
    if (sourceInfo.absoluteDir() == libraryDir) {
        return {sourceInfo.absoluteFilePath(), FontImportFailure::None};
    }

    const QString targetPath = uniqueFontLibraryPath(sourceInfo);
    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath)) {
        return {{}, FontImportFailure::CopyFailed};
    }
    return {targetPath, FontImportFailure::None};
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
