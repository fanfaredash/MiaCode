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

    QString toText() const;
    QVector<int> difficultyIds() const;

    SimaiDifficultyData* difficulty(int id);
    const SimaiDifficultyData* difficulty(int id) const;
    SimaiDifficultyData& ensureDifficulty(int id);
    void removeDifficulty(int id);

    QString title;
    QString artist;
    QString first;
    QString designer;
    QVector<SimaiRawField> extraFields;

private:
    QMap<int, SimaiDifficultyData> difficulties_;
};
