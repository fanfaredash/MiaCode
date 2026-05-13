#include "MainWindow.ValidationSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "EditorSelectionUtils.h"
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
#include "common/OperationLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::ValidationSection::ValidationSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

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

void appendValidationPerfLog(const QString& tag, const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        tag,
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
    const QString alertText = QStringLiteral("[%1]").arg(muriAlertLevelDisplayName(entry.alertLevel, chineseUi));
    const QString typeText = muriKindDisplayName(entry.kind, chineseUi);
    const QString locationText = muriLocationText(entry.line, entry.col);
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

void ensureEditorSelectionVisible(PlainCodeEditor* editor, const QTextCursor& selectionCursor)
{
    if (editor == nullptr || editor->viewport() == nullptr) {
        return;
    }

    QTextCursor startCursor = selectionCursor;
    startCursor.setPosition(selectionCursor.selectionStart());
    startCursor.clearSelection();
    QTextCursor endCursor = selectionCursor;
    const int endPosition = qMax(selectionCursor.selectionStart(), selectionCursor.selectionEnd() - 1);
    endCursor.setPosition(endPosition);
    endCursor.clearSelection();

    const auto selectionRect = [editor, &startCursor, &endCursor]() {
        return editor->cursorRect(startCursor).united(editor->cursorRect(endCursor));
    };

    const int verticalMargin = qMax(12, editor->fontMetrics().height());
    if (QScrollBar* vbar = editor->verticalScrollBar()) {
        QRect targetRect = selectionRect();
        if (targetRect.top() < verticalMargin) {
            const int nextValue = vbar->value() + targetRect.top() - verticalMargin;
            vbar->setValue(qBound(vbar->minimum(), nextValue, vbar->maximum()));
        } else if (targetRect.bottom() > editor->viewport()->height() - verticalMargin) {
            const int nextValue = vbar->value() + targetRect.bottom() - editor->viewport()->height() + verticalMargin;
            vbar->setValue(qBound(vbar->minimum(), nextValue, vbar->maximum()));
        }
    }

    const int horizontalMargin = qMax(16, editor->fontMetrics().horizontalAdvance(QLatin1Char('M')));
    if (QScrollBar* hbar = editor->horizontalScrollBar()) {
        QRect targetRect = selectionRect();
        if (targetRect.left() < horizontalMargin) {
            const int nextValue = hbar->value() + targetRect.left() - horizontalMargin;
            hbar->setValue(qBound(hbar->minimum(), nextValue, hbar->maximum()));
        } else if (targetRect.right() > editor->viewport()->width() - horizontalMargin) {
            const int nextValue = hbar->value() + targetRect.right() - editor->viewport()->width() + horizontalMargin;
            hbar->setValue(qBound(hbar->minimum(), nextValue, hbar->maximum()));
        }
    }
}

QByteArray buildEditorExtraSelectionsSignature(
    const QByteArray& validationSignature,
    bool previewFollowActive,
    int previewFollowStartLine,
    int previewFollowStartCol,
    int previewFollowEndLine,
    int previewFollowEndCol,
    int previewFollowCursorLine,
    int previewFollowCursorCol)
{
    QByteArray signature;
    QDataStream stream(&signature, QIODevice::WriteOnly);
    stream
        << validationSignature
        << previewFollowActive
        << previewFollowStartLine
        << previewFollowStartCol
        << previewFollowEndLine
        << previewFollowEndCol
        << previewFollowCursorLine
        << previewFollowCursorCol;
    return signature;
}

}  // namespace

QByteArray MainWindow::ValidationSection::buildValidationExtraSelectionsSignature() const
{
    QByteArray signature;
    QDataStream stream(&signature, QIODevice::WriteOnly);
    stream << static_cast<quint32>(state_.validationDecorations_.size());
    for (const ValidationDecoration& decoration : state_.validationDecorations_) {
        stream << decoration.line;
        stream << decoration.col;
        stream << decoration.endCol;
        stream << decoration.warning;
        stream << decoration.message;
    }
    return signature;
}

