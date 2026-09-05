#include "SimaiDocument.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace {

constexpr QLatin1StringView kBookmarksFieldKey{"miacode_bookmarks"};

// Trim every trailing whitespace character (spaces, tabs, CR/LF — i.e. any
// trailing blank lines too) from a field value. A &key= header is now allowed
// to sit behind leading horizontal whitespace, and that indentation is ignored
// rather than soaked into the preceding field's value; this right-trim is the
// matching half of that contract for the value's own tail.
QString trimTrailingWhitespace(QString value)
{
    int end = value.size();
    while (end > 0 && value.at(end - 1).isSpace()) {
        --end;
    }
    value.truncate(end);
    return value;
}

bool parseDifficultySuffix(const QString& key, QString* prefix, int* id)
{
    if (prefix == nullptr || id == nullptr) {
        return false;
    }
    const int underscore = key.lastIndexOf('_');
    if (underscore <= 0 || underscore + 1 >= key.size()) {
        return false;
    }
    bool ok = false;
    const int value = key.mid(underscore + 1).toInt(&ok);
    if (!ok || !SimaiDocument::isDifficultyId(value)) {
        return false;
    }
    *prefix = key.left(underscore);
    *id = value;
    return true;
}

QString serializeField(const QString& key, const QString& value)
{
    return QString("&%1=%2").arg(key, value);
}

// True for keys that fromText() routes into dedicated document slots
// (and that have their own editor widgets). Such keys must never live in
// the free-form extraFields list, or they would serialize as a duplicate
// line alongside the model-owned one.
bool isReservedMetadataKey(const QString& key)
{
    if (key.compare(QLatin1String("clock_count"), Qt::CaseInsensitive) == 0
        || key == QLatin1String("title") || key == QLatin1String("artist")
        || key == QLatin1String("first") || key == QLatin1String("des")
        || key == QLatin1String("video") || key == kBookmarksFieldKey) {
        return true;
    }
    QString prefix;
    int id = 0;
    if (parseDifficultySuffix(key, &prefix, &id)) {
        return prefix == QLatin1String("lv") || prefix == QLatin1String("des")
            || prefix == QLatin1String("inote");
    }
    return false;
}

// Simai metadata fields that strict third-party players parse as a number
// (e.g. MajdataPlay does `double.Parse` on them). A *bare* `&key=` with an
// empty value crashes those parsers, and unlike `&first` these have no
// meaningful 0 default — an empty `&wholebpm`/`&pvstart`/`&pvlen` simply means
// "unset". So on save we drop the empty line entirely rather than emit a
// value the user never set or a `0` that would lie. (`&first` is handled
// separately: it always serializes, empty → `&first=0`; `&clock_count` is
// always materialized to a concrete value upstream.)
bool isDroppableWhenEmptyNumericField(const QString& key)
{
    return key.compare(QLatin1String("wholebpm"), Qt::CaseInsensitive) == 0
        || key.compare(QLatin1String("pvstart"), Qt::CaseInsensitive) == 0
        || key.compare(QLatin1String("pvlen"), Qt::CaseInsensitive) == 0;
}

}  // namespace

SimaiDocument SimaiDocument::createEmpty()
{
    SimaiDocument doc;
    doc.title = QString();
    doc.artist = QString();
    doc.first = QString();
    doc.designer = QString();
    ensureDefaultClockCount(&doc.extraFields);
    return doc;
}

