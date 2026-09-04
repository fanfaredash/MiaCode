#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
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

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace miacode::runtime::shared;

namespace {

QString workspaceSwapPreviewPanelStyleSheet(bool swapped)
{
    QString style = UiTheme::previewPanelStyleSheet();
    if (swapped) {
        style.replace(QStringLiteral("border-left: 1px solid"), QStringLiteral("border-right: 1px solid"));
    }
    return style;
}

}  // namespace