void MainWindow::ValidationSection::rebuildValidationExtraSelectionsCache(const QString& reason)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        state_.cachedValidationExtraSelections_.clear();
        state_.currentEditorExtraSelections_.clear();
        state_.validationExtraSelectionsSignature_.clear();
        state_.lastEditorExtraSelectionsSignature_.clear();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    const QByteArray signature = buildValidationExtraSelectionsSignature();
    if (signature == state_.validationExtraSelectionsSignature_) {
        appendValidationPerfLog(
            QStringLiteral("edit/extra_selections_validation_perf"),
            QStringLiteral("reason=%1 skipped=1 decorations=%2 selections=%3 elapsed_ms=%4")
                .arg(reason)
                .arg(state_.validationDecorations_.size())
                .arg(state_.cachedValidationExtraSelections_.size())
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
        return;
    }

    QVector<QTextEdit::ExtraSelection> selections;
    selections.reserve(state_.validationDecorations_.size());
    for (const ValidationDecoration& decoration : state_.validationDecorations_) {
        QTextCursor cursor;
        if (!miacode::mainwindow::editor_selection::buildSelectionCursor(
                editor,
                decoration.line,
                decoration.col,
                decoration.line,
                decoration.endCol,
                &cursor)) {
            continue;
        }

        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(decoration.warning
                                               ? severityColor(ValidationSeverityLevel::Warning)
                                               : severityColor(ValidationSeverityLevel::Error));
        selection.format.setToolTip(decoration.message);
        selections.append(selection);
    }

    state_.cachedValidationExtraSelections_ = selections;
    state_.currentEditorExtraSelections_.clear();
    state_.validationExtraSelectionsSignature_ = signature;
    state_.lastEditorExtraSelectionsSignature_.clear();
    appendValidationPerfLog(
        QStringLiteral("edit/extra_selections_validation_perf"),
        QStringLiteral("reason=%1 skipped=0 decorations=%2 selections=%3 elapsed_ms=%4")
            .arg(reason)
            .arg(state_.validationDecorations_.size())
            .arg(state_.cachedValidationExtraSelections_.size())
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
    );
}

void MainWindow::ValidationSection::applyEditorExtraSelectionsForReason(const QString& reason)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        return;
    }

    QElapsedTimer timer;
    timer.start();

    const QByteArray signature = buildEditorExtraSelectionsSignature(
        state_.validationExtraSelectionsSignature_,
        state_.previewFollowDecorationActive_,
        state_.previewFollowDecorationStartLine_,
        state_.previewFollowDecorationStartCol_,
        state_.previewFollowDecorationEndLine_,
        state_.previewFollowDecorationEndCol_,
        state_.previewFollowDecorationCursorLine_,
        state_.previewFollowDecorationCursorCol_);
    const int totalSelections = state_.cachedValidationExtraSelections_.size();
    if (signature == state_.lastEditorExtraSelectionsSignature_) {
        appendValidationPerfLog(
            QStringLiteral("edit/extra_selections_perf"),
            QStringLiteral("skipped=1 decorations=%1 preview_follow=%2 elapsed_ms=%3")
                .arg(state_.validationDecorations_.size())
                .arg(0)
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
        appendValidationPerfLog(
            QStringLiteral("edit/extra_selections_apply_perf"),
            QStringLiteral(
                "reason=%1 validation_selections=%2 follow_active=%3 total_selections=%4 elapsed_ms=%5")
                .arg(reason)
                .arg(state_.cachedValidationExtraSelections_.size())
                .arg(0)
                .arg(totalSelections)
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
        return;
    }

    state_.currentEditorExtraSelections_ = state_.cachedValidationExtraSelections_;
    if (state_.previewFollowDecorationActive_) {
        QTextCursor cursor;
        if (miacode::mainwindow::editor_selection::buildSelectionCursor(
                editor,
                state_.previewFollowDecorationStartLine_,
                state_.previewFollowDecorationStartCol_,
                state_.previewFollowDecorationEndLine_,
                state_.previewFollowDecorationEndCol_,
                &cursor)) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            QColor followColor = UiTheme::colors().accent;
            followColor.setAlpha(UiTheme::colors().dark ? 64 : 44);
            selection.format.setBackground(followColor);
            state_.currentEditorExtraSelections_.append(selection);
        }
    }
    editor->setExtraSelections(state_.currentEditorExtraSelections_);
    state_.lastEditorExtraSelectionsSignature_ = signature;
    appendValidationPerfLog(
        QStringLiteral("edit/extra_selections_perf"),
        QStringLiteral("skipped=0 decorations=%1 preview_follow=%2 selections=%3 elapsed_ms=%4")
            .arg(state_.validationDecorations_.size())
            .arg(0)
            .arg(state_.currentEditorExtraSelections_.size())
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
    );
    appendValidationPerfLog(
        QStringLiteral("edit/extra_selections_apply_perf"),
        QStringLiteral(
            "reason=%1 validation_selections=%2 follow_active=%3 total_selections=%4 elapsed_ms=%5")
            .arg(reason)
            .arg(state_.cachedValidationExtraSelections_.size())
            .arg(0)
            .arg(state_.currentEditorExtraSelections_.size())
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
    );
}

