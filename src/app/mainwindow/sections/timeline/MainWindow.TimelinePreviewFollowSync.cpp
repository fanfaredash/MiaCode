#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"
#include "../dialogs/MainWindow.DialogsSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "../validation/EditorSelectionUtils.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencySandboxController.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include "MainWindow.PreviewTimelineFlow.Internal.h"

using namespace miacode::mainwindow::shared;
using namespace miacode::mainwindow::preview_timeline_flow_detail;

double MainWindow::TimelineSection::touchPadAuthoringAnchoredSecond(double previewSecond) const
{
    if (state_.touchPadAuthoringAnchorSeekSecond_ < 0.0) {
        return previewSecond;
    }
    return qAbs(previewSecond - state_.touchPadAuthoringAnchorSeekSecond_)
            <= miacode::preview_interaction::kTouchPadAuthoringAnchorToleranceSeconds
        ? state_.touchPadAuthoringAnchorTokenSecond_
        : previewSecond;
}

void MainWindow::TimelineSection::setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond)
{
    state_.touchPadAuthoringAnchorSeekSecond_ = seekSecond;
    state_.touchPadAuthoringAnchorTokenSecond_ = tokenSecond;
}

void MainWindow::TimelineSection::updatePreviewFollowDecorationForTimelineBlueLine(
    double second,
    bool ensureVisible,
    qint64* resolveElapsedNs,
    qint64* followOverlayElapsedNs,
    TimelineQuickModel::PreviewFollowSpan* spanOut)
{
    if (resolveElapsedNs != nullptr) {
        *resolveElapsedNs = 0;
    }
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = 0;
    }
    if (spanOut != nullptr) {
        *spanOut = TimelineQuickModel::PreviewFollowSpan();
    }

    // NOT gated on previewFollowEnabled_: that option governs whether the
    // preview MOVES the caret and viewport, not whether the playhead's token is
    // visible. The highlight is a read-only indicator of where the playhead is,
    // useful on its own, so it stays on with 代码跟随 off. Nothing authors
    // against it — touch-pad click input targets the caret.
    if (!hasActiveDifficulty()) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        owner_.clearPreviewFollowDecoration();
        invalidatePreviewFollowBindingCache();
        if (followOverlayElapsedNs != nullptr) {
            *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
        }
        return;
    }
    second = touchPadAuthoringAnchoredSecond(second);

    TimelineQuickModel::PreviewFollowBinding binding;
    bool resolved = false;
    if (cachedPreviewFollowBindingContainsSecond(second)) {
        binding = state_.previewFollowBindingCache_;
    } else {
        QElapsedTimer resolveTimer;
        resolveTimer.start();
        resolved = state_.timelineQuickModel_.resolvePreviewFollowBinding(qMax(0.0, second), &binding);
        if (resolveElapsedNs != nullptr) {
            *resolveElapsedNs = resolveTimer.nsecsElapsed();
        }
        if (resolved) {
            cachePreviewFollowBinding(binding);
        } else {
            invalidatePreviewFollowBindingCache();
        }
    }
    if (!resolved) {
        resolved = binding.resolved;
    }
    if (!resolved) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        owner_.clearPreviewFollowDecoration();
        if (followOverlayElapsedNs != nullptr) {
            *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
        }
        return;
    }
    if (spanOut != nullptr) {
        *spanOut = binding.span;
    }
    QElapsedTimer overlayTimer;
    overlayTimer.start();
    owner_.setPreviewFollowDecoration(
        binding.span.startLine,
        binding.span.startCol,
        binding.span.endLine,
        binding.span.endCol,
        binding.span.cursorLine,
        binding.span.cursorCol,
        ensureVisible);
    if (followOverlayElapsedNs != nullptr) {
        *followOverlayElapsedNs = overlayTimer.nsecsElapsed();
    }
}

