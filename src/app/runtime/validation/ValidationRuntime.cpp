#include "runtime/validation/ValidationHost.h"
#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/shell/ShellHost.h"

#include "SimaiNativeParser.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"

#include <QtCore>

namespace {

using miacode::muri::MuriPanelEntry;

enum class ValidationSeverityLevel {
    Error,
    Warning,
};

struct ValidationMessageParts {
    QString severityPrefix;
    QString body;
    ValidationSeverityLevel severity = ValidationSeverityLevel::Error;
};

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

}  // namespace

void miacode::runtime::ValidationHost::clearValidationCache()
{
    state_.validationCacheByDifficulty_.clear();
    emit session_.documentValidationChanged();
}

void miacode::runtime::ValidationHost::notifyDocumentValidationChanged()
{
    emit session_.documentValidationChanged();
}

void miacode::runtime::ValidationHost::applyDeferredAnalysisUiUpdates()
{
    if (state_.playing_) {
        return;
    }

    if (state_.pendingDeferredMuriUiRefresh_) {
        if (state_.scene_ != nullptr) {
            state_.scene_->setMuriRenderOptions(state_.muriRenderOptions_);
        }
        session_.applyAlignedMuriAnalysisReportToViews();
        state_.pendingDeferredMuriUiRefresh_ = false;
    }

    if (state_.pendingDeferredValidationUiRefresh_) {
        refreshValidationPanelForActiveField();
        state_.pendingDeferredValidationUiRefresh_ = false;
    }
}