void MainWindow::ValidationSection::refreshEditorExtraSelectionsForReason(const QString& reason)
{
    rebuildValidationExtraSelectionsCache(reason);
    applyEditorExtraSelectionsForReason(reason);
}

void MainWindow::ValidationSection::refreshEditorExtraSelections()
{
    refreshEditorExtraSelectionsForReason(QStringLiteral("validation_refresh"));
}

void MainWindow::ValidationSection::updateEditorValidationSummary()
{
    if (ui_.editorValidationSummaryWidget_ == nullptr
        || ui_.editorValidationErrorIconLabel_ == nullptr
        || ui_.editorValidationErrorCountLabel_ == nullptr
        || ui_.editorValidationWarningIconLabel_ == nullptr
        || ui_.editorValidationWarningCountLabel_ == nullptr
        || ui_.editorValidationMuriIconLabel_ == nullptr
        || ui_.editorValidationMuriCountLabel_ == nullptr) {
        return;
    }

    if (!owner_.hasActiveDifficulty()) {
        ui_.editorValidationSummaryWidget_->hide();
        return;
    }

    int errorCount = 0;
    int warningCount = 0;
    int muriIssueCount = 0;
    const QSet<QString> ignoredTypes = state_.ignoredHeaderIssueTypesByFile_.value(currentValidationIgnoreScopeKey());
    const auto cacheIt = state_.validationCacheByDifficulty_.constFind(owner_.activeDifficultyId());
    if (cacheIt != state_.validationCacheByDifficulty_.constEnd()) {
        const ValidationCacheEntry& entry = cacheIt.value();
        if (entry.chartText == owner_.activeChartText()
            && entry.chineseUi == UiText::isChineseUi()
            && entry.timingMetadata == owner_.currentTimingMetadata()) {
            for (const ValidationCachedIssue& issue : entry.issues) {
                const QString issueTypeKey = issue.issueTypeKey.isEmpty()
                    ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
                    : issue.issueTypeKey;
                if (ignoredTypes.contains(issueTypeKey)) {
                    continue;
                }
                const ValidationMessageParts parts = parseValidationMessage(issue.displayMessage);
                if (parts.severity == ValidationSeverityLevel::Warning) {
                    ++warningCount;
                } else {
                    ++errorCount;
                }
            }
        }
    }
    const MuriAnalysisReport& alignedMuriReport = alignedMuriAnalysisReportForUi(
        state_.latestTimelineNoteMarkerSignature_,
        state_.muriAnalysisReportNoteMarkerSignature_,
        state_.muriAnalysisReport_);
    const QVector<MuriStaticReference>& alignedStaticReferences = alignedMuriStaticReferencesForUi(
        state_.latestTimelineNoteMarkerSignature_,
        state_.muriAnalysisReportNoteMarkerSignature_,
        state_.muriStaticReferences_);
    const QVector<MuriPanelEntry> muriEntries =
        miacode::muri::buildVisibleMuriPanelEntries(alignedMuriReport, alignedStaticReferences);
    for (const MuriPanelEntry& entry : muriEntries) {
        if (ignoredTypes.contains(muriIssueTypeKey(entry.kind))) {
            continue;
        }
        ++muriIssueCount;
    }

    const bool showError = errorCount > 0;
    const bool showWarning = warningCount > 0;
    const bool showMuri = muriIssueCount > 0;

    const QString summaryTooltip = showMuri
        ? uiText(
              "editor.validation_summary.tooltip_with_muri",
              "%1 error(s), %2 warning(s), %3 muri issue(s)"
          ).arg(errorCount).arg(warningCount).arg(muriIssueCount)
        : uiText(
              "editor.validation_summary.tooltip",
              "%1 error(s), %2 warning(s)"
          ).arg(errorCount).arg(warningCount);
    const bool showSummary = state_.previewShowValidationSummary_ && (showError || showWarning || showMuri);
    ui_.editorValidationSummaryWidget_->setProperty("hasContent", showSummary);
    ui_.editorValidationSummaryWidget_->setToolTip(summaryTooltip);

    struct SummaryEntry {
        EditorValidationSummaryIconKind kind;
        QColor color;
        int count = 0;
    };
    QVector<SummaryEntry> visibleEntries;
    if (showMuri) {
        visibleEntries.append(SummaryEntry{
            EditorValidationSummaryIconKind::Muri,
            QColor(QStringLiteral("#A24AD9")),
            muriIssueCount
        });
    }
    if (showWarning) {
        visibleEntries.append(SummaryEntry{
            EditorValidationSummaryIconKind::Warning,
            QColor(QStringLiteral("#D4A12A")),
            warningCount
        });
    }
    if (showError) {
        visibleEntries.append(SummaryEntry{
            EditorValidationSummaryIconKind::Error,
            QColor(QStringLiteral("#D94A4A")),
            errorCount
        });
    }

    const auto applySlot = [showSummary, &summaryTooltip](QLabel* icon, QLabel* count, const SummaryEntry* entry) {
        const bool occupied = showSummary && entry != nullptr && entry->count > 0;
        if (icon != nullptr) {
            if (occupied) {
                icon->setPixmap(makeEditorValidationSummaryIcon(entry->color, entry->kind));
            } else {
                icon->clear();
            }
            icon->setProperty("hasContent", occupied);
            icon->setVisible(occupied);
            icon->setToolTip(summaryTooltip);
        }
        if (count != nullptr) {
            if (occupied) {
                count->setText(QString::number(entry->count));
                count->setStyleSheet(QStringLiteral("color: %1;").arg(entry->color.name(QColor::HexRgb)));
            } else {
                count->clear();
                count->setStyleSheet(QString());
            }
            count->setProperty("hasContent", occupied);
            count->setVisible(occupied);
            count->setToolTip(summaryTooltip);
        }
    };
    const SummaryEntry* slot0 = visibleEntries.size() > 0 ? &visibleEntries[0] : nullptr;
    const SummaryEntry* slot1 = visibleEntries.size() > 1 ? &visibleEntries[1] : nullptr;
    const SummaryEntry* slot2 = visibleEntries.size() > 2 ? &visibleEntries[2] : nullptr;
    applySlot(ui_.editorValidationMuriIconLabel_, ui_.editorValidationMuriCountLabel_, slot0);
    applySlot(ui_.editorValidationWarningIconLabel_, ui_.editorValidationWarningCountLabel_, slot1);
    applySlot(ui_.editorValidationErrorIconLabel_, ui_.editorValidationErrorCountLabel_, slot2);
    ui_.editorValidationSummaryWidget_->setVisible(showSummary);
    ui_.editorValidationSummaryWidget_->adjustSize();
    owner_.updateEditorHeaderLayoutMode();
}