void MainWindow::TimelineSection::syncEditorCursorToPreviewSecond(
    double second,
    bool centerView,
    bool ensureVisibleWhenPaused)
{
    QElapsedTimer timer;
    timer.start();
    const auto logPerf =
        [&](const QString& action,
            bool resolved,
            bool moved,
            const TimelineQuickModel::PreviewFollowSpan* span,
            qint64 resolveElapsedNs,
            qint64 cursorMoveElapsedNs,
            qint64 followOverlayElapsedNs,
            qint64 timelineCursorElapsedNs) {
        if (!state_.runtimeDebugOutputEnabled_) {
            return;
        }
        const qint64 totalElapsedNs = timer.nsecsElapsed();
        const bool hotAction = action == QStringLiteral("selection_end_unchanged")
            || action == QStringLiteral("anchor_unchanged")
            || action == QStringLiteral("binding_unchanged")
            || action == QStringLiteral("cursor_moved")
            || action == QStringLiteral("paused_decoration");
        constexpr qint64 kFollowPerfLogThresholdNs = 4 * 1000 * 1000;
        if (hotAction
            && totalElapsedNs < kFollowPerfLogThresholdNs
            && resolveElapsedNs < kFollowPerfLogThresholdNs
            && cursorMoveElapsedNs < kFollowPerfLogThresholdNs
            && followOverlayElapsedNs < kFollowPerfLogThresholdNs
            && timelineCursorElapsedNs < kFollowPerfLogThresholdNs) {
            return;
        }
        const int line = (span != nullptr) ? span->cursorLine : 1;
        const int col = (span != nullptr) ? span->cursorCol : 1;
        const int endCol = (span != nullptr) ? span->endCol : 1;
        const int startLine = (span != nullptr) ? span->startLine : 1;
        const int startCol = (span != nullptr) ? span->startCol : 1;
        const int endLine = (span != nullptr) ? span->endLine : 1;
        const double totalElapsedMs = totalElapsedNs / 1000000.0;
        appendTimelinePerfLog(
            QStringLiteral("edit/follow_sync_perf"),
            QStringLiteral(
                "action=%1 resolved=%2 moved=%3 second=%4 line=%5 col=%6 end_col=%7 start_line=%8 start_col=%9 end_line=%10 end_col=%11 center=%12 ensure_visible=%13 playing=%14 elapsed_ms=%15"
            )
                .arg(action)
                .arg(resolved ? 1 : 0)
                .arg(moved ? 1 : 0)
                .arg(second, 0, 'f', 6)
                .arg(line)
                .arg(col)
                .arg(endCol)
                .arg(startLine)
                .arg(startCol)
                .arg(endLine)
                .arg(endCol)
                .arg(centerView ? 1 : 0)
                .arg(ensureVisibleWhenPaused ? 1 : 0)
                .arg(state_.qtPreviewPlaying_ ? 1 : 0)
                .arg(totalElapsedMs, 0, 'f', 3)
        );
        appendTimelinePerfLog(
            QStringLiteral("edit/follow_sync_breakdown"),
            QStringLiteral(
                "action=%1 resolved=%2 moved=%3 center=%4 playing=%5 resolve_ms=%6 cursor_move_ms=%7 follow_overlay_ms=%8 timeline_cursor_ms=%9 total_ms=%10"
            )
                .arg(action)
                .arg(resolved ? 1 : 0)
                .arg(moved ? 1 : 0)
                .arg(centerView ? 1 : 0)
                .arg(state_.qtPreviewPlaying_ ? 1 : 0)
                .arg(resolveElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(cursorMoveElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(followOverlayElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(timelineCursorElapsedNs / 1000000.0, 0, 'f', 3)
                .arg(totalElapsedMs, 0, 'f', 3)
        );
    };

    qint64 resolveElapsedNs = 0;
    qint64 cursorMoveElapsedNs = 0;
    qint64 followOverlayElapsedNs = 0;
    TimelineQuickModel::PreviewFollowSpan span;

    if (state_.suppressTimelineCursorSync_ || !hasActiveDifficulty()) {
        owner_.clearPreviewFollowDecoration();
        if (!hasActiveDifficulty()) {
            invalidatePreviewFollowBindingCache();
        }
        logPerf(QStringLiteral("suppressed"), false, false, nullptr, 0, 0, 0, 0);
        return;
    }
    // 代码跟随 off still draws the highlight — it just never moves the caret or
    // scrolls, which is what the option is about. Same decoration-only path the
    // paused branch uses, minus the ensure-visible scroll.
    if (!state_.previewFollowEnabled_ || !state_.qtPreviewPlaying_) {
        updatePreviewFollowDecorationForTimelineBlueLine(
            second,
            state_.previewFollowEnabled_ && ensureVisibleWhenPaused,
            &resolveElapsedNs,
            &followOverlayElapsedNs,
            &span);
        logPerf(
            state_.previewFollowEnabled_
                ? QStringLiteral("paused_decoration")
                : QStringLiteral("disabled_decoration"),
            state_.previewFollowDecorationActive_,
            false,
            state_.previewFollowDecorationActive_ ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (cachedPreviewFollowBindingContainsSecond(second)) {
        span = state_.previewFollowBindingCache_.span;
        logPerf(
            QStringLiteral("binding_unchanged"),
            true,
            false,
            &span,
            0,
            0,
            0,
            0);
        return;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor currentEditorCursor =
        (editor != nullptr && editor->document() != nullptr) ? editor->textCursor() : QTextCursor();
    TimelineQuickModel::PreviewFollowBinding binding;
    QElapsedTimer resolveTimer;
    resolveTimer.start();
    const bool resolved = state_.timelineQuickModel_.resolvePreviewFollowBinding(second, &binding);
    resolveElapsedNs = resolveTimer.nsecsElapsed();
    if (resolved) {
        cachePreviewFollowBinding(binding);
        span = binding.span;
    } else {
        invalidatePreviewFollowBindingCache();
    }
    const int targetLine = resolved ? binding.span.cursorLine : 1;
    const int targetCol = resolved ? binding.span.cursorCol : 1;
    bool alreadyAtSelectionEnd = false;
    if (editor != nullptr && editor->document() != nullptr) {
        if (resolved) {
            if (binding.span.hasVisibleBody) {
                alreadyAtSelectionEnd = currentEditorCursor.hasSelection()
                    && currentEditorCursor.position() == binding.span.cursorPosition
                    && currentEditorCursor.selectionStart() == binding.span.startPosition
                    && currentEditorCursor.selectionEnd() == binding.span.endPositionExclusive;
            } else {
                alreadyAtSelectionEnd = !currentEditorCursor.hasSelection()
                    && currentEditorCursor.position() == binding.span.cursorPosition;
            }
        } else {
            alreadyAtSelectionEnd = !currentEditorCursor.hasSelection() && currentEditorCursor.position() == 0;
        }
    }

    const auto applyFollowOverlay = [&](bool followResolved) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        if (followResolved) {
            owner_.setPreviewFollowDecoration(
                binding.span.startLine,
                binding.span.startCol,
                binding.span.endLine,
                binding.span.endCol,
                binding.span.cursorLine,
                binding.span.cursorCol);
        } else {
            owner_.clearPreviewFollowDecoration();
        }
        return overlayTimer.nsecsElapsed();
    };

    if (state_.qtPreviewPlaying_ && !centerView) {
        followOverlayElapsedNs = applyFollowOverlay(resolved);
        logPerf(
            QStringLiteral("visual_follow_updated"),
            resolved,
            false,
            resolved ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (alreadyAtSelectionEnd) {
        followOverlayElapsedNs = applyFollowOverlay(resolved);
        logPerf(
            QStringLiteral("selection_end_unchanged"),
            resolved,
            false,
            resolved ? &span : nullptr,
            resolveElapsedNs,
            0,
            followOverlayElapsedNs,
            0);
        return;
    }

    if (editor != nullptr && editor->document() != nullptr) {
        QTextCursor cursor;
        const bool builtCursor = resolved && binding.span.hasVisibleBody
            ? miacode::mainwindow::editor_selection::buildSelectionCursor(
                  editor,
                  binding.span.startLine,
                  binding.span.startCol,
                  binding.span.endLine,
                  binding.span.endCol,
                  &cursor)
            : miacode::mainwindow::editor_selection::buildCaretCursor(editor, targetLine, targetCol, &cursor);
        if (builtCursor) {
            QElapsedTimer cursorTimer;
            cursorTimer.start();
            editor->applyPreviewFollowCursor(cursor, centerView, true);
            cursorMoveElapsedNs = cursorTimer.nsecsElapsed();
            followOverlayElapsedNs = applyFollowOverlay(resolved);
            logPerf(
                QStringLiteral("cursor_moved"),
                resolved,
                true,
                resolved ? &span : nullptr,
                resolveElapsedNs,
                cursorMoveElapsedNs,
                followOverlayElapsedNs,
                0);
            return;
        }
    }
    logPerf(
        QStringLiteral("cursor_move_failed"),
        resolved,
        false,
        resolved ? &span : nullptr,
        resolveElapsedNs,
        cursorMoveElapsedNs,
        followOverlayElapsedNs,
        0);
}
