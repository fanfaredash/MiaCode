#include "MainWindow.ValidationSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

using miacode::muri::MuriPanelEntry;

constexpr int kIssueLineRole = Qt::UserRole;
constexpr int kIssueColRole = Qt::UserRole + 1;
constexpr int kIssueAuxRole = Qt::UserRole + 2;
constexpr int kIssueTypeKeyRole = Qt::UserRole + 3;
constexpr int kIssueTypeLabelRole = Qt::UserRole + 4;
constexpr int kIssueIgnoredRole = Qt::UserRole + 5;

enum class ValidationSeverityLevel {
    Error,
    Warning,
};

enum class EditorValidationSummaryIconKind {
    Error,
    Warning,
    Muri,
};

struct ValidationMessageParts {
    QString severityPrefix;
    QString body;
    ValidationSeverityLevel severity = ValidationSeverityLevel::Error;
};

QPixmap makeEditorValidationSummaryIcon(const QColor& color, EditorValidationSummaryIconKind kind)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    if (kind == EditorValidationSummaryIconKind::Warning) {
        QPainterPath triangle;
        triangle.moveTo(7.0, 1.2);
        triangle.lineTo(12.8, 12.0);
        triangle.lineTo(1.2, 12.0);
        triangle.closeSubpath();
        painter.drawPath(triangle);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(6.2, 4.0, 1.6, 4.0), 0.8, 0.8);
        painter.drawEllipse(QRectF(6.1, 9.2, 1.8, 1.8));
    } else if (kind == EditorValidationSummaryIconKind::Muri) {
        painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(11);
        painter.setFont(font);
        painter.drawText(QRectF(0.2, 0.6, 13.6, 12.8), Qt::AlignCenter, QStringLiteral("?"));
    } else {
        painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(6.1, 3.4, 1.8, 5.0), 0.9, 0.9);
        painter.drawEllipse(QRectF(6.0, 9.5, 2.0, 2.0));
    }

    return pixmap;
}

QString issueTypeSegment(const QString& text)
{
    QString normalized = text.trimmed();
    const int split = normalized.indexOf(QStringLiteral(": "));
    if (split > 0) {
        normalized = normalized.left(split).trimmed();
    }
    return normalized;
}

QString issueDetailTail(const QString& text)
{
    const QString normalized = text.trimmed();
    const int split = normalized.indexOf(QStringLiteral(": "));
    if (split < 0 || split + 2 >= normalized.size()) {
        return QString();
    }
    return normalized.mid(split + 2).trimmed();
}

ValidationMessageParts parseValidationMessage(const QString& rawMessage)
{
    ValidationMessageParts parts;
    parts.body = rawMessage.trimmed();
    const auto matchPrefix = [&](const QString& prefix, ValidationSeverityLevel severity) -> bool {
        if (!parts.body.startsWith(prefix)) {
            return false;
        }
        parts.severityPrefix = prefix;
        parts.severity = severity;
        parts.body = parts.body.mid(prefix.size()).trimmed();
        return true;
    };

    if (matchPrefix(QStringLiteral("[ERROR]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[WARNING]"), ValidationSeverityLevel::Warning)) return parts;
    if (matchPrefix(QStringLiteral("[错误]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[警告]"), ValidationSeverityLevel::Warning)) return parts;

    parts.severityPrefix = QStringLiteral("[ERROR]");
    parts.severity = ValidationSeverityLevel::Error;
    return parts;
}

QString highlightValidationDetailHtml(const QString& body)
{
    const QString escapedBody = body.toHtmlEscaped();
    const int split = body.indexOf(QStringLiteral(": "));
    if (split < 0 || split + 2 >= body.size()) {
        return escapedBody;
    }

    const QString prefix = body.left(split + 2).toHtmlEscaped();
    const QString codeTail = body.mid(split + 2).trimmed();
    if (codeTail.isEmpty()) {
        return escapedBody;
    }

    return QStringLiteral("%1<span style=\"color:%2;\">%3</span>")
        .arg(prefix, UiTheme::colors().accent.name(QColor::HexRgb), codeTail.toHtmlEscaped());
}

QColor severityColor(ValidationSeverityLevel severity)
{
    return severity == ValidationSeverityLevel::Warning
        ? QColor(QStringLiteral("#B07B00"))
        : QColor(QStringLiteral("#C62828"));
}

void appendMuriPerfLog(const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("edit/muri_perf"),
        payload,
        true
    );
}

QString validationIssueTypeKeyFromRawMessage(const QString& rawMessage)
{
    return QStringLiteral("validation:%1").arg(issueTypeSegment(rawMessage));
}