void MainWindow::ValidationSection::setPreviewFollowDecoration(
    int startLine,
    int startCol,
    int endLine,
    int endCol,
    int cursorLine,
    int cursorCol,
    bool ensureVisible)
{
    const int normalizedStartLine = qMax(1, startLine);
    const int normalizedStartCol = qMax(1, startCol);
    int normalizedEndLine = qMax(1, endLine >= 0 ? endLine : startLine);
    int normalizedEndCol = qMax(1, endCol >= 0 ? endCol : startCol);
    if (normalizedEndLine < normalizedStartLine
        || (normalizedEndLine == normalizedStartLine && normalizedEndCol < normalizedStartCol)) {
        normalizedEndLine = normalizedStartLine;
        normalizedEndCol = normalizedStartCol;
    }
    const int normalizedCursorLine = qMax(1, cursorLine >= 0 ? cursorLine : normalizedEndLine);
    const int normalizedCursorCol = qMax(1, cursorCol >= 0 ? cursorCol : normalizedEndCol);

    const bool decorationChanged = !state_.previewFollowDecorationActive_
        || state_.previewFollowDecorationStartLine_ != normalizedStartLine
        || state_.previewFollowDecorationStartCol_ != normalizedStartCol
        || state_.previewFollowDecorationEndLine_ != normalizedEndLine
        || state_.previewFollowDecorationEndCol_ != normalizedEndCol
        || state_.previewFollowDecorationCursorLine_ != normalizedCursorLine
        || state_.previewFollowDecorationCursorCol_ != normalizedCursorCol;

    if (state_.previewFollowDecorationActive_
        && state_.previewFollowDecorationStartLine_ == normalizedStartLine
        && state_.previewFollowDecorationStartCol_ == normalizedStartCol
        && state_.previewFollowDecorationEndLine_ == normalizedEndLine
        && state_.previewFollowDecorationEndCol_ == normalizedEndCol
        && state_.previewFollowDecorationCursorLine_ == normalizedCursorLine
        && state_.previewFollowDecorationCursorCol_ == normalizedCursorCol) {
        if (!ensureVisible) {
            return;
        }
    } else {
        state_.previewFollowDecorationActive_ = true;
        state_.previewFollowDecorationStartLine_ = normalizedStartLine;
        state_.previewFollowDecorationStartCol_ = normalizedStartCol;
        state_.previewFollowDecorationEndLine_ = normalizedEndLine;
        state_.previewFollowDecorationEndCol_ = normalizedEndCol;
        state_.previewFollowDecorationCursorLine_ = normalizedCursorLine;
        state_.previewFollowDecorationCursorCol_ = normalizedCursorCol;
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
            editor->setPreviewFollowVisualCaret(true, normalizedCursorLine, normalizedCursorCol);
        }
    }

    if (decorationChanged) {
        applyEditorExtraSelectionsForReason(QStringLiteral("preview_follow"));
    }

    if (!ensureVisible) {
        return;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        return;
    }

    QTextCursor cursor;
    if (!miacode::mainwindow::editor_selection::buildSelectionCursor(
            editor,
            state_.previewFollowDecorationStartLine_,
            state_.previewFollowDecorationStartCol_,
            state_.previewFollowDecorationEndLine_,
            state_.previewFollowDecorationEndCol_,
            &cursor)) {
        return;
    }
    ensureEditorSelectionVisible(editor, cursor);
}

