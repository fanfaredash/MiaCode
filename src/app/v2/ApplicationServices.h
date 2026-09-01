#pragma once

#include "AnalysisService.h"
#include "ChartDropImportService.h"
#include "ChartWorkspace.h"
#include "ChartWorkspaceFileService.h"
#include "EditorSyncController.h"
#include "JobProgressService.h"
#include "PreviewAppearanceState.h"
#include "UiRequestService.h"

#include <QObject>

namespace miacode::v2 {

// Only the slots live here; including ExportEngine.h would drag the whole
// VideoExportTask definition into every consumer of the assembly.
class ExportEngine;
class EditorPageRouter;
class MediaToolsEngine;
class LatencyEngine;
class TimelineSurface;
class PreviewSurface;
class PreferencesStore;

// The parser validation locale matching the session UI language.
//
// This used to live in MainWindowShared, a QtWidgets translation unit, so
// "which locale does the parser validate in" could not be answered without the
// widget layer. It reads nothing but the resolved UI language token, so it
// belongs here; MainWindowShared::uiValidationLocale() now forwards to it and
// stays as the name the widget-side call sites already use.
SimaiNativeValidationLocale uiValidationLocale();

// The application's service assembly: one non-Widget owner for the document
// domain and the shared UI boundaries.
//
// Before this existed the same services had two owners, both of them UI
// objects — MainWindow held UiRequestService, JobProgressService,
// EditorSyncController and ChartDropImportService, while QmlApplicationContext
// held ChartWorkspace, ChartWorkspaceFileService and AnalysisService. That made
// "who owns the document" depend on which of the two you asked, and it kept the
// hidden window on the critical path for services that never needed a window.
//
// Construction order is the contract: the workspace exists before anything that
// reads it, so the file service and the analysis service can bind to it by
// reference and there is never a second workspace to drift from.
//
// Deliberately Qt Widgets-free. `application_services_spec` links Qt6::Core and
// Qt6::Gui only, so a QtWidgets include reaching this assembly fails to link.
class ApplicationServices final : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationServices(QObject* parent = nullptr);

    ChartWorkspace& workspace() { return workspace_; }
    const ChartWorkspace& workspace() const { return workspace_; }

    ChartWorkspaceFileService& files() { return files_; }
    const ChartWorkspaceFileService& files() const { return files_; }

    AnalysisService& analysis() { return analysis_; }
    const AnalysisService& analysis() const { return analysis_; }

    EditorSyncController& editorSync() { return editorSync_; }
    const EditorSyncController& editorSync() const { return editorSync_; }

    ChartDropImportService& chartDropImport() { return chartDropImport_; }
    const ChartDropImportService& chartDropImport() const { return chartDropImport_; }

    UiRequestService& uiRequests() { return uiRequests_; }
    const UiRequestService& uiRequests() const { return uiRequests_; }

    JobProgressService& jobProgress() { return jobProgress_; }
    const JobProgressService& jobProgress() const { return jobProgress_; }

    PreviewAppearanceState& previewAppearance() { return previewAppearance_; }
    const PreviewAppearanceState& previewAppearance() const { return previewAppearance_; }

    // The export engine is the one service the assembly does not own yet: its
    // implementation is still a MainWindow section, 3,600 lines deep in the
    // widget layer. So the assembly holds the slot and the window installs
    // itself into it, then clears it before it starts tearing down.
    //
    // Consumers bind to the SLOT (`exportEngineSlot()`), not to a snapshot of
    // the pointer, so the clear is visible to them immediately rather than
    // leaving each holder with its own dangling copy.
    ExportEngine*& exportEngineSlot() { return exportEngine_; }
    ExportEngine* exportEngine() const { return exportEngine_; }
    void setExportEngine(ExportEngine* engine) { exportEngine_ = engine; }

    // Same arrangement, same reason: switching pages still needs the window's
    // save guard, playback teardown and title update, so the window installs
    // itself here and withdraws before it starts tearing down.
    EditorPageRouter*& editorPageRouterSlot() { return editorPageRouter_; }
    EditorPageRouter* editorPageRouter() const { return editorPageRouter_; }
    void setEditorPageRouter(EditorPageRouter* router) { editorPageRouter_ = router; }

    MediaToolsEngine*& mediaToolsEngineSlot() { return mediaToolsEngine_; }
    MediaToolsEngine* mediaToolsEngine() const { return mediaToolsEngine_; }
    void setMediaToolsEngine(MediaToolsEngine* engine) { mediaToolsEngine_ = engine; }

    LatencyEngine*& latencyEngineSlot() { return latencyEngine_; }
    LatencyEngine* latencyEngine() const { return latencyEngine_; }
    void setLatencyEngine(LatencyEngine* engine) { latencyEngine_ = engine; }

    TimelineSurface*& timelineSurfaceSlot() { return timelineSurface_; }
    TimelineSurface* timelineSurface() const { return timelineSurface_; }
    void setTimelineSurface(TimelineSurface* surface) { timelineSurface_ = surface; }

    PreviewSurface*& previewSurfaceSlot() { return previewSurface_; }
    PreviewSurface* previewSurface() const { return previewSurface_; }
    void setPreviewSurface(PreviewSurface* surface) { previewSurface_ = surface; }

    PreferencesStore*& preferencesStoreSlot() { return preferencesStore_; }
    PreferencesStore* preferencesStore() const { return preferencesStore_; }
    void setPreferencesStore(PreferencesStore* store) { preferencesStore_ = store; }

    // The single export page session. Typed QObject* because the session is a
    // QML-layer type this layer must not name; its two readers qobject_cast it.
    // Like the engine slots it is installed by whoever constructs it and
    // withdrawn before teardown.
    QObject*& exportPageSessionSlot() { return exportPageSession_; }
    QObject* exportPageSession() const { return exportPageSession_; }
    void setExportPageSession(QObject* session) { exportPageSession_ = session; }

    SimaiNativeValidationLocale validationLocale() const { return validationLocale_; }

private:
    // Declaration order is initialization order: workspace_ first, then
    // everything that binds to it.
    ChartWorkspace workspace_;
    ChartWorkspaceFileService files_;
    SimaiNativeValidationLocale validationLocale_;
    AnalysisService analysis_;
    EditorSyncController editorSync_;
    ChartDropImportService chartDropImport_;
    UiRequestService uiRequests_;
    JobProgressService jobProgress_;
    PreviewAppearanceState previewAppearance_;
    ExportEngine* exportEngine_ = nullptr;
    EditorPageRouter* editorPageRouter_ = nullptr;
    MediaToolsEngine* mediaToolsEngine_ = nullptr;
    LatencyEngine* latencyEngine_ = nullptr;
    TimelineSurface* timelineSurface_ = nullptr;
    PreviewSurface* previewSurface_ = nullptr;
    PreferencesStore* preferencesStore_ = nullptr;
    QObject* exportPageSession_ = nullptr;
};

}  // namespace miacode::v2