QString validationIssueTypeLabelFromDisplayMessage(const QString& displayMessage)
{
    const ValidationMessageParts parts = parseValidationMessage(displayMessage);
    return issueTypeSegment(parts.body.isEmpty() ? displayMessage : parts.body);
}

QString validationLocationText(int line, int col)
{
    return QStringLiteral("L%1 C%2").arg(qMax(1, line)).arg(qMax(1, col));
}

QString firstQuotedValidationToken(const QString& text)
{
    const int firstQuote = text.indexOf(QLatin1Char('\''));
    if (firstQuote < 0) {
        return QString();
    }
    const int secondQuote = text.indexOf(QLatin1Char('\''), firstQuote + 1);
    if (secondQuote <= firstQuote + 1) {
        return QString();
    }
    return text.mid(firstQuote + 1, secondQuote - firstQuote - 1).trimmed();
}

QString extractValidationSummaryFocusText(
    const QString& rawMessage,
    const QString& displayMessage,
    const QString& issueTypeLabel)
{
    const QString normalizedRaw = rawMessage.trimmed();
    if (!normalizedRaw.isEmpty()) {
        QString tail = issueDetailTail(normalizedRaw);
        if (!tail.isEmpty()) {
            const int unknownShapeIndex = tail.indexOf(QStringLiteral(" unknown shape "));
            if (unknownShapeIndex > 0) {
                tail = tail.left(unknownShapeIndex).trimmed();
            }
            const QString strictBeatSuffix = QStringLiteral(" (must be a positive divisor of 384)");
            if (tail.endsWith(strictBeatSuffix)) {
                tail.chop(strictBeatSuffix.size());
                tail = tail.trimmed();
            }
            if (!tail.isEmpty()) {
                return tail;
            }
        }

        const QString quotedToken = firstQuotedValidationToken(normalizedRaw);
        if (!quotedToken.isEmpty()) {
            return quotedToken;
        }

        if (normalizedRaw == QStringLiteral("Unterminated BPM block")) {
            return QStringLiteral("(");
        }
        if (normalizedRaw == QStringLiteral("Unterminated beat block")) {
            return QStringLiteral("{");
        }
        if (normalizedRaw == QStringLiteral("Unterminated <HS*> bracket")) {
            return QStringLiteral("<HS*");
        }
        if (normalizedRaw == QStringLiteral("Invalid <HS*N> value")) {
            return QStringLiteral("<HS*");
        }
        if (normalizedRaw == QStringLiteral("Invalid BPM value")) {
            return QStringLiteral("BPM");
        }
        if (normalizedRaw == QStringLiteral("Invalid beat value")) {
            return QStringLiteral("beat");
        }
    }

    const ValidationMessageParts displayParts = parseValidationMessage(displayMessage);
    const QString displayTail = issueDetailTail(displayParts.body);
    if (!displayTail.isEmpty()) {
        return displayTail;
    }
    if (!issueTypeLabel.trimmed().isEmpty()) {
        return issueTypeLabel.trimmed();
    }
    return displayParts.body.trimmed();
}

QString muriIssueTypeKey(MuriKind kind)
{
    return QStringLiteral("muri:%1").arg(static_cast<int>(kind));
}

struct WrappedListEntryText {
    QString html;
    QString plainText;
};

const MuriAnalysisReport& alignedMuriAnalysisReportForUi(
    const QByteArray& latestSignature,
    const QByteArray& analysisSignature,
    const MuriAnalysisReport& report)
{
    static const MuriAnalysisReport kEmptyReport;
    return latestSignature == analysisSignature ? report : kEmptyReport;
}

const QVector<MuriStaticReference>& alignedMuriStaticReferencesForUi(
    const QByteArray& latestSignature,
    const QByteArray& analysisSignature,
    const QVector<MuriStaticReference>& references)
{
    static const QVector<MuriStaticReference> kEmptyReferences;
    return latestSignature == analysisSignature ? references : kEmptyReferences;
}

QString muriLocationText(int line, int col)
{
    return QStringLiteral("L%1C%2").arg(qMax(1, line)).arg(qMax(1, col));
}

QString localizeMuriEntityText(QString text, bool chineseUi)
{
    text = text.trimmed();
    if (!chineseUi || text.isEmpty()) {
        return text;
    }
    static const QString kProtectedPrefix = QStringLiteral("protected ");
    if (text.startsWith(kProtectedPrefix, Qt::CaseInsensitive)) {
        text = QStringLiteral("保护 %1").arg(text.mid(kProtectedPrefix.size()).trimmed());
    }
    return text;
}

