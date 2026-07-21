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

// Every .ttf/.otf in the library, family-resolved (unreadable files skipped),
// sorted by filename. When `includeDefault` is set a leading
// {defaultLabel, "", ""} entry is prepended (the "use the bundled default"
// choice).
QVector<FontLibraryEntry> fontLibraryEntries(bool includeDefault = false,
                                             const QString& defaultLabel = QString());

// Register `path` with the application font database and return its first font
// family (empty on failure). Registration is idempotent for the same file.
QString fontFamilyForFile(const QString& path);

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

// Lets a font selector live inside a narrow inspector column. Font family names
// can be arbitrarily long; keep their popup entries intact while allowing the
// closed field to elide rather than widening its parent layout.
void constrainFontComboToAvailableWidth(QComboBox* combo);

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
