#pragma once

#include <QMap>
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

class SimaiDocument
{
public:
    static SimaiDocument createEmpty();
    static SimaiDocument fromText(const QString& text);

    static QVector<SimaiRawField> parseRawFields(const QString& text, bool prefixDummyIfNeeded = false);
    static QString serializeRawFields(const QVector<SimaiRawField>& fields);

    static bool isDifficultyId(int id);
    static QString difficultyName(int id);
    static QString difficultyShortName(int id);

    QString toText() const;
    QVector<int> difficultyIds() const;

    SimaiDifficultyData* difficulty(int id);
    const SimaiDifficultyData* difficulty(int id) const;
    SimaiDifficultyData& ensureDifficulty(int id);
    void removeDifficulty(int id);

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

private:
    QMap<int, SimaiDifficultyData> difficulties_;
};
