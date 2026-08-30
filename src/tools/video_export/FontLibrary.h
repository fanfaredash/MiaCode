#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>

class QComboBox;
class QWidget;

namespace miacode::video_export {

// Portable user font library — the `<preferences dir>/fonts` directory that
// holds imported .ttf/.otf files. Shared by the HUD font picker
// (HudFontSettings) and the difficulty-card font selectors (CardFontSettings),
// so a font imported for one surface is immediately offered to the others.

// Absolute path to the font-library directory (not created here).
QString fontLibraryDirPath();

struct FontLibraryEntry {
    QString label;   // "Family (file.ttf)"; the default entry uses defaultLabel.
    QString path;    // absolute path in the library; empty == default/none.
    QString family;  // resolved font family; empty for the default entry.
};

// Result of importing one file into the portable font library.  The QML shell
// owns user interaction, so this data-layer operation deliberately has no
// dialog or message-box dependency.
enum class FontImportFailure {
    None,
    NotFontFile,
    InvalidFont,
    CopyFailed,
};

struct FontImportResult {
    QString path;
    FontImportFailure failure = FontImportFailure::None;
};

// Every .ttf/.otf in the library, family-resolved (unreadable files skipped),
// sorted by filename. When `includeDefault` is set a leading
// {defaultLabel, "", ""} entry is prepended (the "use the bundled default"
// choice).
QVector<FontLibraryEntry> fontLibraryEntries(bool includeDefault = false,
                                             const QString& defaultLabel = QString());

// Register `path` with the application font database and return its first font
// family (empty on failure). Registration is idempotent for the same file.
QString fontFamilyForFile(const QString& path);

// Validate a local .ttf/.otf and copy it into the portable library.  A file
// already in that library is returned unchanged.  Callers present any error
// through their own UI boundary.
FontImportResult importFontFileIntoLibrary(const QString& sourcePath);

// Prompt for a .ttf/.otf, validate it, and copy it into the library. Returns the
// resulting library path (empty on cancel or error; an error shows a warning box
// parented to `parent`).
QString importFontIntoLibrary(QWidget* parent);

// Populate `combo` with the library entries (data = each entry's absolute path),
// selecting the row whose path matches `selectedPath` (else the first row).
// Signals are blocked during the repopulate.
void populateFontCombo(QComboBox* combo,
                       const QString& selectedPath,
                       bool includeDefault = true,
                       const QString& defaultLabel = QString());

enum class FontComboWidthMode {
    StandardForm,
    NarrowInspector,
};

// Keeps the shared font selector visually themed while tuning only its layout
// budget for the host. Long names elide in the closed field; popup entries stay
// complete. Every mode remains horizontally expanding so a layout cannot
// collapse the visible control.
void configureFontComboWidth(QComboBox* combo, FontComboWidthMode mode);

// Overlay the user's difficulty-card font choice onto a parsed banner template's
// `fonts` block (keys `display` / `body`). An absolute path is injected as a
// file:// URL string, which MaimaiBannerCard.qml treats as a literal FontLoader
// source; a bare filename (the untouched default) keeps the qrc `fontsRoot`
// behaviour. A missing file is IGNORED (the bundled default stays), so a
// composition carrying a font path that is absent on another machine still
// renders its text instead of going blank.
void applyBannerFontOverride(QVariantMap& templateMap,
                             const QString& displayPath,
                             const QString& bodyPath);

}  // namespace miacode::video_export