QString localizeMuriDetail(QString rawDetail, bool chineseUi)
{
    rawDetail = rawDetail.trimmed();
    if (!chineseUi || rawDetail.isEmpty()) {
        return rawDetail;
    }

    const auto unwrap = [](const QString& text, const QString& prefix, const QString& suffix, QString* middle) {
        if (middle == nullptr || !text.startsWith(prefix) || !text.endsWith(suffix)) {
            return false;
        }
        *middle = text.mid(prefix.size(), text.size() - prefix.size() - suffix.size()).trimmed();
        return true;
    };
    const auto trimTrailingPeriod = [](QString text) {
        text = text.trimmed();
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
        return text.trimmed();
    };
    const auto splitText = [](const QString& text, const QString& separator, QString* left, QString* right) {
        if (left == nullptr || right == nullptr) {
            return false;
        }
        const int splitIndex = text.indexOf(separator);
        if (splitIndex < 0) {
            return false;
        }
        *left = text.left(splitIndex).trimmed();
        *right = text.mid(splitIndex + separator.size()).trimmed();
        return !left->isEmpty() && !right->isEmpty();
    };
    const auto splitGapText =
        [&trimTrailingPeriod](const QString& text,
                              const QString& separator,
                              QString* left,
                              QString* right,
                              QString* gapText) {
            if (left == nullptr || right == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            const int gapIndex = text.lastIndexOf(QStringLiteral(", gap "));
            if (splitIndex < 0 || gapIndex < 0 || gapIndex <= splitIndex) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *right =
                text.mid(splitIndex + separator.size(), gapIndex - splitIndex - separator.size()).trimmed();
            *gapText = trimTrailingPeriod(text.mid(gapIndex + QStringLiteral(", gap ").size()));
            return !left->isEmpty() && !right->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };
    const auto splitSingleGapText =
        [&trimTrailingPeriod](const QString& text,
                              const QString& separator,
                              QString* left,
                              QString* gapText) {
            if (left == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            if (splitIndex < 0) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *gapText = trimTrailingPeriod(text.mid(splitIndex + separator.size()));
            return !left->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };

    QString middle;
    QString left;
    QString right;
    QString gapText;
    if (splitSingleGapText(
            rawDetail,
            QStringLiteral(" start will early-judge a following tap, gap "),
            &left,
            &gapText)) {
        return QStringLiteral("%1 启动，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitSingleGapText(
            rawDetail,
            QStringLiteral(" jump-start will early-judge a following tap, gap "),
            &left,
            &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start will early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start may early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，可能会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start early-judges "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start will early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start may early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，可能会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start early-judges "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory may collide with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹可能会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory will collide with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory collides with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" was early-judged by "), &left, &right, &gapText)) {
        return QStringLiteral("%1 被 %2 提前判定，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (rawDetail.endsWith(QStringLiteral(" resolved outside its critical window."))
        || rawDetail.contains(QStringLiteral(" resolved outside its critical window, gap "))) {
        const int splitIndex = rawDetail.indexOf(QStringLiteral(" resolved outside its critical window"));
        if (splitIndex > 0) {
            const QString target = rawDetail.left(splitIndex).trimmed();
            const int gapIndex = rawDetail.lastIndexOf(QStringLiteral(", gap "));
            if (gapIndex > splitIndex) {
                gapText = trimTrailingPeriod(rawDetail.mid(gapIndex + QStringLiteral(", gap ").size()));
                return QStringLiteral("%1 提前完成，间隔 %2。").arg(target, gapText);
            }
            return QStringLiteral("%1 的判定落在临界窗之外。").arg(target);
        }
    }
    if (unwrap(rawDetail, QStringLiteral("Multi-touch formed by "), QStringLiteral("."), &middle)) {
        return QStringLiteral("%1 构成多押。").arg(middle);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap."))
        && splitText(
            trimTrailingPeriod(rawDetail.left(rawDetail.size() - QStringLiteral(" formed overlap.").size())),
            QStringLiteral(" and same-position "),
            &left,
            &right)) {
        return QStringLiteral("%1 与相同位置的 %2 构成叠键。").arg(left, right);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap at the same position."))) {
        const QString target = rawDetail.left(
            rawDetail.size() - QStringLiteral(" formed overlap at the same position.").size()).trimmed();
        if (!target.isEmpty()) {
            return QStringLiteral("%1 在相同位置构成叠键。").arg(target);
        }
    }

    if (unwrap(
            rawDetail,
            QStringLiteral("Slide-head trigger from "),
            QStringLiteral(" early-judged this note."),
            &middle)) {
        return QStringLiteral("来自 %1 的滑键头触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Pad trigger from "),
            QStringLiteral(" early-judged this note."),
            &middle)) {
        return QStringLiteral("来自 %1 的按下触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Runtime simple-note judge for "),
            QStringLiteral(" missed the critical window and resolved as overlap."),
            &middle)) {
        return QStringLiteral("运行时普通物件判定 %1 错过了判定临界窗，并最终判为叠键。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Slide runtime judge for "),
            QStringLiteral(" resolved outside its critical window."),
            &middle)) {
        return QStringLiteral("Slide 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Slide "),
            QStringLiteral(" was cleared earlier than its normal judge timing."),
            &middle)) {
        return QStringLiteral("Slide %1 的完成时间早于正常判定时机。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Wifi runtime judge for "),
            QStringLiteral(" resolved outside its critical window."),
            &middle)) {
        return QStringLiteral("Wifi 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Wifi "),
            QStringLiteral(" was cleared earlier than its normal judge timing."),
            &middle)) {
        return QStringLiteral("Wifi %1 的完成时间早于正常判定时机。").arg(middle);
    }

    if (rawDetail.startsWith(QStringLiteral("Static reference from "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Static reference from ").size()).trimmed();
        const int deltaIndex = tail.lastIndexOf(QStringLiteral(", Δ "));
        if (deltaIndex >= 0 && tail.endsWith(QStringLiteral(" ms"))) {
            const QString source = tail.left(deltaIndex).trimmed();
            const QString deltaText = tail.mid(deltaIndex + QStringLiteral(", Δ ").size());
            return QStringLiteral("静态参考：%1，Δ %2").arg(source, deltaText);
        }
        return QStringLiteral("静态参考：%1").arg(tail);
    }

    if (rawDetail.startsWith(QStringLiteral("Runtime hand actions formed "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Runtime hand actions formed ").size()).trimmed();
        const QString marker = QStringLiteral("-hand multi-touch: ");
        const int split = tail.indexOf(marker);
        if (split > 0) {
            const QString handCount = tail.left(split).trimmed();
            const QString actions = tail.mid(split + marker.size()).trimmed();
            return QStringLiteral("运行时手部动作形成了 %1 手多押：%2").arg(handCount, actions);
        }
    }

    return rawDetail;
}

WrappedListEntryText buildMuriPanelEntryText(const MuriPanelEntry& entry, bool ignoredInHeader)
{
    const bool chineseUi = UiText::isChineseUi();
    const ValidationSeverityLevel severity = entry.alertLevel == MuriAlertLevel::Warning
        ? ValidationSeverityLevel::Warning
        : ValidationSeverityLevel::Error;
    const QColor summaryColor = ignoredInHeader
        ? UiTheme::colors().textMuted
        : severityColor(severity);
    const QString alertText = QStringLiteral("[%1]").arg(UiText::localized(
        muriAlertLevelDisplayName(entry.alertLevel, false), muriAlertLevelDisplayName(entry.alertLevel, true)));
    const QString typeText = UiText::localized(
        muriKindDisplayName(entry.kind, false), muriKindDisplayName(entry.kind, true));
    const QString locationText = muriLocationText(entry.line, entry.col);
    // Muri detail text stays English/Chinese only: the detail localizer is a
    // text transform, not a table — Japanese falls back to English here.
    const QString detailText = localizeMuriDetail(entry.rawDetail, chineseUi).trimmed();
    const QString summaryText = QStringLiteral("%1 %2 %3").arg(alertText, typeText, locationText);
    QString contentHtml = QStringLiteral("<span style=\"font-weight:700;color:%1;\">%2</span>")
                              .arg(summaryColor.name(QColor::HexRgb), summaryText.toHtmlEscaped());

    WrappedListEntryText text;
    if (!detailText.isEmpty()) {
        contentHtml += QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
                           .arg(
                               (ignoredInHeader ? UiTheme::colors().textSecondary : UiTheme::colors().textPrimary)
                                   .name(QColor::HexRgb),
                               detailText.toHtmlEscaped());
    }
    text.html = QStringLiteral("<div style=\"line-height:128%; margin:1px 0;\">%1</div>").arg(contentHtml);

    text.plainText = detailText.isEmpty()
        ? summaryText
        : QStringLiteral("%1\n%2").arg(summaryText, detailText);
    return text;
}

WrappedListEntryText buildValidationPanelEntryText(
    const ValidationMessageParts& parts,
    const QString& summaryFocusText,
    const QString& detailText,
    int line,
    int col,
    bool ignoredInHeader)
{
    const QColor summaryColor = ignoredInHeader
        ? UiTheme::colors().textMuted
        : severityColor(parts.severity);
    const QString normalizedDetailText = detailText.trimmed();
    const QString normalizedSummaryFocus = summaryFocusText.trimmed().isEmpty()
        ? parts.body.trimmed()
        : summaryFocusText.trimmed();
    const QString summaryText = QStringLiteral("%1 %2 %3")
                                    .arg(parts.severityPrefix, normalizedSummaryFocus, validationLocationText(line, col));

    QString contentHtml = QStringLiteral("<span style=\"font-weight:700;color:%1;\">%2</span>")
                              .arg(summaryColor.name(QColor::HexRgb), summaryText.toHtmlEscaped());

    WrappedListEntryText text;
    if (!normalizedDetailText.isEmpty()) {
        contentHtml += QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
                           .arg(
                               (ignoredInHeader ? UiTheme::colors().textSecondary : UiTheme::colors().textPrimary)
                                   .name(QColor::HexRgb),
                               normalizedDetailText.toHtmlEscaped());
    }
    text.html = QStringLiteral("<div style=\"line-height:128%; margin:1px 0;\">%1</div>").arg(contentHtml);
    text.plainText = normalizedDetailText.isEmpty()
        ? summaryText
        : QStringLiteral("%1\n%2").arg(summaryText, normalizedDetailText);
    return text;
}

int wrappedRichTextHeight(const QString& html, const QFont& font, int width)
{
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(html);
    document.setTextWidth(qMax(1, width));
    return qMax(1, qCeil(document.size().height()));
}

}  // namespace

void MainWindow::ValidationSection::showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList)
{
    if (list == nullptr) {
        return;
    }
    QListWidgetItem* item = list->itemAt(pos);
    if (item == nullptr) {
        return;
    }

    const QString issueTypeKey = item->data(kIssueTypeKeyRole).toString();
    const QString issueTypeLabel = item->data(kIssueTypeLabelRole).toString();
    const bool ignoredInHeader = item->data(kIssueIgnoredRole).toBool();

    QMenu menu(&owner_);
    styleRoundedMenu(menu);

    QAction* copyAction = menu.addAction(
        UiText::localized(QStringLiteral("Copy Info"), QStringLiteral("复制信息"))
    );
    connect(copyAction, &QAction::triggered, &owner_, [this, item]() {
        const QString text = item->toolTip().trimmed();
        QGuiApplication::clipboard()->setText(text);
        if (owner_.statusBar() != nullptr) {
            owner_.statusBar()->showMessage(
                UiText::localized(QStringLiteral("Issue info copied."), QStringLiteral("已复制信息。"))
            );
        }
    });

    QAction* jumpAction = menu.addAction(
        UiText::localized(QStringLiteral("Jump to Source"), QStringLiteral("跳转到源"))
    );
    connect(jumpAction, &QAction::triggered, &owner_, [this, item, muriList]() {
        if (muriList) {
            onMuriItemActivated(item);
        } else {
            onErrorItemActivated(item);
        }
    });

    if (!issueTypeKey.isEmpty()) {
        QAction* ignoreAction = menu.addAction(
            ignoredInHeader
                ? (UiText::localized(QStringLiteral("Stop Ignoring This Issue Type"), QStringLiteral("取消忽视该类型提示")))
                : (UiText::localized(QStringLiteral("Ignore This Issue Type"), QStringLiteral("忽视该类型提示")))
        );
        connect(ignoreAction, &QAction::triggered, &owner_, [this, issueTypeKey, ignoredInHeader]() {
            const MainWindow::BottomTabsTabId previousTabId = owner_.currentBottomTabsTabId();
            setIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey, !ignoredInHeader);
            refreshValidationPanelForActiveField();
            refreshMuriDiagnosticsPanel();
            owner_.restoreBottomTabsCurrentTabAfterRefresh(previousTabId);
        });
    }

    menu.exec(list->viewport()->mapToGlobal(pos));
}

