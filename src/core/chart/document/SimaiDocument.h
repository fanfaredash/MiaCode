#pragma once

#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

struct SimaiRawField {
    QString key;
    QString value;
};

inline bool operator==(const SimaiRawField& lhs, const SimaiRawField& rhs)
{
    return lhs.key == rhs.key && lhs.value == rhs.value;
}

inline bool operator!=(const SimaiRawField& lhs, const SimaiRawField& rhs)
{
    return !(lhs == rhs);
}

struct SimaiDifficultyData {
    int id = 0;
    QString level;
    QString designer;
    QString chart;
};

// One editor bookmark as stored in the managed `&miacode_bookmarks=` field —
// a single-line compact-JSON payload MiaCode owns end to end. Third-party
// players never parse it; parseUnmanagedFields() hides it from the free-form
// "Other &xx Fields" editor so users can't corrupt it by accident.
struct SimaiBookmarkData {
    int difficultyId = 0;        // "d"
    int line = 1;                // "l"
    QString name;                // "n" — the only user-visible text
    double second = -1.0;        // "s" — timeline anchor; < 0 = unknown
    QString source;              // "src" — "manual" / "comment"
    QString commentFingerprint;  // "fp" — re-anchor aid for moved comments
    QString contextBefore;       // "cb"
    QString contextAfter;        // "ca"
    bool nameLocked = false;     // "locked" — user renamed; no auto-naming
};

class SimaiDocument
{
public:
    static SimaiDocument createEmpty();
    static SimaiDocument fromText(const QString& text);

    static QVector<SimaiRawField> parseRawFields(const QString& text, bool prefixDummyIfNeeded = false);
    // Like parseRawFields, but drops keys that already have dedicated model
    // storage and editor UI (title/artist/first/des/video and the
    // per-difficulty lv_/des_/inote_). Used by the free-form "Other &xx
    // Fields" editor so a user can't smuggle e.g. a second &des_5 line that
    // would bypass the model — and, under unified-designer mode, its sync.
    static QVector<SimaiRawField> parseUnmanagedFields(const QString& text, bool prefixDummyIfNeeded = false);
    static QString serializeRawFields(const QVector<SimaiRawField>& fields);
    static bool ensureDefaultClockCount(QVector<SimaiRawField>* fields);

    static bool isDifficultyId(int id);
    static QString difficultyName(int id);
    static QString difficultyShortName(int id);

    QString toText() const;
    QVector<int> difficultyIds() const;

    SimaiDifficultyData* difficulty(int id);
    const SimaiDifficultyData* difficulty(int id) const;
    SimaiDifficultyData& ensureDifficulty(int id);
    void removeDifficulty(int id);

    // Per-difficulty designer (`&des_N`) access that works for *all* slots
    // 1..7, including those with no chart yet. A slot's name is stored on its
    // SimaiDifficultyData when the difficulty actually exists; otherwise it is
    // a "standalone" designer — a `&des_N` recorded without creating a phantom
    // empty difficulty (no `&lv_N` / `&inote_N`, never listed in
    // difficultyIds()). This backs the "manage per-difficulty designers"
    // dialog, which can set a name for a slot the user hasn't charted.
    QString designerForSlot(int id) const;
    // Sets `&des_N` for the slot. When difficulty `id` exists, writes its
    // .designer. Otherwise a non-empty name is stored as a standalone designer
    // (no difficulty is created); an empty name clears any standalone entry.
    void setDesignerForSlot(int id, const QString& name);
    // Sorted ids that currently hold a standalone (chart-less) `&des_N`.
    QVector<int> standaloneDesignerIds() const;
    // (id, designer) for every slot that has a designer — real difficulties
    // and standalone entries alike — sorted by id. Used by the unified-designer
    // survey / broadcast so chart-less names participate too.
    QVector<QPair<int, QString>> perDifficultyDesigners() const;

    // Default for the "all difficulties share the same designer name"
    // project preference when no explicit value is recorded yet.
    // Currently *always false* — auto-enabling has no entirely-safe
    // fallback, so we require the user to opt in. The heuristic that
    // detects "this project is already trivially unified" lives in
    // isUnifiedDesignerTriviallySafe() below and is preserved for a
    // future preferences/onboarding flow that might surface a suggestion.
    bool inferUnifiedDesignerDefault() const;

    // Returns true when every &des_N already matches the top-level &des
    // verbatim (or both are empty). Includes the brand-new-project case
    // (no difficulties + empty &des) and the already-unified case
    // (&des = "X" with every existing &des_N = "X"). Any disagreement —
    // including a blank &des_N when &des has a value, or vice versa —
    // returns false. Kept as a public utility even though
    // inferUnifiedDesignerDefault() doesn't consult it, because future
    // UI affordances (e.g. "this project looks unified, enable it?")
    // will want to ask this question explicitly.
    bool isUnifiedDesignerTriviallySafe() const;

    QString title;
    QString artist;
    QString first;
    QString designer;
    // Phase 4 of the v2-refactor — optional video-background path for
    // chart preview. Stored as the raw `&video=` value (typically a
    // relative path next to the chart file, e.g. `bg.mp4`). Empty
    // means "no video; fall back to image background". Resolution to
    // an absolute filesystem path happens at chart-load time, not
    // here, because SimaiDocument is location-agnostic.
    QString videoPath;
    QVector<SimaiRawField> extraFields;
    // Managed bookmark payload (`&miacode_bookmarks=`). fromText() fills this
    // (never extraFields); toText() emits one compact single-line field after
    // the extra fields and before the difficulty triples — or nothing when
    // empty. Items with an invalid difficulty id or line are dropped on parse.
    QVector<SimaiBookmarkData> bookmarks;
    // True when the parsed text carried a `&miacode_bookmarks=` value that was
    // not valid JSON. The field is ignored (chart loading must never block on
    // bookmark metadata); callers may surface a non-fatal warning.
    bool bookmarksParseError = false;

private:
    QMap<int, SimaiDifficultyData> difficulties_;
    // `&des_N` names for slots that have no chart. Kept disjoint from
    // difficulties_ (a slot is in one map or the other, never both) so a
    // chart-less designer name round-trips without materializing an empty
    // difficulty. fromText() moves designer-only difficulties here; toText()
    // emits a bare `&des_N=` line for each.
    QMap<int, QString> standaloneDesigners_;
};
