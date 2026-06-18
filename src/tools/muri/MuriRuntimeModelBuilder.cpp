#include "tools/muri/MuriRuntimeModelBuilder.h"

#include <algorithm>

#include <QSet>

#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"  // makeMarkerAnalysisKey
#include "tools/muri/MuriAnalyzerInternal.h"

namespace miacode::muri::detail {

QHash<QString, MarkerSourceRef> buildMarkerSourceRefs(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, MarkerSourceRef> refs;
    refs.reserve(noteMarkers.size() * 2);

    for (int index = 0; index < noteMarkers.size(); ++index) {
        const TimelineNoteMarker& marker = noteMarkers.at(index);
        MarkerSourceRef ref;
        ref.order = marker.parseOrder >= 0 ? marker.parseOrder : index;
        ref.second = marker.second;
        ref.line = marker.sourceLine;
        ref.col = marker.sourceCol;
        ref.type = marker.type;
        refs.insert(makeMarkerAnalysisKey(marker), ref);

        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }

        MarkerSourceRef headStarRef = ref;
        headStarRef.col = slideHeadStarSourceCol(marker);
        headStarRef.type = QStringLiteral("tap");
        refs.insert(slideHeadStarMarkerKey(marker), headStarRef);
    }

    return refs;
}

QVector<JudgeableSimpleNote> buildJudgeableSimpleNotes(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    QHash<QString, int>* noteIndexByMarkerKey)
{
    QVector<JudgeableSimpleNote> notes;
    notes.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        // Mine notes (simai `m`) are dodged by autoplay — no judge window,
        // no judge sprite/perfect-text/firework.
        if (marker.isMine || marker.trackMine) {
            continue;
        }
        QString pad;
        double criticalSeconds = 0.0;
        double availableSeconds = 0.0;
        if (!buildSimpleNoteJudgeWindow(marker, &pad, &criticalSeconds, &availableSeconds)) {
            continue;
        }

        const QString markerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(markerKey);

        JudgeableSimpleNote note;
        note.marker = &marker;
        note.markerKey = markerKey;
        note.type = marker.type;
        note.pad = pad;
        note.order = ref.order;
        note.parseOrder = marker.parseOrder;
        note.eachGroupId = marker.eachGroupId;
        note.line = marker.sourceLine;
        note.col = marker.sourceCol;
        note.momentSecond = marker.second;
        note.pressEndSecond = notePressEndSecond(marker);
        note.criticalSeconds = criticalSeconds;
        note.availableSeconds = availableSeconds;
        note.expiryTick = judgeTickForNoteExpiry(marker.second, availableSeconds);
        note.slideHead = marker.slideHead;
        note.onSlide = marker.onSlide;
        note.tailOnSlideHead = marker.tailOnSlideHead;
        note.hasProtection = marker.isEx;
        const int noteIndex = notes.size();
        notes.append(note);
        if (noteIndexByMarkerKey != nullptr) {
            noteIndexByMarkerKey->insert(markerKey, noteIndex);
        }
    }

    QSet<QString> emittedHeadStarKeys;
    for (int markerIndex = 0; markerIndex < noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }

        const QString emitKey = headStarJudgeEmitKey(marker);
        if (emittedHeadStarKeys.contains(emitKey)) {
            continue;
        }
        emittedHeadStarKeys.insert(emitKey);

        const QString ownerMarkerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(ownerMarkerKey);
        const QString markerKey = slideHeadStarMarkerKey(marker);

        JudgeableSimpleNote note;
        note.marker = &marker;
        note.markerKey = markerKey;
        note.type = QStringLiteral("tap");
        note.pad = slideHeadPad(marker.lane);
        note.order = ref.order;
        note.parseOrder = marker.parseOrder;
        note.eachGroupId = marker.eachGroupId;
        note.line = marker.sourceLine;
        note.col = slideHeadStarSourceCol(marker);
        note.momentSecond = marker.second;
        note.pressEndSecond = marker.second + miacode::muri::kReleaseDelaySeconds;
        note.criticalSeconds = miacode::muri::kTapCriticalSeconds;
        note.availableSeconds = miacode::muri::kTapAvailableSeconds;
        note.expiryTick = judgeTickForNoteExpiry(marker.second, note.availableSeconds);
        note.slideHead = marker.slideHead;
        // Keep the star identity for labeling and source attribution only.
        note.headStarTapLike = true;
        note.hasProtection = marker.headEx;
        const int noteIndex = notes.size();
        notes.append(note);
        if (noteIndexByMarkerKey != nullptr) {
            noteIndexByMarkerKey->insert(markerKey, noteIndex);
        }
    }

    return notes;
}

QMap<int, QVector<int>> buildNoteExpiryBuckets(const QVector<JudgeableSimpleNote>& notes)
{
    QMap<int, QVector<int>> notesByExpiryTick;
    for (int index = 0; index < notes.size(); ++index) {
        notesByExpiryTick[notes.at(index).expiryTick].append(index);
    }
    return notesByExpiryTick;
}

