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

    // Heuristic for "all difficulties share the same designer name" default
    // when the project has no explicit preference recorded yet.
    //
    // Returns false when there is evidence that difficulties are deliberately
    // authored under distinct designer names (multiple distinct non-empty
    // per-difficulty designers; or per-difficulty designers exist but the
    // top-level &des field is empty). Otherwise returns true (no per-diff
    // designers, or all per-diff designers match the top-level &des).
    bool inferUnifiedDesignerDefault() const;

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