void MainWindow::ValidationSection::clearPreviewFollowDecoration()
{
    if (!state_.previewFollowDecorationActive_) {
        return;
    }
    state_.previewFollowDecorationActive_ = false;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        editor->setPreviewFollowVisualCaret(false);
    }
    applyEditorExtraSelectionsForReason(QStringLiteral("preview_follow_clear"));
}

void MainWindow::ValidationSection::clearValidationErrors()
{
    if (ui_.errorList_ == nullptr) {
        return;
    }
    ui_.errorList_->clear();
}

void MainWindow::ValidationSection::clearMuriDiagnostics()
{
    if (ui_.muriList_ == nullptr) {
        state_.pendingMuriPanelRefresh_ = false;
        updateEditorValidationSummary();
        return;
    }
    ui_.muriList_->clear();
    state_.pendingMuriPanelRefresh_ = false;
    updateEditorValidationSummary();
}

void MainWindow::ValidationSection::clearValidationDecorations()
{
    state_.validationDecorations_.clear();
    refreshEditorExtraSelectionsForReason(QStringLiteral("validation_clear"));
}

void MainWindow::ValidationSection::addValidationError(
    int line,
    int col,
    const QString& rawMessage,
    const QString& displayMessage,
    const QString& issueTypeKey,
    const QString& issueTypeLabel,
    bool ignoredInHeader)
{
    if (ui_.errorList_ == nullptr) {
        return;
    }

    const ValidationMessageParts parts = parseValidationMessage(displayMessage);
    const QString issueTitle = issueTypeLabel.isEmpty() ? issueTypeSegment(parts.body) : issueTypeLabel;
    const QString detailTail = issueDetailTail(parts.body);
    const QString detailText = detailTail.isEmpty() ? parts.body : detailTail;
    const QString summaryFocusText = extractValidationSummaryFocusText(rawMessage, displayMessage, issueTitle);
    const WrappedListEntryText text =
        buildValidationPanelEntryText(parts, summaryFocusText, detailText, line, col, ignoredInHeader);
    QListWidgetItem* item = addWrappedListEntry(ui_.errorList_, text.html, text.plainText, line, col, -1.0, true);
    if (item != nullptr) {
        item->setData(kIssueAuxRole, parts.severity == ValidationSeverityLevel::Warning ? 1 : 0);
        item->setData(kIssueTypeKeyRole, issueTypeKey);
        item->setData(kIssueTypeLabelRole, issueTypeLabel);
        item->setData(kIssueIgnoredRole, ignoredInHeader);
    }
}