QVector<RuntimeTouchGroup> buildRuntimeTouchGroups(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, int>& noteIndexByMarkerKey,
    QHash<int, int>* touchGroupByChildNoteIndex)
{
    QMap<int, QVector<int>> touchMarkerIndicesByEachGroup;
    for (int markerIndex = 0; markerIndex < noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
        if (marker.type == QLatin1String("touch") && marker.eachGroupId >= 0 && !marker.touchPad.isEmpty()) {
            touchMarkerIndicesByEachGroup[marker.eachGroupId].append(markerIndex);
        }
    }

    QVector<RuntimeTouchGroup> groups;
    for (auto it = touchMarkerIndicesByEachGroup.begin(); it != touchMarkerIndicesByEachGroup.end(); ++it) {
        QVector<int> markerIndices = it.value();
        std::sort(markerIndices.begin(), markerIndices.end(), [&noteMarkers](int a, int b) {
            const TimelineNoteMarker& left = noteMarkers.at(a);
            const TimelineNoteMarker& right = noteMarkers.at(b);
            const int leftOrder = left.parseOrder >= 0 ? left.parseOrder : a;
            const int rightOrder = right.parseOrder >= 0 ? right.parseOrder : b;
            return leftOrder < rightOrder;
        });

        if (markerIndices.size() <= 1) {
            continue;
        }

        QVector<int> parents(markerIndices.size());
        for (int i = 0; i < parents.size(); ++i) {
            parents[i] = i;
        }

        const auto findRoot = [&parents](int index) {
            int root = index;
            while (parents[root] != root) {
                root = parents[root];
            }
            int cursor = index;
            while (parents[cursor] != root) {
                const int parent = parents[cursor];
                parents[cursor] = root;
                cursor = parent;
            }
            return root;
        };

        for (int left = 0; left < markerIndices.size(); ++left) {
            const TimelineNoteMarker& leftMarker = noteMarkers.at(markerIndices.at(left));
            for (int right = left + 1; right < markerIndices.size(); ++right) {
                const TimelineNoteMarker& rightMarker = noteMarkers.at(markerIndices.at(right));
                if (!touchPadsAreAdjacent(leftMarker.touchPad, rightMarker.touchPad)) {
                    continue;
                }

                const int rootLeft = findRoot(left);
                const int rootRight = findRoot(right);
                if (rootLeft == rootRight) {
                    continue;
                }

                for (int cursor = 0; cursor < parents.size(); ++cursor) {
                    if (parents[cursor] == rootRight) {
                        parents[cursor] = rootLeft;
                    }
                }
            }
        }

        QHash<int, QVector<int>> componentLocals;
        QVector<int> componentOrder;
        for (int localIndex = 0; localIndex < markerIndices.size(); ++localIndex) {
            const int root = findRoot(localIndex);
            if (!componentLocals.contains(root)) {
                componentOrder.append(root);
            }
            componentLocals[root].append(localIndex);
        }

        for (int root : componentOrder) {
            const QVector<int> locals = componentLocals.value(root);
            if (locals.size() <= 1) {
                continue;
            }

            RuntimeTouchGroup group;
            group.eachGroupId = it.key();
            group.allOnSlide = true;
            for (int localIndex : locals) {
                const int markerIndex = markerIndices.at(localIndex);
                const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
                const QString markerKey = makeMarkerAnalysisKey(marker);
                const int noteIndex = noteIndexByMarkerKey.value(markerKey, -1);
                if (noteIndex < 0) {
                    continue;
                }

                if (group.sourceMarkerKey.isEmpty()) {
                    group.sourceMarkerKey = markerKey;
                    group.line = marker.sourceLine;
                    group.col = marker.sourceCol;
                    group.momentSecond = marker.second;
                }
                if (group.firstParseOrder < 0 || marker.parseOrder < group.firstParseOrder) {
                    group.firstParseOrder = marker.parseOrder;
                }
                if (!marker.onSlide) {
                    group.allOnSlide = false;
                }

                group.childNoteIndices.append(noteIndex);
            }

            if (group.childNoteIndices.size() <= 1) {
                continue;
            }

            group.threshold = group.childNoteIndices.size() * 0.51;
            const int groupIndex = groups.size();
            groups.append(group);
            if (touchGroupByChildNoteIndex != nullptr) {
                for (int childNoteIndex : group.childNoteIndices) {
                    touchGroupByChildNoteIndex->insert(childNoteIndex, groupIndex);
                }
            }
        }
    }

    return groups;
}