void miacode::runtime::ValidationHost::refreshValidationPanelForActiveField()
{
    if (!session_.hasActiveDifficulty()) {
        session_.setBottomTabsTabVisible(Session::BottomTabsTabId::Validation, false);
        clearValidationDecorations();
        return;
    }

    session_.setBottomTabsTabVisible(Session::BottomTabsTabId::Validation, true);
    const int difficultyId = session_.activeDifficultyId();
    const auto it = state_.validationCacheByDifficulty_.constFind(difficultyId);
    if (it == state_.validationCacheByDifficulty_.constEnd()) {
        clearValidationDecorations();
        return;
    }

    const QString chartText = session_.activeChartText();
    const SimaiNativeValidationLocale validationLocale = miacode::v2::uiValidationLocale();
    const miacode::simai::SimaiTimingMetadata timingMetadata = session_.currentTimingMetadata();
    const Session::ValidationCacheEntry& entry = it.value();
    if (entry.chartText != chartText
        || entry.validationLocale != validationLocale
        || entry.timingMetadata != timingMetadata) {
        clearValidationDecorations();
        return;
    }

    state_.validationDecorations_.clear();
    for (const Session::ValidationCachedIssue& issue : entry.issues) {
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
}

Session::DocumentValidationSnapshot
miacode::runtime::ValidationHost::documentValidationSnapshot() const
{
    const miacode::simai::SimaiTimingMetadata timingMetadata = session_.currentTimingMetadata();
    miacode::qml_ui::DocumentValidationProjectionInput input;
    input.difficultyId = session_.hasActiveDifficulty() ? session_.activeDifficultyId() : 0;
    input.chartTextSignature = session_.activeChartText();
    input.timingSignature = QStringLiteral("%1|%2|%3|%4")
        .arg(timingMetadata.wholeTimeSignatureText)
        .arg(timingMetadata.wholeTimeSignatureNumerator)
        .arg(timingMetadata.wholeTimeSignatureDenominator)
        .arg(timingMetadata.wholeTimeSignatureValid ? 1 : 0);
    input.timelineRevision = state_.timelineRevision_;

    miacode::qml_ui::DocumentValidationProjectionCache cache;
    const auto it = state_.validationCacheByDifficulty_.constFind(input.difficultyId);
    if (it != state_.validationCacheByDifficulty_.constEnd()
        && it->validationLocale == miacode::v2::uiValidationLocale()) {
        const Session::ValidationCacheEntry& entry = it.value();
        cache.difficultyId = input.difficultyId;
        cache.chartTextSignature = entry.chartText;
        cache.timingSignature = QStringLiteral("%1|%2|%3|%4")
            .arg(entry.timingMetadata.wholeTimeSignatureText)
            .arg(entry.timingMetadata.wholeTimeSignatureNumerator)
            .arg(entry.timingMetadata.wholeTimeSignatureDenominator)
            .arg(entry.timingMetadata.wholeTimeSignatureValid ? 1 : 0);
        cache.validationRevision = entry.validationRevision;
        cache.ok = entry.ok;
        cache.errorCount = entry.errorCount;
        cache.warningCount = entry.warningCount;
        cache.parsedNoteCount = entry.strictNoteCount;
        cache.issues.reserve(entry.issues.size());
        for (const Session::ValidationCachedIssue& cachedIssue : entry.issues) {
            cache.issues.append({
                cachedIssue.line,
                cachedIssue.col,
                cachedIssue.endCol,
                cachedIssue.severity == SimaiNativeValidationSeverity::Warning
                    ? miacode::qml_ui::DocumentValidationIssueSeverity::Warning
                    : miacode::qml_ui::DocumentValidationIssueSeverity::Error,
                cachedIssue.displayMessage,
            });
        }
    }
    return miacode::qml_ui::projectDocumentValidation(input, cache);
}

Session::QmlAnalysisSnapshot miacode::runtime::ValidationHost::qmlAnalysisSnapshot() const
{
    miacode::qml_ui::AnalysisProjectionInput input;
    input.validation = documentValidationSnapshot();
    input.activeDifficultyId = session_.hasActiveDifficulty() ? session_.activeDifficultyId() : 0;
    input.muriDifficultyId = state_.muriAnalysisReportDifficultyId_;
    input.muriRevision = state_.muriAnalysisReportTimelineRevision_;
    input.muriSignatureAligned = state_.muriAnalysisResultAvailable_
        && !state_.muriAnalysisReportNoteMarkerSignature_.isEmpty()
        && state_.latestTimelineNoteMarkerSignature_ == state_.muriAnalysisReportNoteMarkerSignature_;
    input.muriStaticReferencesAligned = state_.muriStaticReferencesAvailable_
        && state_.muriStaticReferencesDifficultyId_ == input.muriDifficultyId
        && state_.muriStaticReferencesTimelineRevision_ == input.muriRevision
        && state_.muriStaticReferencesNoteMarkerSignature_ == state_.muriAnalysisReportNoteMarkerSignature_;
    if (input.muriSignatureAligned) {
        const auto entries = miacode::muri::buildVisibleMuriPanelEntries(
            state_.muriAnalysisReport_, state_.muriStaticReferences_);
        input.muriRows.reserve(entries.size());
        for (const MuriPanelEntry& entry : entries) {
            miacode::qml_ui::AnalysisRow row;
            row.line = qMax(1, entry.line);
            row.column = qMax(1, entry.col);
            row.endColumn = row.column;
            row.second = entry.second;
            row.severity = entry.alertLevel == MuriAlertLevel::Warning
                ? QStringLiteral("warning") : QStringLiteral("error");
            row.alert = entry.alertLevel == MuriAlertLevel::Warning
                ? QStringLiteral("warning") : QStringLiteral("muri");
            switch (entry.kind) {
            case MuriKind::SlideTooFast: row.title = QStringLiteral("Slide too fast"); break;
            case MuriKind::SlideHeadTap: row.title = QStringLiteral("Slide head tap"); break;
            case MuriKind::TapOnSlide: row.title = QStringLiteral("Tap on slide"); break;
            case MuriKind::Overlap: row.title = QStringLiteral("Overlap"); break;
            case MuriKind::MultiTouch: row.title = QStringLiteral("Multi-touch"); break;
            }
            row.detail = renderMuriDetail(entry.detailKind, entry.detailArgs, miacode::v2::uiValidationLocale()).trimmed();
            if (row.detail.isEmpty()) row.detail = entry.rawDetail;
            input.muriRows.append(row);
        }
    }
    return miacode::qml_ui::projectAnalysis(input);
}

bool miacode::runtime::ValidationHost::runValidateSimaiSilently()
{
    if (!session_.hasActiveDifficulty()) {
        refreshValidationPanelForActiveField();
        emit session_.documentValidationChanged();
        return false;
    }

    const int difficultyId = session_.activeDifficultyId();
    const QString chartText = session_.activeChartText();
    const SimaiNativeValidationLocale validationLocale = miacode::v2::uiValidationLocale();
    const miacode::simai::SimaiTimingMetadata timingMetadata = session_.currentTimingMetadata();
    const SimaiNativeParseResult* cachedLenientResult =
        (state_.lastTimelineParseDifficultyId_ == difficultyId
            && state_.lastTimelineParseChartText_ == chartText
            && state_.lastTimelineParseTimingMetadata_ == timingMetadata)
        ? &state_.lastTimelineParseResult_
        : nullptr;

    Session::ValidationCacheEntry entry;
    const auto cacheIt = state_.validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != state_.validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->validationLocale == validationLocale
        && cacheIt->timingMetadata == timingMetadata
        && cacheIt->validationRevision == state_.timelineRevision_) {
        entry = cacheIt.value();
    } else {
        QElapsedTimer reportTimer;
        reportTimer.start();
        const SimaiNativeValidationReport report =
            SimaiNativeParser::buildValidationReport(chartText, validationLocale, cachedLenientResult, timingMetadata);
        entry.chartText = chartText;
        entry.validationLocale = validationLocale;
        entry.timingMetadata = timingMetadata;
        entry.validationRevision = state_.timelineRevision_;
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
            Session::ValidationCachedIssue cachedIssue;
            cachedIssue.line = issue.line;
            cachedIssue.col = issue.col;
            cachedIssue.endCol = issue.endCol;
            cachedIssue.severity = issue.severity;
            cachedIssue.rawMessage = issue.rawMessage;
            cachedIssue.displayMessage = issue.displayMessage;
            entry.issues.append(cachedIssue);
        }
        state_.validationCacheByDifficulty_.insert(difficultyId, entry);
        if (state_.runtimeDebugOutputEnabled_) {
            session_.shell_->appendOutput(
                "edit/validation_perf",
                QStringLiteral("build_report=%1ms issues=%2 cached_lenient=%3")
                    .arg(reportTimer.elapsed())
                    .arg(entry.issues.size())
                    .arg(cachedLenientResult != nullptr ? QStringLiteral("1") : QStringLiteral("0"))
            );
        }
    }

    session_.setBottomTabsTabVisible(Session::BottomTabsTabId::Validation, true);
    state_.validationDecorations_.clear();
    for (const Session::ValidationCachedIssue& issue : entry.issues) {
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    if (state_.runtimeDebugOutputEnabled_) {
        session_.shell_->appendOutput(
            "edit/validation_apply_perf",
            QStringLiteral("apply_state issues=%1")
                .arg(entry.issues.size())
        );
    }
    emit session_.documentValidationChanged();
    return entry.ok;
}

void Session::clearValidationCache()
{
    validation_->clearValidationCache();
}

void Session::applyDeferredAnalysisUiUpdates()
{
    validation_->applyDeferredAnalysisUiUpdates();
}

void Session::refreshValidationPanelForActiveField()
{
    validation_->refreshValidationPanelForActiveField();
}

bool Session::runValidateSimaiSilently()
{
    return validation_->runValidateSimaiSilently();
}

Session::DocumentValidationSnapshot Session::documentValidationSnapshot() const
{
    return validation_->documentValidationSnapshot();
}

Session::QmlAnalysisSnapshot Session::qmlAnalysisSnapshot() const
{
    return validation_->qmlAnalysisSnapshot();
}

bool Session::validateActiveDocument()
{
    // UIv2's explicit recheck must not publish a validation-only generation:
    // advance the shared slow-refresh revision first, then let its analysis
    // worker publish matching validation, Muri report and static references.
    // The synchronous validation below remains useful for immediate feedback
    // while that aligned Muri result is pending.
    playback_->scheduleTimelineRefresh();
    return validation_->runValidateSimaiSilently();
}
