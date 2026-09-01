#include "runtime/validation/ValidationHost.h"
#include "runtime/Shared.h"
#include "runtime/document/DocumentSessionHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
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

using namespace miacode::runtime::shared;

miacode::runtime::ValidationHost::ValidationHost(
    Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
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

// Extra-selections apply logging used to emit TWO lines per apply
// (`edit/extra_selections_perf` + `edit/extra_selections_apply_perf`) with
// overlapping fields. In a 39-minute field capture that pair was 46.5% of the
// whole runtime channel — 11,196 lines whose elapsed_ms was between 0.012 and
// 0.048 ms, i.e. pure volume with no diagnostic signal. The channel rotated
// (4 MB x 3 segments) inside two hours and took the pre-freeze evidence window
// with it.
//
// So: one line per apply instead of two, and only when it is worth reading —
// immediately if the apply was actually slow, otherwise folded into a periodic
// summary that preserves the rate and the worst case. GUI-thread only, which is
// why plain statics are safe here.
constexpr double kExtraSelectionsSlowMs = 1.0;
constexpr qint64 kExtraSelectionsSummaryIntervalMs = 5000;

struct ExtraSelectionsLogThrottle {
    qint64 lastEmitMs = 0;
    int suppressedCount = 0;
    double suppressedMaxMs = 0.0;
    double suppressedTotalMs = 0.0;
};

ExtraSelectionsLogThrottle& extraSelectionsLogThrottle()
{
    static ExtraSelectionsLogThrottle throttle;
    return throttle;
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

QString muriAlertLevelText(MuriAlertLevel level)
{
    switch (level) {
    case MuriAlertLevel::Muri:
        return UiText::text(QStringLiteral("validation.muri.alert.muri"));
    case MuriAlertLevel::Warning:
        return UiText::text(QStringLiteral("validation.muri.alert.warning"));
    }
    return UiText::text(QStringLiteral("validation.muri.alert.muri"));
}

QString muriKindText(MuriKind kind)
{
    switch (kind) {
    case MuriKind::SlideTooFast:
        return UiText::text(QStringLiteral("validation.muri.kind.slide_too_fast"));
    case MuriKind::SlideHeadTap:
        return UiText::text(QStringLiteral("validation.muri.kind.slide_head_tap"));
    case MuriKind::TapOnSlide:
        return UiText::text(QStringLiteral("validation.muri.kind.tap_on_slide"));
    case MuriKind::Overlap:
        return UiText::text(QStringLiteral("validation.muri.kind.overlap"));
    case MuriKind::MultiTouch:
        return UiText::text(QStringLiteral("validation.muri.kind.multi_touch"));
    }
    return UiText::text(QStringLiteral("validation.muri.alert.muri"));
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

WrappedListEntryText buildMuriPanelEntryText(const MuriPanelEntry& entry, bool ignoredInHeader)
{
    const ValidationSeverityLevel severity = entry.alertLevel == MuriAlertLevel::Warning
        ? ValidationSeverityLevel::Warning
        : ValidationSeverityLevel::Error;
    const QColor summaryColor = ignoredInHeader
        ? UiTheme::colors().textMuted
        : severityColor(severity);
    const QString alertText = QStringLiteral("[%1]").arg(muriAlertLevelText(entry.alertLevel));
    const QString typeText = muriKindText(entry.kind);
    const QString locationText = muriLocationText(entry.line, entry.col);
    const QString renderedDetail =
        renderMuriDetail(entry.detailKind, entry.detailArgs, uiValidationLocale()).trimmed();
    const QString detailText = renderedDetail.isEmpty() ? entry.rawDetail.trimmed() : renderedDetail;
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

void miacode::runtime::ValidationHost::updateEditorValidationSummary()
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

    if (!session_.hasActiveDifficulty()) {
        ui_.editorValidationSummaryWidget_->hide();
        return;
    }

    int errorCount = 0;
    int warningCount = 0;
    int muriIssueCount = 0;
    const QSet<QString> ignoredTypes = state_.ignoredHeaderIssueTypesByFile_.value(currentValidationIgnoreScopeKey());
    const auto cacheIt = state_.validationCacheByDifficulty_.constFind(session_.activeDifficultyId());
    if (cacheIt != state_.validationCacheByDifficulty_.constEnd()) {
        const Session::ValidationCacheEntry& entry = cacheIt.value();
        if (entry.chartText == session_.activeChartText()
            && entry.validationLocale == uiValidationLocale()
            && entry.timingMetadata == session_.currentTimingMetadata()) {
            for (const Session::ValidationCachedIssue& issue : entry.issues) {
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
    if (!state_.ignoreMuriIssuePrompts_) {
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
    }

    const bool showError = errorCount > 0;
    const bool showWarning = warningCount > 0;
    const bool showMuri = muriIssueCount > 0;

    const QString summaryTooltip = showMuri
        ? UiText::text(QStringLiteral("editor.validation_summary.tooltip_with_muri"))
              .arg(errorCount).arg(warningCount).arg(muriIssueCount)
        : UiText::text(QStringLiteral("editor.validation_summary.tooltip"))
              .arg(errorCount).arg(warningCount);
    const QString jumpHint = UiText::text(QStringLiteral("validation.click_an_icon_to_jump"));
    const QString summaryTooltipWithJump = summaryTooltip + QStringLiteral("\n") + jumpHint;
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

    const auto applySlot = [showSummary, &summaryTooltipWithJump](QLabel* icon, QLabel* count, const SummaryEntry* entry) {
        const bool occupied = showSummary && entry != nullptr && entry->count > 0;
        // These three icon/count widget pairs are positional SLOTS, not
        // fixed-purpose badges: any kind (error/warning/muri) can land in any
        // pair depending on which kinds are present. Stamp the bottom tab the
        // shown badge should jump to so the header click handler routes by the
        // badge kind actually displayed, not by the widget's name. Red/yellow
        // (error/warning) → "validation" (语法); purple (muri) → "muri" (无理).
        const QString targetTab =
            (occupied && entry->kind == EditorValidationSummaryIconKind::Muri)
                ? QStringLiteral("muri")
                : QStringLiteral("validation");
        if (icon != nullptr) {
            if (occupied) {
                icon->setPixmap(makeEditorValidationSummaryIcon(entry->color, entry->kind));
            } else {
                icon->clear();
            }
            icon->setProperty("hasContent", occupied);
            icon->setProperty("validationSummaryTab", occupied ? targetTab : QString());
            icon->setVisible(occupied);
            icon->setToolTip(summaryTooltipWithJump);
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
            count->setProperty("validationSummaryTab", occupied ? targetTab : QString());
            count->setVisible(occupied);
            count->setToolTip(summaryTooltipWithJump);
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
    session_.updateEditorHeaderLayoutMode();
}

void miacode::runtime::ValidationHost::clearValidationErrors()
{
    if (ui_.errorList_ == nullptr) {
        return;
    }
    ui_.errorList_->clear();
}

void miacode::runtime::ValidationHost::clearMuriDiagnostics()
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

void miacode::runtime::ValidationHost::clearValidationDecorations()
{
    state_.validationDecorations_.clear();
}

void miacode::runtime::ValidationHost::addValidationError(
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

void miacode::runtime::ValidationHost::addValidationDecoration(int line, int col, const QString& message, int endCol)
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
    Session::ValidationDecoration decoration;
    decoration.line = qMax(1, line);
    decoration.col = qMax(1, col);
    decoration.endCol = qMax(decoration.col, endCol);
    decoration.message = message;
    decoration.warning = (parts.severity == ValidationSeverityLevel::Warning);
    state_.validationDecorations_.append(decoration);
}

void miacode::runtime::ValidationHost::jumpToLocation(int line, int col)
{
    session_.documents_->requestEditorNavigation(
        qMax(1, line), qMax(1, col), qMax(1, line), qMax(1, col),
        false, true, true);
}

void miacode::runtime::ValidationHost::onErrorItemActivated(QListWidgetItem* item)
{
    MC_OP("miacode::runtime::ValidationHost::onErrorItemActivated");
    if (item == nullptr) {
        return;
    }
    const int line = item->data(kIssueLineRole).toInt();
    const int col = item->data(kIssueColRole).toInt();
    _mc_op_.note(QStringLiteral("line=%1 col=%2").arg(line).arg(col));
    jumpToLocation(line, col);
}

void miacode::runtime::ValidationHost::onMuriItemActivated(QListWidgetItem* item)
{
    MC_OP("miacode::runtime::ValidationHost::onMuriItemActivated");
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

    const double clampedSecond = qBound(0.0, second, session_.previewDurationSeconds());
    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = true;
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    session_.seekPreviewToSecond(clampedSecond, true);
    state_.timelineQuickStateBridge_->setCursorSeconds(clampedSecond, false);
    state_.timelineQuickStateBridge_->focusPlayhead(true);
}

void Session::updateEditorValidationSummary()
{
    validation_->updateEditorValidationSummary();
}

void Session::clearPreviewFollowDecoration()
{
    if (editorSyncController_ != nullptr) {
        miacode::v2::EditorFollowState follow;
        follow.playbackActive = playing_;
        editorSyncController_->publishFollow(follow);
    }
}

void Session::clearValidationErrors()
{
    validation_->clearValidationErrors();
}

void Session::clearMuriDiagnostics()
{
    validation_->clearMuriDiagnostics();
}

void Session::clearValidationDecorations()
{
    validation_->clearValidationDecorations();
}

void Session::addValidationError(
    int line,
    int col,
    const QString& rawMessage,
    const QString& displayMessage,
    const QString& issueTypeKey,
    const QString& issueTypeLabel,
    bool ignoredInHeader)
{
    validation_->addValidationError(line, col, rawMessage, displayMessage, issueTypeKey, issueTypeLabel, ignoredInHeader);
}

void Session::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    validation_->addValidationDecoration(line, col, message, endCol);
}

void Session::jumpToLocation(int line, int col)
{
    validation_->jumpToLocation(line, col);
}

void Session::onErrorItemActivated(QListWidgetItem* item)
{
    validation_->onErrorItemActivated(item);
}

void Session::onMuriItemActivated(QListWidgetItem* item)
{
    validation_->onMuriItemActivated(item);
}
