#include "common/MuriTypes.h"

#include "core/chart/parser/SimaiNativeParser.h"
#include "timeline/TimelineData.h"

namespace {

struct MuriDetailTemplateRow {
    MuriDetailKind kind = MuriDetailKind::None;
    const char* muri[3] = {"", "", ""};
    const char* warning[3] = {"", "", ""};
};

constexpr int kEnglishLocaleIndex = 0;
constexpr int kChineseLocaleIndex = 1;
constexpr int kJapaneseLocaleIndex = 2;

int localeTemplateIndex(SimaiNativeValidationLocale locale)
{
    switch (locale) {
    case SimaiNativeValidationLocale::Chinese:
        return kChineseLocaleIndex;
    case SimaiNativeValidationLocale::Japanese:
        return kJapaneseLocaleIndex;
    case SimaiNativeValidationLocale::English:
    default:
        return kEnglishLocaleIndex;
    }
}

QString protectedPrefixText(SimaiNativeValidationLocale locale)
{
    static const char* kProtectedPrefix[3] = {"protected", "保护", "保護"};
    return QString::fromUtf8(kProtectedPrefix[localeTemplateIndex(locale)]);
}

QString localizeMuriEntityText(QString text, SimaiNativeValidationLocale locale)
{
    text = text.trimmed();
    if (text.isEmpty() || locale == SimaiNativeValidationLocale::English) {
        return text;
    }

    static const QString kProtectedPrefix = QStringLiteral("protected ");
    if (text.startsWith(kProtectedPrefix, Qt::CaseInsensitive)) {
        return QStringLiteral("%1 %2")
            .arg(protectedPrefixText(locale), text.mid(kProtectedPrefix.size()).trimmed());
    }
    return text;
}

QString localizeMuriEntityList(QString text, SimaiNativeValidationLocale locale)
{
    text = text.trimmed();
    if (text.isEmpty() || locale == SimaiNativeValidationLocale::English) {
        return text;
    }

    QStringList parts = text.split(QStringLiteral(", "), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return localizeMuriEntityText(text, locale);
    }
    for (QString& part : parts) {
        part = localizeMuriEntityText(part, locale);
    }
    return parts.join(QStringLiteral(", "));
}

const MuriDetailTemplateRow* detailTemplateRow(MuriDetailKind kind)
{
    static const MuriDetailTemplateRow kRows[] = {
        {
            MuriDetailKind::SlideHeadStartEarlyJudgeTap,
            {
                "%1 start will early-judge a following tap, gap %2.",
                "%1 启动，会提前判定后续 tap，间隔 %2。",
                "%1 が始動し、後続の tap を早入力判定します（間隔 %2）。",
            },
            {
                "%1 start may early-judge a following tap, gap %2.",
                "%1 启动，可能会提前判定后续 tap，间隔 %2。",
                "%1 が始動し、後続の tap を早入力判定する可能性があります（間隔 %2）。",
            },
        },
        {
            MuriDetailKind::SlideHeadJumpStartEarlyJudgeTap,
            {
                "%1 jump-start will early-judge a following tap, gap %2.",
                "%1 偷跑，会提前判定后续 tap，间隔 %2。",
                "%1 が先行始動し、後続の tap を早入力判定します（間隔 %2）。",
            },
            {
                "%1 jump-start may early-judge a following tap, gap %2.",
                "%1 偷跑，可能会提前判定后续 tap，间隔 %2。",
                "%1 が先行始動し、後続の tap を早入力判定する可能性があります（間隔 %2）。",
            },
        },
        {
            MuriDetailKind::SlideHeadStartEarlyJudge,
            {
                "%1 start will early-judge %2, gap %3.",
                "%1 启动，会提前判定 %2，间隔 %3。",
                "%1 が始動し、%2 を早入力判定します（間隔 %3）。",
            },
            {
                "%1 start may early-judge %2, gap %3.",
                "%1 启动，可能会提前判定 %2，间隔 %3。",
                "%1 が始動し、%2 を早入力判定する可能性があります（間隔 %3）。",
            },
        },
        {
            MuriDetailKind::SlideHeadJumpStartEarlyJudge,
            {
                "%1 jump-start will early-judge %2, gap %3.",
                "%1 偷跑，会提前判定 %2，间隔 %3。",
                "%1 が先行始動し、%2 を早入力判定します（間隔 %3）。",
            },
            {
                "%1 jump-start may early-judge %2, gap %3.",
                "%1 偷跑，可能会提前判定 %2，间隔 %3。",
                "%1 が先行始動し、%2 を早入力判定する可能性があります（間隔 %3）。",
            },
        },
        {
            MuriDetailKind::TapOnSlideCollide,
            {
                "%1 trajectory will collide with %2, gap %3.",
                "%1 运行轨迹会撞到 %2，间隔 %3。",
                "%1 の軌道が %2 と衝突します（間隔 %3）。",
            },
            {
                "%1 trajectory may collide with %2, gap %3.",
                "%1 运行轨迹可能会撞到 %2，间隔 %3。",
                "%1 の軌道が %2 と衝突する可能性があります（間隔 %3）。",
            },
        },
        {
            MuriDetailKind::EarlyJudgedBy,
            {
                "%1 was early-judged by %2, gap %3.",
                "%1 被 %2 提前判定，间隔 %3。",
                "%1 は %2 によって早入力判定されました（間隔 %3）。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::ResolvedOutsideWindow,
            {
                "%1 resolved outside its critical window, gap %2.",
                "%1 提前完成，间隔 %2。",
                "%1 は判定臨界ウィンドウ外で解決しました（間隔 %2）。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::MultiTouchFormedBy,
            {
                "Multi-touch formed by %1.",
                "%1 构成多押。",
                "%1 が多点押しを構成します。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::FormedOverlapSamePosition,
            {
                "%1 and same-position %2 formed overlap.",
                "%1 与相同位置的 %2 构成叠键。",
                "%1 と同位置の %2 が重なりを構成します。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::FormedOverlapAtSamePosition,
            {
                "%1 formed overlap at the same position.",
                "%1 在相同位置构成叠键。",
                "%1 が同じ位置で重なりを構成します。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::SlideHeadTriggerEarlyJudged,
            {
                "Slide-head trigger from %1 early-judged this note.",
                "来自 %1 的滑键头触发提前判定了此物件。",
                "%1 からのスライド頭トリガーがこのノーツを早入力判定しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::PadTriggerEarlyJudged,
            {
                "Pad trigger from %1 early-judged this note.",
                "来自 %1 的按下触发提前判定了此物件。",
                "%1 からのパッド入力がこのノーツを早入力判定しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::SimpleNoteMissedWindowOverlap,
            {
                "Runtime simple-note judge for %1 missed the critical window and resolved as overlap.",
                "运行时普通物件判定 %1 错过了判定临界窗，并最终判为叠键。",
                "%1 の通常ノーツ実行時判定が臨界ウィンドウを外れ、重なりとして解決されました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::SlideRuntimeOutsideWindow,
            {
                "Slide runtime judge for %1 resolved outside its critical window.",
                "Slide 运行时判定 %1 落在判定临界窗之外。",
                "Slide の実行時判定 %1 は判定臨界ウィンドウ外で解決しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::WifiRuntimeOutsideWindow,
            {
                "Wifi runtime judge for %1 resolved outside its critical window.",
                "Wifi 运行时判定 %1 落在判定临界窗之外。",
                "Wifi の実行時判定 %1 は判定臨界ウィンドウ外で解決しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::SlideClearedEarly,
            {
                "Slide %1 was cleared earlier than its normal judge timing.",
                "Slide %1 的完成时间早于正常判定时机。",
                "Slide %1 は通常の判定タイミングより早く完了しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::WifiClearedEarly,
            {
                "Wifi %1 was cleared earlier than its normal judge timing.",
                "Wifi %1 的完成时间早于正常判定时机。",
                "Wifi %1 は通常の判定タイミングより早く完了しました。",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::StaticReference,
            {
                "Static reference from %1, Δ %2",
                "静态参考：%1，Δ %2",
                "静的参照：%1、Δ %2",
            },
            {"", "", ""},
        },
        {
            MuriDetailKind::RuntimeHandMultiTouch,
            {
                "Runtime hand actions formed %1-hand multi-touch: %2",
                "运行时手部动作形成了 %1 手多押：%2",
                "実行時の手動作が %1 手の多点押しを構成しました：%2",
            },
            {"", "", ""},
        },
    };

    for (const MuriDetailTemplateRow& row : kRows) {
        if (row.kind == kind) {
            return &row;
        }
    }
    return nullptr;
}

QString noGapResolvedOutsideWindowTemplate(SimaiNativeValidationLocale locale)
{
    static const char* kTemplates[3] = {
        "%1 resolved outside its critical window.",
        "%1 的判定落在临界窗之外。",
        "%1 の判定は臨界ウィンドウ外で解決しました。",
    };
    return QString::fromUtf8(kTemplates[localeTemplateIndex(locale)]);
}

QString earlyJudgedByPerfectWindowTemplate(SimaiNativeValidationLocale locale)
{
    static const char* kTemplates[3] = {
        "%1 was early-judged by %2 %3 before standard timing, outside the Perfect tolerance (%4 for this trace).",
        "%1 被 %2 提前判定，比标准判定早 %3；已超出 Perfect 容差（本条 %4）。",
        "%1 は %2 によって標準判定より %3 早く判定され、Perfect 許容幅（この軌道は %4）を超えています。",
    };
    return QString::fromUtf8(kTemplates[localeTemplateIndex(locale)]);
}

QString resolvedOutsidePerfectWindowTemplate(SimaiNativeValidationLocale locale)
{
    static const char* kTemplates[3] = {
        "%1 resolved %2 before standard timing, outside the Perfect tolerance (%3 for this trace).",
        "%1 的实际判定比标准判定早 %2，已超出 Perfect 容差（本条 %3）。",
        "%1 は標準判定より %2 早く判定され、Perfect 許容幅（この軌道は %3）を超えています。",
    };
    return QString::fromUtf8(kTemplates[localeTemplateIndex(locale)]);
}

QString staticReferenceNoDeltaTemplate(SimaiNativeValidationLocale locale)
{
    static const char* kTemplates[3] = {
        "Static reference from %1",
        "静态参考：%1",
        "静的参照：%1",
    };
    return QString::fromUtf8(kTemplates[localeTemplateIndex(locale)]);
}

QString selectedDetailTemplate(
    const MuriDetailTemplateRow& row,
    MuriAlertLevel alert,
    SimaiNativeValidationLocale locale)
{
    const int index = localeTemplateIndex(locale);
    const char* text = alert == MuriAlertLevel::Warning && row.warning[index][0] != '\0'
        ? row.warning[index]
        : row.muri[index];
    if (text[0] == '\0' && index != kEnglishLocaleIndex) {
        text = alert == MuriAlertLevel::Warning && row.warning[kEnglishLocaleIndex][0] != '\0'
            ? row.warning[kEnglishLocaleIndex]
            : row.muri[kEnglishLocaleIndex];
    }
    return QString::fromUtf8(text);
}

QString localizedLeft(const MuriDetailArgs& args, SimaiNativeValidationLocale locale)
{
    return localizeMuriEntityText(args.left, locale);
}

QString localizedRight(const MuriDetailArgs& args, SimaiNativeValidationLocale locale)
{
    return localizeMuriEntityText(args.right, locale);
}

QString localizedActions(const MuriDetailArgs& args, SimaiNativeValidationLocale locale)
{
    return localizeMuriEntityList(args.actions, locale);
}

}  // namespace

QString makeMarkerAnalysisKey(const TimelineNoteMarker& marker)
{
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                      .arg(marker.type)
                      .arg(marker.second, 0, 'f', 6)
                      .arg(marker.lane)
                      .arg(marker.endLane)
                      .arg(marker.sourceLine)
                      .arg(marker.sourceCol)
                      .arg(marker.slideTrackKey);
    if (!marker.slideSegmentKeys.isEmpty()) {
        key += QStringLiteral("|%1").arg(marker.slideSegmentKeys.join(QLatin1Char('/')));
    }
    return key;
}

QString muriKindDisplayName(MuriKind kind, bool chineseUi)
{
    switch (kind) {
    case MuriKind::SlideTooFast:
        return chineseUi ? QStringLiteral("内无") : QStringLiteral("Inner");
    case MuriKind::SlideHeadTap:
        return chineseUi ? QStringLiteral("外无") : QStringLiteral("Outer");
    case MuriKind::TapOnSlide:
        return chineseUi ? QStringLiteral("撞尾") : QStringLiteral("Tail");
    case MuriKind::Overlap:
        return chineseUi ? QStringLiteral("叠键") : QStringLiteral("Overlap");
    case MuriKind::MultiTouch:
        return chineseUi ? QStringLiteral("多押") : QStringLiteral("Multi-touch");
    }
    return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
}

QString muriDetailKindKey(MuriDetailKind kind)
{
    switch (kind) {
    case MuriDetailKind::None:
        return QStringLiteral("none");
    case MuriDetailKind::SlideHeadStartEarlyJudgeTap:
        return QStringLiteral("slide_head_start_early_judge_tap");
    case MuriDetailKind::SlideHeadJumpStartEarlyJudgeTap:
        return QStringLiteral("slide_head_jump_start_early_judge_tap");
    case MuriDetailKind::SlideHeadStartEarlyJudge:
        return QStringLiteral("slide_head_start_early_judge");
    case MuriDetailKind::SlideHeadJumpStartEarlyJudge:
        return QStringLiteral("slide_head_jump_start_early_judge");
    case MuriDetailKind::TapOnSlideCollide:
        return QStringLiteral("tap_on_slide_collide");
    case MuriDetailKind::EarlyJudgedBy:
        return QStringLiteral("early_judged_by");
    case MuriDetailKind::ResolvedOutsideWindow:
        return QStringLiteral("resolved_outside_window");
    case MuriDetailKind::MultiTouchFormedBy:
        return QStringLiteral("multi_touch_formed_by");
    case MuriDetailKind::FormedOverlapSamePosition:
        return QStringLiteral("formed_overlap_same_position");
    case MuriDetailKind::FormedOverlapAtSamePosition:
        return QStringLiteral("formed_overlap_at_same_position");
    case MuriDetailKind::SlideHeadTriggerEarlyJudged:
        return QStringLiteral("slide_head_trigger_early_judged");
    case MuriDetailKind::PadTriggerEarlyJudged:
        return QStringLiteral("pad_trigger_early_judged");
    case MuriDetailKind::SimpleNoteMissedWindowOverlap:
        return QStringLiteral("simple_note_missed_window_overlap");
    case MuriDetailKind::SlideRuntimeOutsideWindow:
        return QStringLiteral("slide_runtime_outside_window");
    case MuriDetailKind::WifiRuntimeOutsideWindow:
        return QStringLiteral("wifi_runtime_outside_window");
    case MuriDetailKind::SlideClearedEarly:
        return QStringLiteral("slide_cleared_early");
    case MuriDetailKind::WifiClearedEarly:
        return QStringLiteral("wifi_cleared_early");
    case MuriDetailKind::StaticReference:
        return QStringLiteral("static_reference");
    case MuriDetailKind::RuntimeHandMultiTouch:
        return QStringLiteral("runtime_hand_multi_touch");
    }
    return QStringLiteral("none");
}

QString renderMuriDetail(
    MuriDetailKind kind,
    const MuriDetailArgs& args,
    SimaiNativeValidationLocale locale)
{
    if (kind == MuriDetailKind::None) {
        return QString();
    }

    if (kind == MuriDetailKind::ResolvedOutsideWindow && args.gapText.trimmed().isEmpty()) {
        return noGapResolvedOutsideWindowTemplate(locale).arg(localizedLeft(args, locale));
    }
    if (kind == MuriDetailKind::ResolvedOutsideWindow && !args.perfectWindowText.trimmed().isEmpty()) {
        return resolvedOutsidePerfectWindowTemplate(locale)
            .arg(localizedLeft(args, locale), args.gapText, args.perfectWindowText);
    }
    if (kind == MuriDetailKind::EarlyJudgedBy && !args.perfectWindowText.trimmed().isEmpty()) {
        return earlyJudgedByPerfectWindowTemplate(locale)
            .arg(localizedLeft(args, locale), localizedRight(args, locale), args.gapText, args.perfectWindowText);
    }
    if (kind == MuriDetailKind::StaticReference && args.deltaText.trimmed().isEmpty()) {
        return staticReferenceNoDeltaTemplate(locale).arg(localizedLeft(args, locale));
    }

    const MuriDetailTemplateRow* row = detailTemplateRow(kind);
    if (row == nullptr) {
        return QString();
    }

    const QString text = selectedDetailTemplate(*row, args.alert, locale);
    switch (kind) {
    case MuriDetailKind::SlideHeadStartEarlyJudgeTap:
    case MuriDetailKind::SlideHeadJumpStartEarlyJudgeTap:
    case MuriDetailKind::ResolvedOutsideWindow:
        return text.arg(localizedLeft(args, locale), args.gapText);
    case MuriDetailKind::SlideHeadStartEarlyJudge:
    case MuriDetailKind::SlideHeadJumpStartEarlyJudge:
    case MuriDetailKind::TapOnSlideCollide:
    case MuriDetailKind::EarlyJudgedBy:
        return text.arg(localizedLeft(args, locale), localizedRight(args, locale), args.gapText);
    case MuriDetailKind::MultiTouchFormedBy:
    case MuriDetailKind::FormedOverlapAtSamePosition:
    case MuriDetailKind::SlideHeadTriggerEarlyJudged:
    case MuriDetailKind::PadTriggerEarlyJudged:
    case MuriDetailKind::SimpleNoteMissedWindowOverlap:
    case MuriDetailKind::SlideRuntimeOutsideWindow:
    case MuriDetailKind::WifiRuntimeOutsideWindow:
    case MuriDetailKind::SlideClearedEarly:
    case MuriDetailKind::WifiClearedEarly: {
        const QString actions = localizedActions(args, locale);
        return text.arg(actions.isEmpty() ? localizedLeft(args, locale) : actions);
    }
    case MuriDetailKind::FormedOverlapSamePosition:
        return text.arg(localizedLeft(args, locale), localizedRight(args, locale));
    case MuriDetailKind::StaticReference:
        return text.arg(localizedLeft(args, locale), args.deltaText);
    case MuriDetailKind::RuntimeHandMultiTouch:
        return text.arg(args.handCount, localizedActions(args, locale));
    case MuriDetailKind::None:
        return QString();
    }
    return QString();
}

QString renderMuriDiagnosticDetail(
    const MuriDiagnostic& diagnostic,
    SimaiNativeValidationLocale locale)
{
    const QString rendered = renderMuriDetail(diagnostic.detailKind, diagnostic.detailArgs, locale);
    return rendered.isEmpty() ? diagnostic.detail : rendered;
}

QString muriAlertLevelDisplayName(MuriAlertLevel level, bool chineseUi)
{
    switch (level) {
    case MuriAlertLevel::Muri:
        return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
    case MuriAlertLevel::Warning:
        return chineseUi ? QStringLiteral("警告") : QStringLiteral("Warning");
    }
    return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
}
