#include "runtime/document/DocumentSessionHost.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>

using namespace miacode::runtime::shared;
#include "runtime/document/DocumentFlow.Internal.h"

using namespace miacode::runtime::document_detail;

bool miacode::runtime::DocumentSessionHost::unifiedDocumentDesignerEnabled() const
{
    return state_.unifiedDesignerEnabled_;
}

void miacode::runtime::DocumentSessionHost::enableUnifiedDocumentDesigner(const QString& canonicalName)
{
    state_.unifiedDesignerEnabled_ = true;
    applyUnifiedDesignerName(canonicalName);
}

void miacode::runtime::DocumentSessionHost::disableUnifiedDocumentDesigner()
{
    if (!state_.unifiedDesignerEnabled_) {
        return;
    }
    state_.unifiedDesignerEnabled_ = false;
}

void miacode::runtime::DocumentSessionHost::applyUnifiedDesignerName(const QString& canonicalName)
{
    const bool changed = session_.applicationServices_.workspace().unifyDesigners(canonicalName);
    if (changed) {
        state_.documentDirty_ = session_.applicationServices_.workspace().snapshot().dirty;
        anchorCurrentFieldCleanState();
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        session_.updateWindowTitle();
    }
}

bool miacode::runtime::DocumentSessionHost::promptCanonicalDesignerName(const QStringList& candidates, QString* out)
{
    Q_UNUSED(candidates);
    if (out != nullptr) {
        *out = QString();
    }
    return false;
}