QVector<RuntimeTopLevelNote> buildRuntimeTopLevelNotes(
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex)
{
    struct TouchTopLevelEntry {
        int firstParseOrder = -1;
        int simpleNoteIndex = -1;
        int touchGroupIndex = -1;
    };

    QMap<int, QVector<int>> nonTouchNoteIndicesByEachGroup;
    QMap<int, QVector<TouchTopLevelEntry>> touchEntriesByEachGroup;
    QMap<int, int> firstParseOrderByEachGroup;

    for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const JudgeableSimpleNote& note = notes.at(noteIndex);
        const int eachGroupId = note.eachGroupId;
        if (eachGroupId < 0) {
            continue;
        }
        const int parseOrder = note.parseOrder >= 0 ? note.parseOrder : note.order;
        if (!firstParseOrderByEachGroup.contains(eachGroupId) || parseOrder < firstParseOrderByEachGroup.value(eachGroupId)) {
            firstParseOrderByEachGroup[eachGroupId] = parseOrder;
        }

        if (touchGroupByChildNoteIndex.contains(noteIndex)) {
            continue;
        }

        if (note.type == QLatin1String("touch")) {
            TouchTopLevelEntry entry;
            entry.firstParseOrder = parseOrder;
            entry.simpleNoteIndex = noteIndex;
            touchEntriesByEachGroup[eachGroupId].append(entry);
        } else {
            nonTouchNoteIndicesByEachGroup[eachGroupId].append(noteIndex);
        }
    }

    for (int touchGroupIndex = 0; touchGroupIndex < touchGroups.size(); ++touchGroupIndex) {
        const RuntimeTouchGroup& group = touchGroups.at(touchGroupIndex);
        if (group.eachGroupId < 0) {
            continue;
        }
        if (!firstParseOrderByEachGroup.contains(group.eachGroupId)
            || group.firstParseOrder < firstParseOrderByEachGroup.value(group.eachGroupId)) {
            firstParseOrderByEachGroup[group.eachGroupId] = group.firstParseOrder;
        }
        TouchTopLevelEntry entry;
        entry.firstParseOrder = group.firstParseOrder;
        entry.touchGroupIndex = touchGroupIndex;
        touchEntriesByEachGroup[group.eachGroupId].append(entry);
    }

    QVector<int> eachGroupIds = firstParseOrderByEachGroup.keys().toVector();
    std::sort(eachGroupIds.begin(), eachGroupIds.end(), [&firstParseOrderByEachGroup](int a, int b) {
        return firstParseOrderByEachGroup.value(a) < firstParseOrderByEachGroup.value(b);
    });

    QVector<RuntimeTopLevelNote> topLevelNotes;
    int sequenceOrder = 0;
    for (int eachGroupId : eachGroupIds) {
        QVector<int> nonTouchIndices = nonTouchNoteIndicesByEachGroup.value(eachGroupId);
        std::sort(nonTouchIndices.begin(), nonTouchIndices.end(), [&notes](int a, int b) {
            return notes.at(a).parseOrder < notes.at(b).parseOrder;
        });
        for (int noteIndex : nonTouchIndices) {
            RuntimeTopLevelNote topLevel;
            topLevel.sequenceOrder = sequenceOrder++;
            topLevel.simpleNoteIndex = noteIndex;
            topLevelNotes.append(topLevel);
        }

        QVector<TouchTopLevelEntry> touchEntries = touchEntriesByEachGroup.value(eachGroupId);
        std::sort(touchEntries.begin(), touchEntries.end(), [](const TouchTopLevelEntry& a, const TouchTopLevelEntry& b) {
            return a.firstParseOrder < b.firstParseOrder;
        });
        for (const TouchTopLevelEntry& entry : touchEntries) {
            RuntimeTopLevelNote topLevel;
            topLevel.sequenceOrder = sequenceOrder++;
            topLevel.simpleNoteIndex = entry.simpleNoteIndex;
            topLevel.touchGroupIndex = entry.touchGroupIndex;
            topLevelNotes.append(topLevel);
        }
    }

    QVector<int> ungroupedNoteIndices;
    for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const JudgeableSimpleNote& note = notes.at(noteIndex);
        if (note.eachGroupId >= 0) {
            continue;
        }
        if (touchGroupByChildNoteIndex.contains(noteIndex)) {
            continue;
        }
        ungroupedNoteIndices.append(noteIndex);
    }
    std::sort(ungroupedNoteIndices.begin(), ungroupedNoteIndices.end(), [&notes](int a, int b) {
        return notes.at(a).parseOrder < notes.at(b).parseOrder;
    });
    for (int noteIndex : ungroupedNoteIndices) {
        RuntimeTopLevelNote topLevel;
        topLevel.sequenceOrder = sequenceOrder++;
        topLevel.simpleNoteIndex = noteIndex;
        topLevelNotes.append(topLevel);
    }

    return topLevelNotes;
}

MuriRuntimeModel MuriRuntimeModelBuilder::build(const QVector<TimelineNoteMarker>& noteMarkers)
{
    MuriRuntimeModel model;
    model.markerRefs = buildMarkerSourceRefs(noteMarkers);
    model.notes = buildJudgeableSimpleNotes(noteMarkers, model.markerRefs, &model.noteIndexByMarkerKey);
    model.touchGroups =
        buildRuntimeTouchGroups(noteMarkers, model.noteIndexByMarkerKey, &model.touchGroupByChildNoteIndex);
    return model;
}

}  // namespace miacode::muri::detail
