#include "runtime/document/DocumentSessionHost.h"

#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "core/chart/document/SimaiDocument.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>

namespace {

// Per-project sidecar key, unchanged from v1 so a project that was already
// working in unified mode is recognized by this build.
constexpr const char* kUnifiedDesignerPrefKey = "unified_designer_enabled";

}  // namespace

bool miacode::runtime::DocumentSessionHost::applyDocumentDesignerSlots(
    const QVector<QPair<int, QString>>& slotValues, bool unified, const QString& canonicalName)
{
    MC_OP("miacode::runtime::DocumentSessionHost::applyDocumentDesignerSlots");
    _mc_op_.note(QStringLiteral("slots=%1 unified=%2").arg(slotValues.size()).arg(unified ? 1 : 0));
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    if (!workspace.snapshot().hasDocument) {
        return false;
    }
    const bool modeWas = workspace.unifiedDesignerEnabled();
    // The per-slot rows are written with the mode OFF on purpose: while it is
    // on, every designer write is a broadcast, so seven rows would collapse
    // into whichever one happened to be written last.
    if (modeWas) {
        workspace.setUnifiedDesignerEnabled(false);
    }
    bool documentChanged = false;
    for (const QPair<int, QString>& slot : slotValues) {
        documentChanged = workspace.setDesignerForSlot(slot.first, slot.second) || documentChanged;
    }
    if (unified) {
        workspace.setUnifiedDesignerEnabled(true);
        documentChanged = workspace.unifyDesigners(canonicalName) || documentChanged;
    }
    writeUnifiedDesignerPreference(unified);
    if (documentChanged) {
        refreshAfterDesignerTransaction();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/unified_designer"),
        QStringLiteral("action=apply_slots unified=%1 changed=%2 slots=%3")
            .arg(unified ? 1 : 0)
            .arg(documentChanged ? 1 : 0)
            .arg(slotValues.size()));
    return documentChanged || modeWas != unified;
}

// Everything from here down is the load / self-heal half of the flow: it may
// move the mode and the stored preference, and must never write a document
// field — that is what keeps a freshly opened chart clean. The one document
// writer (applyDocumentDesignerSlots, above) stays above this line, which is
// also how QmlDocumentLifecycleContractSpec states the rule.
void miacode::runtime::DocumentSessionHost::reconcileUnifiedDocumentDesigner(
    miacode::v2::DocumentBridge::UnifiedDesignerReconcileReason reason)
{
    if (reason == miacode::v2::DocumentBridge::UnifiedDesignerReconcileReason::SourceReplaced) {
        demoteUnifiedDesignerAfterSourceReplacement();
        return;
    }
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    // A pending choice belonged to the document that just went away.
    state_.pendingUnifiedDesignerPreferenceValid_ = false;
    workspace.setUnifiedDesignerEnabled(false);
    const QString chartPath = workspace.snapshot().filePath;
    if (chartPath.isEmpty() || !workspace.snapshot().hasDocument) {
        return;
    }
    const QJsonObject preferences = miacode::project_preferences::load(chartPath);
    const QJsonValue stored = preferences.value(QLatin1String(kUnifiedDesignerPrefKey));
    // A project that never used the mode gets no sidecar written for it.
    if (!stored.isBool() || !stored.toBool()) {
        return;
    }
    if (workspace.document().isUnifiedDesignerTriviallySafe()) {
        workspace.setUnifiedDesignerEnabled(true);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("editor/unified_designer"),
            QStringLiteral("action=restore_from_preference path=%1").arg(chartPath));
        return;
    }
    // The file disagrees with the stored intent — hand-edited, merged, or
    // written by another tool since. The document is the truth: lower the
    // PREFERENCE instead, silently, so the chart that was just opened is byte
    // for byte what is on disk and the window opens clean. Rewriting the
    // document here is what made v1 report unsaved changes at startup.
    QJsonObject next = preferences;
    next[QLatin1String(kUnifiedDesignerPrefKey)] = false;
    miacode::project_preferences::save(chartPath, next);
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/unified_designer"),
        QStringLiteral("action=demote_preference reason=document_diverged path=%1").arg(chartPath));
}

void miacode::runtime::DocumentSessionHost::demoteUnifiedDesignerAfterSourceReplacement()
{
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    if (!workspace.unifiedDesignerEnabled()
        || workspace.document().isUnifiedDesignerTriviallySafe()) {
        return;
    }
    // Whole-source editing is the one route that can write a divergent &des_N
    // behind the mode's back. What the user typed wins; the mode steps down
    // rather than broadcasting over it.
    workspace.setUnifiedDesignerEnabled(false);
    writeUnifiedDesignerPreference(false);
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/unified_designer"),
        QStringLiteral("action=demote_preference reason=source_replaced"));
}

void miacode::runtime::DocumentSessionHost::writeUnifiedDesignerPreference(bool enabled)
{
    const QString chartPath = session_.applicationServices_.workspace().snapshot().filePath;
    if (chartPath.isEmpty()) {
        // An untitled document has no project directory to record this in yet.
        // flushPendingUnifiedDesignerPreference() writes it once one exists.
        state_.pendingUnifiedDesignerPreferenceValid_ = true;
        state_.pendingUnifiedDesignerPreference_ = enabled;
        return;
    }
    QJsonObject preferences = miacode::project_preferences::load(chartPath);
    const QJsonValue stored = preferences.value(QLatin1String(kUnifiedDesignerPrefKey));
    if (stored.isBool() && stored.toBool() == enabled) {
        return;
    }
    preferences[QLatin1String(kUnifiedDesignerPrefKey)] = enabled;
    miacode::project_preferences::save(chartPath, preferences);
}

void miacode::runtime::DocumentSessionHost::flushPendingUnifiedDesignerPreference()
{
    if (!state_.pendingUnifiedDesignerPreferenceValid_) {
        return;
    }
    const bool enabled = state_.pendingUnifiedDesignerPreference_;
    state_.pendingUnifiedDesignerPreferenceValid_ = false;
    writeUnifiedDesignerPreference(enabled);
}

void miacode::runtime::DocumentSessionHost::refreshAfterDesignerTransaction()
{
    state_.documentDirty_ = session_.applicationServices_.workspace().snapshot().dirty;
    anchorCurrentFieldCleanState();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    session_.updateWindowTitle();
}