void MainWindow::ValidationSection::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }
    if (endCol < col) {
        endCol = col;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    ValidationDecoration decoration;
    decoration.line = qMax(1, line);
    decoration.col = qMax(1, col);
    decoration.endCol = qMax(decoration.col, endCol);
    decoration.message = message;
    decoration.warning = (parts.severity == ValidationSeverityLevel::Warning);
    state_.validationDecorations_.append(decoration);
}

void MainWindow::ValidationSection::jumpToLocation(int line, int col)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }

    QTextCursor cursor;
    if (!miacode::mainwindow::editor_selection::buildCaretCursor(editor, line, col, &cursor)) {
        return;
    }
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    editor->setFocus();
    clearPreviewFollowDecoration();
}

void MainWindow::ValidationSection::onErrorItemActivated(QListWidgetItem* item)
{
    MC_OP("MainWindow::ValidationSection::onErrorItemActivated");
    if (item == nullptr) {
        return;
    }
    const int line = item->data(kIssueLineRole).toInt();
    const int col = item->data(kIssueColRole).toInt();
    _mc_op_.note(QStringLiteral("line=%1 col=%2").arg(line).arg(col));
    jumpToLocation(line, col);
}

void MainWindow::ValidationSection::onMuriItemActivated(QListWidgetItem* item)
{
    MC_OP("MainWindow::ValidationSection::onMuriItemActivated");
    if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) {
        return;
    }
    const int line = item->data(kIssueLineRole).toInt();
    const int col = item->data(kIssueColRole).toInt();
    const double second = item->data(kIssueAuxRole).toDouble();
    const bool previousSuppressState = state_.suppressTimelineCursorSync_;
    state_.suppressTimelineCursorSync_ = true;
    jumpToLocation(line, col);
    state_.suppressTimelineCursorSync_ = previousSuppressState;

    if (second < 0.0 || state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }

    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = true;
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    owner_.seekPreviewToSecond(clampedSecond, true);
    state_.timelineQuickStateBridge_->setCursorSeconds(clampedSecond, false);
    state_.timelineQuickStateBridge_->focusPlayhead(true);
}

void MainWindow::refreshEditorExtraSelections()
{
    validationSection_->refreshEditorExtraSelections();
}

void MainWindow::updateEditorValidationSummary()
{
    validationSection_->updateEditorValidationSummary();
}

void MainWindow::setPreviewFollowDecoration(
    int startLine,
    int startCol,
    int endLine,
    int endCol,
    int cursorLine,
    int cursorCol,
    bool ensureVisible)
{
    validationSection_->setPreviewFollowDecoration(
        startLine,
        startCol,
        endLine,
        endCol,
        cursorLine,
        cursorCol,
        ensureVisible);
}

void MainWindow::clearPreviewFollowDecoration()
{
    validationSection_->clearPreviewFollowDecoration();
}

void MainWindow::clearValidationErrors()
{
    validationSection_->clearValidationErrors();
}

void MainWindow::clearMuriDiagnostics()
{
    validationSection_->clearMuriDiagnostics();
}

void MainWindow::clearValidationDecorations()
{
    validationSection_->clearValidationDecorations();
}

void MainWindow::addValidationError(
    int line,
    int col,
    const QString& rawMessage,
    const QString& displayMessage,
    const QString& issueTypeKey,
    const QString& issueTypeLabel,
    bool ignoredInHeader)
{
    validationSection_->addValidationError(line, col, rawMessage, displayMessage, issueTypeKey, issueTypeLabel, ignoredInHeader);
}

void MainWindow::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    validationSection_->addValidationDecoration(line, col, message, endCol);
}

void MainWindow::jumpToLocation(int line, int col)
{
    validationSection_->jumpToLocation(line, col);
}

void MainWindow::onErrorItemActivated(QListWidgetItem* item)
{
    validationSection_->onErrorItemActivated(item);
}

void MainWindow::onMuriItemActivated(QListWidgetItem* item)
{
    validationSection_->onMuriItemActivated(item);
}