void MainWindow::ValidationSection::refreshMuriDiagnosticsPanel()
{
    if (ui_.muriList_ == nullptr) {
        state_.pendingMuriPanelRefresh_ = false;
        updateEditorValidationSummary();
        return;
    }

    if (!isMuriDiagnosticsTabActive()) {
        state_.pendingMuriPanelRefresh_ = true;
        updateEditorValidationSummary();
        appendMuriPerfLog(QStringLiteral("phase=panel_deferred active=0"));
        return;
    }

    rebuildMuriDiagnosticsPanel();
}

void MainWindow::ValidationSection::flushPendingMuriDiagnosticsPanelRefresh()
{
    if (!state_.pendingMuriPanelRefresh_ || !isMuriDiagnosticsTabActive()) {
        return;
    }
    rebuildMuriDiagnosticsPanel();
}

bool MainWindow::ValidationSection::isMuriDiagnosticsTabActive() const
{
    if (ui_.muriList_ == nullptr) {
        return false;
    }
    if (owner_.quickShellBottomTabsProxyActive() && ui_.quickShellBottomTabsProxy_ != nullptr) {
        return ui_.quickShellBottomTabsProxy_->currentWidget() == ui_.muriList_;
    }
    return ui_.bottomTabs_ != nullptr && ui_.bottomTabs_->currentWidget() == ui_.muriList_;
}

