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
#include "common/ProcessDiagnostics.h"
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

void MainWindow::TimelineSection::applyTimelineQuickChange(int position, int charsRemoved, int charsAdded)
{
    if (state_.timelineQuickStateBridge_ == nullptr || !hasActiveDifficulty()) {
        return;
    }

    miacode::diag::MemoryStageScope memScope("preview/mem_stage", "timeline_quick_change");
    QElapsedTimer timer;
    timer.start();

    const double firstSeconds = parsedFirstSeconds();
    const miacode::simai::SimaiTimingMetadata timingMetadata = currentTimingMetadata();
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    QTextDocument* document = editor != nullptr ? editor->document() : nullptr;
    if (document != nullptr) {
        state_.timelineQuickModel_.applyContentsChange(
            document,
            position,
            charsRemoved,
            charsAdded,
            firstSeconds,
            timingMetadata);
    } else {
        state_.timelineQuickModel_.rebuildFromText(activeChartText(), firstSeconds, timingMetadata);
    }
    invalidatePreviewFollowBindingCache();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineData(state_.timelineQuickModel_.snapshot());
    }
    updatePreviewSliderRange();
    if (state_.runtimeDebugOutputEnabled_) {
        appendTimelinePerfLog(
            QStringLiteral("edit/quick_timeline_perf"),
            QStringLiteral("mode=incremental revision=%1 chars_removed=%2 chars_added=%3 lines=%4 elapsed_ms=%5")
                .arg(state_.timelineRevision_)
                .arg(charsRemoved)
                .arg(charsAdded)
                .arg(state_.timelineQuickModel_.snapshot().lines.size())
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
    }
}

void MainWindow::TimelineSection::refreshTimelineQuickModelFromCurrentText()
{
    if (state_.timelineQuickStateBridge_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    state_.timelineQuickModel_.rebuildFromText(activeChartText(), parsedFirstSeconds(), currentTimingMetadata());
    invalidatePreviewFollowBindingCache();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setTimelineData(state_.timelineQuickModel_.snapshot());
    }
    updatePreviewSliderRange();
    if (state_.runtimeDebugOutputEnabled_) {
        appendTimelinePerfLog(
            QStringLiteral("edit/quick_timeline_perf"),
            QStringLiteral("mode=full revision=%1 lines=%2 elapsed_ms=%3")
                .arg(state_.timelineRevision_)
                .arg(state_.timelineQuickModel_.snapshot().lines.size())
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
        );
    }
}
