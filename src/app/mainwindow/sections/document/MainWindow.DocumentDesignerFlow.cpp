#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
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
#include <QtWidgets>

using namespace miacode::mainwindow::shared;
#include "MainWindow.DocumentFlow.Internal.h"

using namespace miacode::mainwindow::documentflow_detail;

namespace {

constexpr const char* kUnifiedDesignerPrefKey = "unified_designer_enabled";

// Collect every distinct non-empty designer name across the top &des and
// each per-difficulty &des_N. Used by the OFF→ON popup flows so the user
// can pick which name to canonicalize, or so we know whether anything is
// at risk of being overwritten silently.
struct DesignerSurvey {
    QString topDesigner;
    QVector<QPair<int, QString>> perDifficulty;  // (id, designer)
    QStringList distinctNonEmpty;                // de-duped, preserves insert order
};

DesignerSurvey surveyDesigners(const SimaiDocument& doc)
{
    DesignerSurvey survey;
    survey.topDesigner = doc.designer;
    if (!doc.designer.isEmpty()) {
        survey.distinctNonEmpty.append(doc.designer);
    }
    // Includes chart-less standalone &des_N so the unify picker can offer (and
    // overwrite) names that belong to slots without a chart.
    survey.perDifficulty = doc.perDifficultyDesigners();
    for (const QPair<int, QString>& slot : survey.perDifficulty) {
        if (!slot.second.isEmpty() && !survey.distinctNonEmpty.contains(slot.second)) {
            survey.distinctNonEmpty.append(slot.second);
        }
    }
    return survey;
}

void writeUnifiedDesignerPreference(const QString& chartPath, bool enabled)
{
    if (chartPath.isEmpty()) {
        return;
    }
    QJsonObject prefs = miacode::project_preferences::load(chartPath);
    prefs[QLatin1String(kUnifiedDesignerPrefKey)] = enabled;
    miacode::project_preferences::save(chartPath, prefs);
}

}  // namespace

void MainWindow::DocumentSection::refreshUnifiedDesignerStateForLoadedDocument()
{
    bool enabled = false;
    const QString chartPath = state_.currentFilePath_;
    bool decisionFromPreference = false;
    if (!chartPath.isEmpty()) {
        const QJsonObject prefs = miacode::project_preferences::load(chartPath);
        const QJsonValue stored = prefs.value(QLatin1String(kUnifiedDesignerPrefKey));
        if (stored.isBool()) {
            enabled = stored.toBool();
            decisionFromPreference = true;
        }
    }
    if (!decisionFromPreference) {
        enabled = owner_.applicationServices_.workspace().document().inferUnifiedDesignerDefault();
    }
    state_.unifiedDesignerEnabled_ = enabled;

    // The preference claims "all difficulties share one designer", but the file
    // we just loaded may disagree (it could have been hand-edited, merged, or
    // touched by another tool since unified mode was last on). Without this,
    // the box would read as "synced" while &des and the &des_N silently
    // diverge until the next manual designer edit broadcasts. Reconcile now so
    // the on-screen promise holds the moment the document opens.
    if (!enabled) {
        return;
    }
    const DesignerSurvey survey = surveyDesigners(owner_.applicationServices_.workspace().document());
    // Only auto-reconcile when there's an unambiguous canonical name:
    //   - 0/1 distinct non-empty name → fill blanks, no data loss; or
    //   - &des is non-empty → it is the declared source of truth.
    // When &des is empty AND the per-difficulty names genuinely conflict
    // there is no winner to pick without asking, and silently guessing on
    // load would destroy data with no undo — so leave it for the user.
    if (survey.distinctNonEmpty.size() >= 2 && survey.topDesigner.isEmpty()) {
        return;
    }
    QString canonical = survey.topDesigner;
    if (canonical.isEmpty() && !survey.distinctNonEmpty.isEmpty()) {
        canonical = survey.distinctNonEmpty.first();
    }
    // applyUnifiedDesignerName is a no-op (no dirty, no UI churn) when the
    // document is already consistent, which is the common case.
    applyUnifiedDesignerName(canonical);
}

void MainWindow::DocumentSection::enableUnifiedDocumentDesigner(const QString& canonicalName)
{
    state_.unifiedDesignerEnabled_ = true;
    writeUnifiedDesignerPreference(state_.currentFilePath_, true);
    applyUnifiedDesignerName(canonicalName);
}

void MainWindow::DocumentSection::disableUnifiedDocumentDesigner()
{
    if (!state_.unifiedDesignerEnabled_) {
        return;
    }
    state_.unifiedDesignerEnabled_ = false;
    writeUnifiedDesignerPreference(state_.currentFilePath_, false);
}

void MainWindow::DocumentSection::openPerDifficultyDesignerDialog()
{
}


void MainWindow::DocumentSection::applyUnifiedDesignerName(const QString& canonicalName)
{
    const bool changed = owner_.applicationServices_.workspace().unifyDesigners(canonicalName);
    if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != canonicalName) {
        QSignalBlocker block(ui_.designerEdit_);
        ui_.designerEdit_->setText(canonicalName);
    }
    syncHeaderDesignerEditFromModel();
    if (changed) {
        state_.documentDirty_ = owner_.applicationServices_.workspace().snapshot().dirty;
        anchorCurrentFieldCleanState();
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        owner_.updateWindowTitle();
        rebuildFieldSidebar();
    }
}

bool MainWindow::DocumentSection::promptCanonicalDesignerName(const QStringList& candidates, QString* out)
{
    Q_UNUSED(candidates);
    if (out != nullptr) {
        *out = QString();
    }
    return false;
}