void MainWindow::ValidationSection::rebuildMuriDiagnosticsPanel()
{
    if (ui_.muriList_ == nullptr) {
        state_.pendingMuriPanelRefresh_ = false;
        updateEditorValidationSummary();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    ui_.muriList_->clear();
    const MuriAnalysisReport& alignedMuriReport = alignedMuriAnalysisReportForUi(
        state_.latestTimelineNoteMarkerSignature_,
        state_.muriAnalysisReportNoteMarkerSignature_,
        state_.muriAnalysisReport_);
    const QVector<MuriStaticReference>& alignedStaticReferences = alignedMuriStaticReferencesForUi(
        state_.latestTimelineNoteMarkerSignature_,
        state_.muriAnalysisReportNoteMarkerSignature_,
        state_.muriStaticReferences_);
    if (alignedMuriReport.diagnostics.isEmpty() && alignedStaticReferences.isEmpty()) {
        auto* item = new QListWidgetItem(
            UiText::localized(QStringLiteral("No muri issues detected."), QStringLiteral("未检测到无理。")),
            ui_.muriList_
        );
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        state_.pendingMuriPanelRefresh_ = false;
        updateEditorValidationSummary();
        appendMuriPerfLog(
            QStringLiteral("phase=panel_rebuild entries=0 diagnostics=0 static_refs=0 elapsed_ms=%1")
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
        return;
    }

    QVector<MuriPanelEntry> entries =
        miacode::muri::buildVisibleMuriPanelEntries(alignedMuriReport, alignedStaticReferences);
    std::sort(entries.begin(), entries.end(), [](const MuriPanelEntry& a, const MuriPanelEntry& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });

    int entryIndex = 0;
    int addedItemCount = 0;
    for (const MuriPanelEntry& entry : entries) {
        const QString issueTypeKey = muriIssueTypeKey(entry.kind);
        const bool ignoredInHeader = isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey);
        const QString title = UiText::localized(
            muriKindDisplayName(entry.kind, false), muriKindDisplayName(entry.kind, true));
        const WrappedListEntryText text = buildMuriPanelEntryText(entry, ignoredInHeader);
        QListWidgetItem* item = addWrappedListEntry(
            ui_.muriList_,
            text.html,
            text.plainText,
            entry.line,
            entry.col,
            entry.second,
            true
        );
        if (item != nullptr) {
            item->setData(kIssueTypeKeyRole, issueTypeKey);
            item->setData(kIssueTypeLabelRole, title);
            item->setData(kIssueIgnoredRole, ignoredInHeader);
            ++addedItemCount;
        }
        appendMuriPerfLog(
            QStringLiteral(
                "phase=panel_entry_added index=%1 kind=%2 line=%3 col=%4 second=%5 "
                "is_static=%6 ignored_in_header=%7 item_added=%8 list_count=%9 "
                "plain_text_chars=%10 html_chars=%11"
            )
                .arg(entryIndex)
                .arg(static_cast<int>(entry.kind))
                .arg(entry.line)
                .arg(entry.col)
                .arg(entry.second, 0, 'f', 6)
                .arg(entry.isStatic ? 1 : 0)
                .arg(ignoredInHeader ? 1 : 0)
                .arg(item != nullptr ? 1 : 0)
                .arg(ui_.muriList_->count())
                .arg(text.plainText.size())
                .arg(text.html.size())
        );
        ++entryIndex;
    }
    state_.pendingMuriPanelRefresh_ = false;
    scheduleWrappedListRelayout(ui_.muriList_);
    updateEditorValidationSummary();
    appendMuriPerfLog(
        QStringLiteral(
            "phase=panel_rebuild entries=%1 diagnostics=%2 static_refs=%3 "
            "added_items=%4 list_count=%5 elapsed_ms=%6"
        )
            .arg(entries.size())
            .arg(alignedMuriReport.diagnostics.size())
            .arg(alignedStaticReferences.size())
            .arg(addedItemCount)
            .arg(ui_.muriList_->count())
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
    );
}

void MainWindow::ValidationSection::clearValidationCache()
{
    state_.validationCacheByDifficulty_.clear();
    updateEditorValidationSummary();
}

void MainWindow::ValidationSection::applyDeferredAnalysisUiUpdates()
{
    if (state_.qtPreviewPlaying_) {
        return;
    }

    if (state_.pendingDeferredMuriUiRefresh_) {
        if (state_.previewCanvas_ != nullptr) {
            state_.previewCanvas_->setMuriRenderOptions(state_.muriRenderOptions_);
        }
        owner_.applyAlignedMuriAnalysisReportToViews();
        refreshMuriDiagnosticsPanel();
        state_.pendingDeferredMuriUiRefresh_ = false;
    }

    if (state_.pendingDeferredValidationUiRefresh_) {
        refreshValidationPanelForActiveField();
        state_.pendingDeferredValidationUiRefresh_ = false;
    }
}

void MainWindow::ValidationSection::setValidationTabVisible(bool visible)
{
    owner_.setBottomTabsTabVisible(MainWindow::BottomTabsTabId::Validation, visible);
}

void MainWindow::ValidationSection::refreshValidationPanelForActiveField()
{
    if (!owner_.hasActiveDifficulty()) {
        setValidationTabVisible(false);
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    setValidationTabVisible(true);
    const int difficultyId = owner_.activeDifficultyId();
    const auto it = state_.validationCacheByDifficulty_.constFind(difficultyId);
    if (it == state_.validationCacheByDifficulty_.constEnd()) {
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    const QString chartText = owner_.activeChartText();
    const SimaiNativeValidationLocale validationLocale = uiValidationLocale();
    const miacode::simai::SimaiTimingMetadata timingMetadata = owner_.currentTimingMetadata();
    const ValidationCacheEntry& entry = it.value();
    if (entry.chartText != chartText
        || entry.validationLocale != validationLocale
        || entry.timingMetadata != timingMetadata) {
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    clearValidationErrors();
    state_.validationDecorations_.clear();
    if (entry.issues.isEmpty() && ui_.errorList_ != nullptr) {
        auto* item = new QListWidgetItem(
            UiText::localized(QStringLiteral("No syntax errors detected."), QStringLiteral("未检测到语法错误。")),
            ui_.errorList_
        );
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    for (const ValidationCachedIssue& issue : entry.issues) {
        const QString issueTypeKey = issue.issueTypeKey.isEmpty()
            ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
            : issue.issueTypeKey;
        const QString issueTypeLabel = issue.issueTypeLabel.isEmpty()
            ? validationIssueTypeLabelFromDisplayMessage(issue.displayMessage)
            : issue.issueTypeLabel;
        addValidationError(
            issue.line,
            issue.col,
            issue.rawMessage,
            issue.displayMessage,
            issueTypeKey,
            issueTypeLabel,
            isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey)
        );
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    refreshEditorExtraSelectionsForReason(QStringLiteral("validation_refresh"));
    scheduleWrappedListRelayout(ui_.errorList_);
    updateEditorValidationSummary();
}

bool MainWindow::ValidationSection::runValidateSimaiSilently(bool focusFirstIssue)
{
    if (!owner_.hasActiveDifficulty()) {
        refreshValidationPanelForActiveField();
        return false;
    }

    const int difficultyId = owner_.activeDifficultyId();
    const QString chartText = owner_.activeChartText();
    const SimaiNativeValidationLocale validationLocale = uiValidationLocale();
    const miacode::simai::SimaiTimingMetadata timingMetadata = owner_.currentTimingMetadata();
    const SimaiNativeParseResult* cachedLenientResult =
        (state_.lastTimelineParseDifficultyId_ == difficultyId
            && state_.lastTimelineParseChartText_ == chartText
            && state_.lastTimelineParseTimingMetadata_ == timingMetadata)
        ? &state_.lastTimelineParseResult_
        : nullptr;

    ValidationCacheEntry entry;
    const auto cacheIt = state_.validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != state_.validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->validationLocale == validationLocale
        && cacheIt->timingMetadata == timingMetadata) {
        entry = cacheIt.value();
    } else {
        QElapsedTimer reportTimer;
        reportTimer.start();
        const SimaiNativeValidationReport report =
            SimaiNativeParser::buildValidationReport(chartText, validationLocale, cachedLenientResult, timingMetadata);
        entry.chartText = chartText;
        entry.validationLocale = validationLocale;
        entry.timingMetadata = timingMetadata;
        entry.ok = report.ok;
        entry.errorCount = report.errorCount;
        entry.warningCount = report.warningCount;
        entry.lenientNoteCount = report.lenientNoteCount;
        entry.lenientErrorCount = report.lenientErrorCount;
        entry.strictNoteCount = report.strictNoteCount;
        entry.strictErrorCount = report.strictErrorCount;
        entry.issues.clear();
        entry.issues.reserve(report.issues.size());
        for (const SimaiNativeValidationIssue& issue : report.issues) {
            ValidationCachedIssue cachedIssue;
            cachedIssue.line = issue.line;
            cachedIssue.col = issue.col;
            cachedIssue.endCol = issue.endCol;
            cachedIssue.rawMessage = issue.rawMessage;
            cachedIssue.displayMessage = issue.displayMessage;
            cachedIssue.issueTypeKey = validationIssueTypeKeyFromRawMessage(issue.rawMessage);
            cachedIssue.issueTypeLabel = validationIssueTypeLabelFromDisplayMessage(issue.displayMessage);
            entry.issues.append(cachedIssue);
        }
        state_.validationCacheByDifficulty_.insert(difficultyId, entry);
        if (state_.runtimeDebugOutputEnabled_) {
            owner_.windowSection_->appendOutput(
                "edit/validation_perf",
                QStringLiteral("build_report=%1ms issues=%2 cached_lenient=%3")
                    .arg(reportTimer.elapsed())
                    .arg(entry.issues.size())
                    .arg(cachedLenientResult != nullptr ? QStringLiteral("1") : QStringLiteral("0"))
            );
        }
    }

    QElapsedTimer applyTimer;
    applyTimer.start();
    setValidationTabVisible(true);
    clearValidationErrors();
    state_.validationDecorations_.clear();
    if (entry.issues.isEmpty() && ui_.errorList_ != nullptr) {
        auto* item = new QListWidgetItem(
            UiText::localized(QStringLiteral("No syntax errors detected."), QStringLiteral("未检测到语法错误。")),
            ui_.errorList_
        );
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    for (const ValidationCachedIssue& issue : entry.issues) {
        const QString issueTypeKey = issue.issueTypeKey.isEmpty()
            ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
            : issue.issueTypeKey;
        const QString issueTypeLabel = issue.issueTypeLabel.isEmpty()
            ? validationIssueTypeLabelFromDisplayMessage(issue.displayMessage)
            : issue.issueTypeLabel;
        addValidationError(
            issue.line,
            issue.col,
            issue.rawMessage,
            issue.displayMessage,
            issueTypeKey,
            issueTypeLabel,
            isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey)
        );
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    refreshEditorExtraSelectionsForReason(QStringLiteral("explicit_validate"));
    scheduleWrappedListRelayout(ui_.errorList_);
    updateEditorValidationSummary();
    if (focusFirstIssue && !entry.issues.isEmpty() && ui_.bottomTabs_ != nullptr && ui_.errorList_ != nullptr) {
        owner_.setCurrentBottomTabsTabId(MainWindow::BottomTabsTabId::Validation);
        onErrorItemActivated(ui_.errorList_->item(0));
    }
    if (state_.runtimeDebugOutputEnabled_) {
        owner_.windowSection_->appendOutput(
            "edit/validation_apply_perf",
            QStringLiteral("apply_ui=%1ms issues=%2")
                .arg(applyTimer.elapsed())
                .arg(entry.issues.size())
        );
    }
    return entry.ok;
}

void MainWindow::showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList)
{
    validationSection_->showIssueListContextMenu(list, pos, muriList);
}

void MainWindow::refreshMuriDiagnosticsPanel()
{
    validationSection_->refreshMuriDiagnosticsPanel();
}

void MainWindow::clearValidationCache()
{
    validationSection_->clearValidationCache();
}

void MainWindow::applyDeferredAnalysisUiUpdates()
{
    validationSection_->applyDeferredAnalysisUiUpdates();
}

void MainWindow::setValidationTabVisible(bool visible)
{
    validationSection_->setValidationTabVisible(visible);
}

void MainWindow::refreshValidationPanelForActiveField()
{
    validationSection_->refreshValidationPanelForActiveField();
}

bool MainWindow::runValidateSimaiSilently(bool focusFirstIssue)
{
    return validationSection_->runValidateSimaiSilently(focusFirstIssue);
}
