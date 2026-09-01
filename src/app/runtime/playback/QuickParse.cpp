#include "runtime/playback/PlaybackHost.h"
#include "runtime/Shared.h"
#include "runtime/media/MediaJobsHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
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

#include "runtime/playback/TimelineFlow.Internal.h"

using namespace miacode::runtime::shared;
using namespace miacode::runtime::preview_timeline_detail;

void miacode::runtime::PlaybackHost::refreshTimelineQuickModelFromCurrentText()
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