SimaiDocument SimaiDocument::fromText(const QString& text)
{
    // NOTE: start from a plain document, NOT createEmpty(): createEmpty() seeds a
    // default &clock_count=4, and if the parsed text also carries its own
    // &clock_count= the two would stack into a duplicate (and grow on every
    // load/save round-trip). The single ensureDefaultClockCount() below
    // materializes the default exactly once, after the file's own fields land.
    SimaiDocument doc;
    const QVector<SimaiRawField> fields = parseRawFields(text);
    if (fields.isEmpty()) {
        if (!text.trimmed().isEmpty()) {
            SimaiDifficultyData& master = doc.ensureDifficulty(5);
            master.chart = text;
        }
        ensureDefaultClockCount(&doc.extraFields);
        return doc;
    }

    for (const SimaiRawField& field : fields) {
        if (field.key == "title") {
            doc.title = field.value;
            continue;
        }
        if (field.key == "artist") {
            doc.artist = field.value;
            continue;
        }
        if (field.key == "first") {
            doc.first = field.value;
            continue;
        }
        if (field.key == "des") {
            doc.designer = field.value;
            continue;
        }
        if (field.key == "video") {
            // Phase 4 — optional video-background path. Stored verbatim
            // (typically a relative filename next to the chart file);
            // path resolution to absolute filesystem location happens at
            // chart-load time in the calling code.
            doc.videoPath = field.value;
            continue;
        }
        if (field.key == kBookmarksFieldKey) {
            // Obsolete dev-only bookmark payload. It is hidden from the
            // free-form metadata editor and intentionally not persisted.
            continue;
        }

        QString prefix;
        int id = 0;
        if (parseDifficultySuffix(field.key, &prefix, &id)) {
            SimaiDifficultyData& difficultyData = doc.ensureDifficulty(id);
            if (prefix == "lv") {
                difficultyData.level = field.value;
                continue;
            }
            if (prefix == "des") {
                difficultyData.designer = field.value;
                continue;
            }
            if (prefix == "inote") {
                difficultyData.chart = field.value;
                continue;
            }
        }

        doc.extraFields.append(field);
    }
    ensureDefaultClockCount(&doc.extraFields);

    // A `&des_N` with no accompanying `&lv_N` / `&inote_N` is a chart-less
    // designer name, not a difficulty. Move those out of the difficulty map so
    // they never surface as a phantom empty difficulty (sidebar entry / empty
    // lv+inote on save). A genuinely all-empty difficulty (no name either) is
    // left in place so a freshly-added blank difficulty still round-trips.
    const QVector<int> parsedIds = doc.difficultyIds();
    for (int id : parsedIds) {
        const SimaiDifficultyData* difficultyData = doc.difficulty(id);
        if (difficultyData == nullptr) {
            continue;
        }
        if (difficultyData->level.isEmpty() && difficultyData->chart.isEmpty()
            && !difficultyData->designer.isEmpty()) {
            doc.standaloneDesigners_[id] = difficultyData->designer;
            doc.difficulties_.remove(id);
        }
    }

    return doc;
}

QVector<SimaiRawField> SimaiDocument::parseRawFields(const QString& text, bool prefixDummyIfNeeded)
{
    QVector<SimaiRawField> fields;
    QString source = text;
    if (source.trimmed().isEmpty()) {
        return fields;
    }
    if (prefixDummyIfNeeded) {
        const QRegularExpression startsWithField(R"(\A\s*&[^\r\n=]+=)");
        if (!startsWithField.match(source).hasMatch()) {
            source.prepend("&dummy=");
        }
    }

    // `&key=` may now sit behind leading horizontal whitespace (spaces/tabs and
    // other non-newline blanks) instead of strictly at the line start. The
    // indentation is matched but not captured, so it lands in neither the key
    // nor the previous field's value. `[^\S\r\n]` = whitespace minus CR/LF, so
    // the `^` line anchor still only fires at true line starts.
    const QRegularExpression headerRe(R"((?m)^[^\S\r\n]*&([^\r\n=]+)=)");
    QVector<QRegularExpressionMatch> matches;
    QRegularExpressionMatchIterator it = headerRe.globalMatch(source);
    while (it.hasNext()) {
        matches.append(it.next());
    }
    if (matches.isEmpty()) {
        return fields;
    }

    fields.reserve(matches.size());
    for (int i = 0; i < matches.size(); ++i) {
        const QRegularExpressionMatch& match = matches.at(i);
        const int valueStart = match.capturedEnd(0);
        const int valueEnd = (i + 1 < matches.size()) ? matches.at(i + 1).capturedStart(0) : source.size();
        SimaiRawField field;
        field.key = match.captured(1);
        field.value = trimTrailingWhitespace(source.mid(valueStart, valueEnd - valueStart));
        fields.append(field);
    }

    return fields;
}

QVector<SimaiPropertyIssue> SimaiDocument::invalidPropertyLineNumbers(const QString& text)
{
    QVector<SimaiPropertyIssue> issues;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QString line = lines.at(lineIndex);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const int firstNonSpace = line.indexOf(QRegularExpression(QStringLiteral("\\S")));
        if (firstNonSpace < 0) {
            continue;
        }
        if (line.at(firstNonSpace) != QLatin1Char('&')) {
            issues.append({lineIndex + 1, firstNonSpace + 1,
                           qMax(firstNonSpace + 1, static_cast<int>(line.size())),
                           QStringLiteral("invalid_property")});
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='), firstNonSpace + 1);
        const QString key = equals >= 0
            ? line.mid(firstNonSpace + 1, equals - firstNonSpace - 1).trimmed()
            : QString();
        if (equals <= firstNonSpace + 1 || key.isEmpty()) {
            issues.append({lineIndex + 1, firstNonSpace + 1,
                           qMax(firstNonSpace + 1, static_cast<int>(line.size())),
                           QStringLiteral("invalid_property")});
        }
    }
    return issues;
}

QVector<SimaiRawField> SimaiDocument::parseUnmanagedFields(const QString& text, bool prefixDummyIfNeeded)
{
    const QVector<SimaiRawField> all = parseRawFields(text, prefixDummyIfNeeded);
    QVector<SimaiRawField> unmanaged;
    unmanaged.reserve(all.size());
    for (const SimaiRawField& field : all) {
        if (isReservedMetadataKey(field.key)) {
            continue;
        }
        unmanaged.append(field);
    }
    return unmanaged;
}

QString SimaiDocument::serializeRawFields(const QVector<SimaiRawField>& fields)
{
    QStringList blocks;
    blocks.reserve(fields.size());
    for (const SimaiRawField& field : fields) {
        if (field.key.isEmpty()) {
            continue;
        }
        blocks.append(serializeField(field.key, field.value));
    }
    return blocks.join('\n');
}

bool SimaiDocument::ensureDefaultClockCount(QVector<SimaiRawField>* fields)
{
    if (fields == nullptr) {
        return false;
    }
    // Keep the first clock_count and drop any strays. This both materializes the
    // default (when none is present) and self-heals documents that already
    // accumulated duplicate &clock_count= lines from earlier round-trips.
    bool found = false;
    bool mutated = false;
    for (int i = 0; i < fields->size();) {
        if (fields->at(i).key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) == 0) {
            if (found) {
                fields->removeAt(i);
                mutated = true;
                continue;
            }
            found = true;
        }
        ++i;
    }
    if (!found) {
        fields->append(SimaiRawField{QStringLiteral("clock_count"), QStringLiteral("4")});
        mutated = true;
    }
    return mutated;
}

bool SimaiDocument::isDifficultyId(int id)
{
    return id >= 1 && id <= 7;
}

QString SimaiDocument::difficultyName(int id)
{
    switch (id) {
    case 1:
        return "Easy";
    case 2:
        return "Basic";
    case 3:
        return "Advanced";
    case 4:
        return "Expert";
    case 5:
        return "Master";
    case 6:
        return "Re:Master";
    case 7:
        return "Utage";
    default:
        return QString("Difficulty %1").arg(id);
    }
}

QString SimaiDocument::difficultyShortName(int id)
{
    switch (id) {
    case 1:
        return "ESY";
    case 2:
        return "BAS";
    case 3:
        return "ADV";
    case 4:
        return "EXP";
    case 5:
        return "MAS";
    case 6:
        return "REM";
    case 7:
        return "UTG";
    default:
        return QString("Difficulty %1").arg(id);
    }
}

QString SimaiDocument::toText() const
{
    QStringList blocks;
    blocks.reserve(3 + extraFields.size() + difficulties_.size() * 3);
    blocks.append(serializeField("title", title));
    blocks.append(serializeField("artist", artist));
    // `&first` must always carry a value strict third-party players can parse
    // as a number — a bare `&first=` crashes MajdataPlay's `double.Parse`. An
    // unset offset means "no offset", whose canonical, lossless encoding is 0.
    blocks.append(serializeField("first", first.trimmed().isEmpty() ? QStringLiteral("0") : first));
    if (!designer.isEmpty()) {
        blocks.append(serializeField("des", designer));
    }
    if (!videoPath.isEmpty()) {
        // Phase 4 — round-trip the video-background path so saving a
        // chart preserves the &video= field. Mirrors the placement of
        // the parser's `video` handler before the difficulty fields.
        blocks.append(serializeField("video", videoPath));
    }

    QVector<SimaiRawField> serializedExtraFields = extraFields;
    ensureDefaultClockCount(&serializedExtraFields);
    for (const SimaiRawField& field : serializedExtraFields) {
        if (field.key.isEmpty()) {
            continue;
        }
        // Strip empty numeric metadata (&wholebpm/&pvstart/&pvlen) on save:
        // a bare `&wholebpm=` etc. has no meaningful 0 default and crashes
        // strict third-party parsers, so drop the line rather than write it.
        if (field.value.trimmed().isEmpty() && isDroppableWhenEmptyNumericField(field.key)) {
            continue;
        }
        blocks.append(serializeField(field.key, field.value));
    }

    // Emit real difficulties (full lv/des/inote triple) and chart-less
    // standalone designers (a bare `&des_N`) interleaved in ascending id order
    // so the output stays diff-friendly regardless of which slots are charted.
    QVector<int> allIds;
    allIds.reserve(difficulties_.size() + standaloneDesigners_.size());
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        allIds.append(it.key());
    }
    for (auto it = standaloneDesigners_.cbegin(); it != standaloneDesigners_.cend(); ++it) {
        allIds.append(it.key());
    }
    std::sort(allIds.begin(), allIds.end());
    allIds.erase(std::unique(allIds.begin(), allIds.end()), allIds.end());
    for (int id : allIds) {
        const QString suffix = QString::number(id);
        auto difficultyIt = difficulties_.constFind(id);
        if (difficultyIt != difficulties_.cend()) {
            const SimaiDifficultyData& difficultyData = difficultyIt.value();
            blocks.append(serializeField("lv_" + suffix, difficultyData.level));
            blocks.append(serializeField("des_" + suffix, difficultyData.designer));
            blocks.append(serializeField("inote_" + suffix, difficultyData.chart));
            continue;
        }
        blocks.append(serializeField("des_" + suffix, standaloneDesigners_.value(id)));
    }

    return blocks.join('\n');
}

QVector<int> SimaiDocument::difficultyIds() const
{
    QVector<int> ids;
    ids.reserve(difficulties_.size());
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        ids.append(it.key());
    }
    return ids;
}

SimaiDifficultyData* SimaiDocument::difficulty(int id)
{
    auto it = difficulties_.find(id);
    if (it == difficulties_.end()) {
        return nullptr;
    }
    return &it.value();
}

const SimaiDifficultyData* SimaiDocument::difficulty(int id) const
{
    auto it = difficulties_.constFind(id);
    if (it == difficulties_.cend()) {
        return nullptr;
    }
    return &it.value();
}

SimaiDifficultyData& SimaiDocument::ensureDifficulty(int id)
{
    SimaiDifficultyData& difficultyData = difficulties_[id];
    difficultyData.id = id;
    return difficultyData;
}

void SimaiDocument::removeDifficulty(int id)
{
    difficulties_.remove(id);
}

QString SimaiDocument::designerForSlot(int id) const
{
    if (const SimaiDifficultyData* difficultyData = difficulty(id); difficultyData != nullptr) {
        return difficultyData->designer;
    }
    return standaloneDesigners_.value(id);
}

void SimaiDocument::setDesignerForSlot(int id, const QString& name)
{
    if (!isDifficultyId(id)) {
        return;
    }
    if (SimaiDifficultyData* difficultyData = difficulty(id); difficultyData != nullptr) {
        // A charted difficulty owns its own designer; never create a
        // standalone entry that would duplicate the `&des_N` line.
        difficultyData->designer = name;
        return;
    }
    if (name.isEmpty()) {
        standaloneDesigners_.remove(id);
        return;
    }
    standaloneDesigners_[id] = name;
}

QVector<int> SimaiDocument::standaloneDesignerIds() const
{
    QVector<int> ids;
    ids.reserve(standaloneDesigners_.size());
    for (auto it = standaloneDesigners_.cbegin(); it != standaloneDesigners_.cend(); ++it) {
        ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QVector<QPair<int, QString>> SimaiDocument::perDifficultyDesigners() const
{
    QVector<int> ids;
    ids.reserve(difficulties_.size() + standaloneDesigners_.size());
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        ids.append(it.key());
    }
    for (auto it = standaloneDesigners_.cbegin(); it != standaloneDesigners_.cend(); ++it) {
        ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    QVector<QPair<int, QString>> result;
    result.reserve(ids.size());
    for (int id : ids) {
        result.append(qMakePair(id, designerForSlot(id)));
    }
    return result;
}

bool SimaiDocument::inferUnifiedDesignerDefault() const
{
    // Policy: never auto-enable. Even when isUnifiedDesignerTriviallySafe()
    // would return true (e.g. a brand-new chart, or a project whose every
    // &des_N already matches &des verbatim), we still default OFF — the
    // user has to opt in via the checkbox. This keeps the feature
    // discoverable rather than letting it appear "already enabled" on
    // load, which previously led to confusion when a user added a new
    // difficulty and saw the seeded designer name appear "by itself".
    return false;
}

bool SimaiDocument::isUnifiedDesignerTriviallySafe() const
{
    // True when every existing &des / &des_N already agrees. Includes the
    // brand-new-project case (no difficulties, empty top &des). Any
    // disagreement, *including* a blank &des_N when top &des has a
    // value, returns false.
    //
    // Examples:
    //   &des="X", no difficulties                       → true
    //   &des="X", &des_5="X", &des_6="X"                → true
    //   &des="",  &des_5="",  &des_6=""                 → true  (new project)
    //   &des="X", &des_5="",  &des_6="X"                → false (5 disagrees)
    //   &des="",  &des_5="X"                            → false (5 disagrees)
    //   &des="X", &des_5="X", &des_6="Y"                → false (6 disagrees)
    //
    // Currently not wired to document loading. Preserved for a future
    // suggestion-style UI that might prompt the user "this project looks
    // unified, enable the option?" after the v1 behavior is understood.
    for (auto it = difficulties_.cbegin(); it != difficulties_.cend(); ++it) {
        if (it.value().designer != designer) {
            return false;
        }
    }
    // Chart-less `&des_N` names must agree too, or the project isn't unified.
    for (auto it = standaloneDesigners_.cbegin(); it != standaloneDesigners_.cend(); ++it) {
        if (it.value() != designer) {
            return false;
        }
    }
    return true;
}
