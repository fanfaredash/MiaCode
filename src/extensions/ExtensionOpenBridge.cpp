#include "ExtensionOpenBridge.h"

#include <utility>

namespace miacode::extensions {
namespace {

ExtensionOpenBridgeMethod method(
    const QString& name,
    const QString& route,
    const QString& permission,
    const QString& description,
    const QString& status = {})
{
    const bool isHostMethod = route.contains(QLatin1Char('/'));
    const QString resolvedStatus = status.isEmpty()
        ? (route.isEmpty() ? QStringLiteral("planned") : QStringLiteral("implemented"))
        : status;
    return ExtensionOpenBridgeMethod{
        name,
        isHostMethod ? route : QString(),
        isHostMethod ? QString() : route,
        permission,
        resolvedStatus,
        description,
    };
}

ExtensionOpenBridgeObject object(
    const QString& id,
    const QString& permission,
    const QString& description,
    QVector<ExtensionOpenBridgeMethod> methods,
    const QString& stability = QStringLiteral("open"))
{
    return ExtensionOpenBridgeObject{
        id,
        permission,
        stability,
        description,
        std::move(methods),
    };
}

ExtensionOpenBridgeObject experimentalRawObject(const ExtensionForbiddenOpenTarget& target)
{
    return object(
        target.id,
        QStringLiteral("experimental.invoke"),
        QStringLiteral("%1 Experimental raw target: %2").arg(target.category, target.reason),
        {
            method(QStringLiteral("inspect"), QStringLiteral("experimental/raw/inspect"), QStringLiteral("experimental.invoke"), QStringLiteral("Inspect an experimental raw target descriptor.")),
            method(QStringLiteral("callUnsafe"), QStringLiteral("experimental/raw/call"), QStringLiteral("experimental.invoke"), QStringLiteral("Submit an experimental raw target call descriptor.")),
        },
        QStringLiteral("experimentalRaw"));
}

QJsonObject methodToJson(const ExtensionOpenBridgeMethod& method)
{
    QJsonObject object{
        {QStringLiteral("name"), method.name},
        {QStringLiteral("permission"), method.permission},
        {QStringLiteral("status"), method.status},
        {QStringLiteral("description"), method.description},
    };
    if (!method.hostMethod.isEmpty()) {
        object.insert(QStringLiteral("hostMethod"), method.hostMethod);
    }
    if (!method.command.isEmpty()) {
        object.insert(QStringLiteral("command"), method.command);
    }
    return object;
}

QJsonObject objectToJson(const ExtensionOpenBridgeObject& object)
{
    QJsonArray methods;
    for (const ExtensionOpenBridgeMethod& method : object.methods) {
        methods.append(methodToJson(method));
    }
    const bool experimentalRaw = object.stability == QStringLiteral("experimentalRaw");
    return QJsonObject{
        {QStringLiteral("id"), object.id},
        {QStringLiteral("permission"), object.permission},
        {QStringLiteral("stability"), object.stability},
        {QStringLiteral("experimentalRaw"), experimentalRaw},
        {QStringLiteral("description"), object.description},
        {QStringLiteral("methods"), methods},
        {QStringLiteral("rawAccess"), experimentalRaw},
        {QStringLiteral("rawCppObjectsExposed"), experimentalRaw},
    };
}

QJsonObject forbiddenTargetToJson(const ExtensionForbiddenOpenTarget& target)
{
    QJsonObject object = objectToJson(experimentalRawObject(target));
    object.insert(QStringLiteral("category"), target.category);
    object.insert(QStringLiteral("reason"), target.reason);
    object.insert(QStringLiteral("forbidden"), false);
    object.insert(QStringLiteral("legacyName"), QStringLiteral("forbiddenTarget"));
    return object;
}

QJsonObject legacyForbiddenTargetToJson(const ExtensionForbiddenOpenTarget& target)
{
    return QJsonObject{
        {QStringLiteral("id"), target.id},
        {QStringLiteral("category"), target.category},
        {QStringLiteral("reason"), target.reason},
        {QStringLiteral("forbidden"), true},
    };
}

QString normalizedOpenTargetId(QString id)
{
    id = id.trimmed();
    if (id.startsWith(QStringLiteral("miacode.internal."))) {
        id.remove(0, QStringLiteral("miacode.internal.").size());
    }
    while (id.endsWith(QLatin1Char('*'))) {
        id.chop(1);
        id = id.trimmed();
    }
    return id;
}

bool targetMatchesForbiddenId(const QString& requestedId, const QString& forbiddenId)
{
    const QString requested = normalizedOpenTargetId(requestedId);
    const QString forbidden = normalizedOpenTargetId(forbiddenId);
    return requested.compare(forbidden, Qt::CaseInsensitive) == 0
        || requested.startsWith(forbidden + QLatin1Char('.'), Qt::CaseInsensitive)
        || requested.startsWith(forbidden + QStringLiteral("::"), Qt::CaseInsensitive);
}

}  // namespace

QVector<ExtensionOpenBridgeObject> extensionOpenBridgeObjects()
{
    static const QVector<ExtensionOpenBridgeObject> objects = [] {
        QVector<ExtensionOpenBridgeObject> result{
        object(QStringLiteral("app"), QStringLiteral("open.app"), QStringLiteral("Application-level commands exposed through a facade."),
               {
                   method(QStringLiteral("openPreferences"), QStringLiteral("app.openPreferences"), QStringLiteral("ui.prompt"), QStringLiteral("Open Preferences.")),
                   method(QStringLiteral("openAboutDialog"), QStringLiteral("app.openAboutDialog"), QStringLiteral("ui.prompt"), QStringLiteral("Open About dialog.")),
               }),
        object(QStringLiteral("workspace"), QStringLiteral("open.workspace"), QStringLiteral("Workspace file and save operations exposed through a facade."),
               {
                   method(QStringLiteral("save"), QStringLiteral("workspace.save"), QStringLiteral("workspace.write"), QStringLiteral("Save the active chart.")),
                   method(QStringLiteral("saveAs"), QStringLiteral("workspace.saveAs"), QStringLiteral("workspace.write"), QStringLiteral("Save the active chart to a path.")),
               }),
        object(QStringLiteral("document"), QStringLiteral("open.document"), QStringLiteral("Document query and edit operations exposed through a facade."),
               {
                   method(QStringLiteral("query"), QStringLiteral("document/query"), QStringLiteral("workspace.read"), QStringLiteral("Query active chart data.")),
                   method(QStringLiteral("edit"), QStringLiteral("document/edit"), QStringLiteral("document.edit"), QStringLiteral("Apply structured chart edit operations.")),
                   method(QStringLiteral("setActiveDifficulty"), QStringLiteral("document/setActiveDifficulty"), QStringLiteral("document.edit"), QStringLiteral("Switch the active difficulty.")),
                   method(QStringLiteral("formatActiveDifficulty"), QStringLiteral("document/format"), QStringLiteral("document.edit"), QStringLiteral("Trim trailing whitespace in the active difficulty.")),
               }),
        object(QStringLiteral("editor"), QStringLiteral("open.editor"), QStringLiteral("Editor commands exposed through a facade."),
               {
                   method(QStringLiteral("undo"), QStringLiteral("editor.undo"), QStringLiteral("editor.edit"), QStringLiteral("Undo the active editor.")),
                   method(QStringLiteral("redo"), QStringLiteral("editor.redo"), QStringLiteral("editor.edit"), QStringLiteral("Redo the active editor.")),
                   method(QStringLiteral("cut"), QStringLiteral("editor.cut"), QStringLiteral("editor.edit"), QStringLiteral("Cut the active selection.")),
                   method(QStringLiteral("copy"), QStringLiteral("editor.copy"), QStringLiteral("editor.read"), QStringLiteral("Copy the active selection.")),
                   method(QStringLiteral("paste"), QStringLiteral("editor.paste"), QStringLiteral("editor.edit"), QStringLiteral("Paste into the active editor.")),
                   method(QStringLiteral("selectAll"), QStringLiteral("editor.selectAll"), QStringLiteral("editor.edit"), QStringLiteral("Select all active editor text.")),
                   method(QStringLiteral("getText"), QStringLiteral("editor/getText"), QStringLiteral("editor.read"), QStringLiteral("Read active editor text.")),
                   method(QStringLiteral("getVisibleRange"), QStringLiteral("editor/getVisibleRange"), QStringLiteral("editor.read"), QStringLiteral("Read visible editor range.")),
                   method(QStringLiteral("revealRange"), QStringLiteral("editor/revealRange"), QStringLiteral("editor.edit"), QStringLiteral("Reveal an editor range.")),
                   method(QStringLiteral("getParsedSnapshot"), QStringLiteral("editor/getParsedSnapshot"), QStringLiteral("editor.read"), QStringLiteral("Read editor/parsed marker summary.")),
                   method(QStringLiteral("showCompletions"), QStringLiteral("editor/showCompletions"), QStringLiteral("providers.read"), QStringLiteral("Show completions in host UI.")),
                   method(QStringLiteral("showCodeActions"), QStringLiteral("editor/showCodeActions"), QStringLiteral("providers.read"), QStringLiteral("Show code actions in host UI.")),
               }),
        object(QStringLiteral("timeline"), QStringLiteral("open.timeline"), QStringLiteral("Timeline read/control operations exposed through a facade."),
               {
                   method(QStringLiteral("seek"), QStringLiteral("timeline.seek"), QStringLiteral("timeline.control"), QStringLiteral("Seek the preview timeline.")),
                   method(QStringLiteral("getCurrentSecond"), QStringLiteral("timeline/getCurrentSecond"), QStringLiteral("timeline.read"), QStringLiteral("Read the current preview/timeline second.")),
                   method(QStringLiteral("getSnapshot"), QStringLiteral("timeline/getSnapshot"), QStringLiteral("timeline.read"), QStringLiteral("Read the timeline snapshot.")),
                   method(QStringLiteral("getZoomState"), QStringLiteral("timeline/getZoomState"), QStringLiteral("timeline.read"), QStringLiteral("Read timeline zoom state.")),
                   method(QStringLiteral("getVisibleRange"), QStringLiteral("timeline/getVisibleRange"), QStringLiteral("timeline.read"), QStringLiteral("Read approximate visible timeline seconds.")),
                   method(QStringLiteral("getMarkersAtSecond"), QStringLiteral("timeline/getMarkersAtSecond"), QStringLiteral("timeline.read"), QStringLiteral("Read parsed markers near a second.")),
                   method(QStringLiteral("zoomIn"), QStringLiteral("timeline/zoomIn"), QStringLiteral("timeline.control"), QStringLiteral("Increase timeline zoom by one preset.")),
                   method(QStringLiteral("zoomOut"), QStringLiteral("timeline/zoomOut"), QStringLiteral("timeline.control"), QStringLiteral("Decrease timeline zoom by one preset.")),
                   method(QStringLiteral("stepZoomPreset"), QStringLiteral("timeline/stepZoomPreset"), QStringLiteral("timeline.control"), QStringLiteral("Move timeline zoom by preset delta.")),
                   method(QStringLiteral("setZoomScale"), QStringLiteral("timeline/setZoomScale"), QStringLiteral("timeline.control"), QStringLiteral("Set timeline zoom scale.")),
                   method(QStringLiteral("scrollToSecond"), QStringLiteral("timeline/scrollToSecond"), QStringLiteral("timeline.control"), QStringLiteral("Scroll or seek the timeline to a second.")),
                   method(QStringLiteral("setFollowPreview"), QStringLiteral("timeline/setFollowPreview"), QStringLiteral("timeline.control"), QStringLiteral("Set timeline follow-preview mode.")),
                   method(QStringLiteral("setFollowProgress"), QStringLiteral("timeline/setFollowProgress"), QStringLiteral("timeline.control"), QStringLiteral("Set timeline follow-progress mode.")),
               }),
        object(QStringLiteral("preview"), QStringLiteral("open.preview"), QStringLiteral("Preview transport and state operations exposed through a facade."),
               {
                   method(QStringLiteral("play"), QStringLiteral("preview/play"), QStringLiteral("preview.control"), QStringLiteral("Start preview playback.")),
                   method(QStringLiteral("pause"), QStringLiteral("preview/pause"), QStringLiteral("preview.control"), QStringLiteral("Pause preview playback.")),
                   method(QStringLiteral("stop"), QStringLiteral("preview/stop"), QStringLiteral("preview.control"), QStringLiteral("Stop preview playback.")),
                   method(QStringLiteral("seek"), QStringLiteral("preview/seek"), QStringLiteral("preview.control"), QStringLiteral("Seek preview playback.")),
                   method(QStringLiteral("setSpeed"), QStringLiteral("preview/setSpeed"), QStringLiteral("preview.control"), QStringLiteral("Set preview speed.")),
                   method(QStringLiteral("getState"), QStringLiteral("preview/getState"), QStringLiteral("preview.read"), QStringLiteral("Read preview state.")),
                   method(QStringLiteral("getRenderState"), QStringLiteral("preview/getRenderState"), QStringLiteral("preview.read"), QStringLiteral("Read preview canvas/render settings.")),
               }),
        object(QStringLiteral("validation"), QStringLiteral("open.validation"), QStringLiteral("Validation operations exposed through a facade."),
               {
                   method(QStringLiteral("run"), QStringLiteral("validation.run"), QStringLiteral("diagnostics.run"), QStringLiteral("Run chart validation.")),
                   method(QStringLiteral("getLastResult"), QStringLiteral("validation/getLastResult"), QStringLiteral("diagnostics.run"), QStringLiteral("Read the last validation result.")),
               }),
        object(QStringLiteral("analysis"), QStringLiteral("open.analysis"), QStringLiteral("Analysis operations exposed through a facade."),
               {
                   method(QStringLiteral("runMuriAnalysis"), QStringLiteral("analysis.runMuriAnalysis"), QStringLiteral("analysis.run"), QStringLiteral("Run Muri analysis from current timeline markers.")),
                   method(QStringLiteral("getLastMuriResult"), QStringLiteral("analysis/getLastMuriResult"), QStringLiteral("analysis.run"), QStringLiteral("Read the last Muri analysis result.")),
               }),
        object(QStringLiteral("export"), QStringLiteral("open.export"), QStringLiteral("Export entry points exposed through a facade."),
               {
                   method(QStringLiteral("startVideoExport"), QStringLiteral("export/startVideoExport"), QStringLiteral("export.write"), QStringLiteral("Open the video export flow.")),
                   method(QStringLiteral("startCoverExport"), QStringLiteral("export/startCoverExport"), QStringLiteral("export.write"), QStringLiteral("Open the cover export flow.")),
                   method(QStringLiteral("getPresets"), QStringLiteral("export/getPresets"), QStringLiteral("export.read"), QStringLiteral("Read export presets.")),
               }),
        object(QStringLiteral("ui"), QStringLiteral("open.ui"), QStringLiteral("Extension UI contribution state exposed through a facade."),
               {
                   method(QStringLiteral("registerPetOverlay"), QStringLiteral("ui/registerPetOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("Register a controlled preview pet overlay with extension-local image resources.")),
                   method(QStringLiteral("registerSceneOverlay"), QStringLiteral("ui/registerSceneOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("Register a controlled preview scene overlay.")),
                   method(QStringLiteral("renderFloatingPanel"), QStringLiteral("ui/renderFloatingPanel"), QStringLiteral("ui.contribute"), QStringLiteral("Render a controlled floating extension panel.")),
                   method(QStringLiteral("renderSceneOverlay"), QStringLiteral("ui/renderSceneOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("Render a controlled preview scene overlay.")),
                   method(QStringLiteral("renderWebView"), QStringLiteral("ui/renderWebView"), QStringLiteral("ui.contribute"), QStringLiteral("Render an HTML-lite view.")),
                   method(QStringLiteral("renderCanvasView"), QStringLiteral("ui/renderCanvasView"), QStringLiteral("ui.contribute"), QStringLiteral("Render a declarative canvas/scene view.")),
                   method(QStringLiteral("getViews"), QStringLiteral("ui/getViews"), QStringLiteral("ui.contribute"), QStringLiteral("Read rendered extension views.")),
                   method(QStringLiteral("refreshViews"), QStringLiteral("ui/refreshViews"), QStringLiteral("ui.contribute"), QStringLiteral("Refresh extension view hosts.")),
               }),
        object(QStringLiteral("input"), QStringLiteral("open.input"), QStringLiteral("Controlled input gestures exposed through a facade."),
               {
                   method(QStringLiteral("registerWheelGesture"), QStringLiteral("input/registerWheelGesture"), QStringLiteral("input.listen"), QStringLiteral("Register a wheel gesture that invokes an extension command.")),
                   method(QStringLiteral("registerKeyGesture"), QStringLiteral("input/registerKeyGesture"), QStringLiteral("input.listen"), QStringLiteral("Register a key gesture that invokes an extension command.")),
                   method(QStringLiteral("registerMouseGesture"), QStringLiteral("input/registerMouseGesture"), QStringLiteral("input.listen"), QStringLiteral("Register a mouse gesture that invokes an extension command.")),
                   method(QStringLiteral("getGestures"), QStringLiteral("input/getGestures"), QStringLiteral("input.listen"), QStringLiteral("List registered extension input gestures.")),
               }),
        object(QStringLiteral("providers"), QStringLiteral("open.providers"), QStringLiteral("Provider registration and consumption exposed through a facade."),
               {
                   method(QStringLiteral("getRegistered"), QStringLiteral("providers/getRegistered"), QStringLiteral("providers.read"), QStringLiteral("List registered providers.")),
                   method(QStringLiteral("collectHover"), QStringLiteral("providers/collectHover"), QStringLiteral("providers.read"), QStringLiteral("Collect hover providers for context.")),
                   method(QStringLiteral("collectCompletions"), QStringLiteral("providers/collectCompletions"), QStringLiteral("providers.read"), QStringLiteral("Collect completion providers for context.")),
                   method(QStringLiteral("collectCodeActions"), QStringLiteral("providers/collectCodeActions"), QStringLiteral("providers.read"), QStringLiteral("Collect code-action providers for context.")),
                   method(QStringLiteral("showHover"), QStringLiteral("providers/showHover"), QStringLiteral("providers.read"), QStringLiteral("Show hover provider output in host UI.")),
                   method(QStringLiteral("showCompletions"), QStringLiteral("providers/showCompletions"), QStringLiteral("providers.read"), QStringLiteral("Show completion provider output in host UI.")),
                   method(QStringLiteral("showCodeActions"), QStringLiteral("providers/showCodeActions"), QStringLiteral("providers.read"), QStringLiteral("Show code-action provider output in host UI.")),
               }),
        object(QStringLiteral("shortcuts"), QStringLiteral("open.shortcuts"), QStringLiteral("Editable extension shortcut operations exposed through a facade."),
               {
                   method(QStringLiteral("list"), QStringLiteral("shortcuts/list"), QStringLiteral("settings.read"), QStringLiteral("List extension shortcut bindings.")),
                   method(QStringLiteral("getEditable"), QStringLiteral("shortcuts/getEditable"), QStringLiteral("settings.read"), QStringLiteral("List shortcuts visible in Preferences > Shortcuts.")),
                   method(QStringLiteral("getKeybinding"), QStringLiteral("shortcuts/getKeybinding"), QStringLiteral("settings.read"), QStringLiteral("Read one extension shortcut binding.")),
                   method(QStringLiteral("register"), QStringLiteral("shortcuts/register"), QStringLiteral("settings.write"), QStringLiteral("Register an extension command shortcut.")),
               }),
        object(QStringLiteral("extensions"), QStringLiteral("open.extensions"), QStringLiteral("Extension management state exposed through a facade."),
               {
                   method(QStringLiteral("all"), QStringLiteral("extensions.all"), QStringLiteral("extensions.manage"), QStringLiteral("List discovered extensions.")),
                   method(QStringLiteral("reload"), QStringLiteral("extensions.reload"), QStringLiteral("extensions.manage"), QStringLiteral("Reload extensions.")),
               }),
        };
        for (const ExtensionForbiddenOpenTarget& target : extensionForbiddenOpenTargets()) {
            result.append(experimentalRawObject(target));
        }
        return result;
    }();
    return objects;
}

QJsonArray extensionOpenBridgeObjectIds()
{
    QJsonArray array;
    for (const ExtensionOpenBridgeObject& object : extensionOpenBridgeObjects()) {
        array.append(object.id);
    }
    return array;
}

QJsonArray extensionOpenBridgeObjectsJson()
{
    QJsonArray array;
    for (const ExtensionOpenBridgeObject& object : extensionOpenBridgeObjects()) {
        array.append(objectToJson(object));
    }
    return array;
}

QJsonObject extensionOpenBridgeDescribeObject(const QString& objectId)
{
    const QString id = normalizedOpenTargetId(objectId);
    for (const ExtensionOpenBridgeObject& object : extensionOpenBridgeObjects()) {
        if (object.id.compare(id, Qt::CaseInsensitive) == 0) {
            return objectToJson(object);
        }
    }
    return QJsonObject();
}

bool extensionOpenBridgeHasObject(const QString& objectId)
{
    return !extensionOpenBridgeDescribeObject(objectId).isEmpty();
}

QJsonObject extensionOpenBridgeDescribeMethod(const QString& objectId, const QString& methodName)
{
    const QString normalizedObjectId = normalizedOpenTargetId(objectId);
    const QString normalizedMethodName = methodName.trimmed();
    for (const ExtensionOpenBridgeObject& object : extensionOpenBridgeObjects()) {
        if (object.id.compare(normalizedObjectId, Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const ExtensionOpenBridgeMethod& method : object.methods) {
            if (method.name.compare(normalizedMethodName, Qt::CaseInsensitive) == 0) {
                return methodToJson(method);
            }
        }
    }
    return QJsonObject();
}

QVector<ExtensionForbiddenOpenTarget> extensionForbiddenOpenTargets()
{
    static const QVector<ExtensionForbiddenOpenTarget> targets{
        {QStringLiteral("MainWindow"), QStringLiteral("raw-ui-owner"), QStringLiteral("Owns app orchestration and mutable UI state; expose a facade instead.")},
        {QStringLiteral("QWidget"), QStringLiteral("raw-qt-widget"), QStringLiteral("Raw widgets expose lifetime, parenting, styling, and thread-affinity hazards.")},
        {QStringLiteral("QQuickItem"), QStringLiteral("raw-qt-quick-item"), QStringLiteral("Raw Qt Quick items expose scene graph lifetime and rendering internals.")},
        {QStringLiteral("QSGNode"), QStringLiteral("raw-scene-graph"), QStringLiteral("Scene graph nodes are render-thread objects and must not be extension-owned.")},
        {QStringLiteral("QPainter"), QStringLiteral("raw-rendering"), QStringLiteral("Raw painting bypasses the declarative UI/rendering policy.")},
        {QStringLiteral("QRhi"), QStringLiteral("raw-rendering"), QStringLiteral("Raw RHI access bypasses renderer ownership and backend selection.")},
        {QStringLiteral("D3D11"), QStringLiteral("raw-rendering"), QStringLiteral("Direct D3D access bypasses the renderer boundary.")},
        {QStringLiteral("DirectComposition"), QStringLiteral("raw-rendering"), QStringLiteral("DirectComposition access bypasses the renderer boundary.")},
        {QStringLiteral("PreviewRuntime"), QStringLiteral("raw-preview-runtime"), QStringLiteral("Preview runtime lifetime, threads, and render ownership stay host-controlled.")},
        {QStringLiteral("PreviewQuickSceneRoot"), QStringLiteral("raw-preview-scene"), QStringLiteral("The QSG scene root is renderer-owned; expose preview facade methods instead.")},
        {QStringLiteral("PreviewQuickLayer"), QStringLiteral("raw-preview-scene"), QStringLiteral("Preview layers are renderer-owned implementation details.")},
        {QStringLiteral("TimelineQuickItem"), QStringLiteral("raw-timeline-scene"), QStringLiteral("Timeline QSG item lifetime and interaction routing stay host-controlled.")},
        {QStringLiteral("SimaiDocument"), QStringLiteral("raw-document-model"), QStringLiteral("Raw document mutation would bypass dirty-state, validation, timeline, and preview sync.")},
        {QStringLiteral("PlainCodeEditor"), QStringLiteral("raw-editor-widget"), QStringLiteral("Raw editor widgets bypass editor facade permissions and UI lifecycle.")},
        {QStringLiteral("QObject"), QStringLiteral("raw-reflection"), QStringLiteral("Arbitrary QObject reflection would bypass the Open Bridge allowlist.")},
        {QStringLiteral("QProcess"), QStringLiteral("process-execution"), QStringLiteral("Process spawning is experimental raw access and requires explicit host binding.")},
        {QStringLiteral("shell.execute"), QStringLiteral("process-execution"), QStringLiteral("Shell execution is experimental raw access and requires explicit host binding.")},
        {QStringLiteral("renderer.raw"), QStringLiteral("raw-rendering"), QStringLiteral("Raw renderer access is experimental and explicitly marked unstable.")},
        {QStringLiteral("internal.raw"), QStringLiteral("raw-internal"), QStringLiteral("Raw internal calls are experimental and require explicit Open Bridge permissions.")},
        {QStringLiteral("security"), QStringLiteral("security"), QStringLiteral("Security internals are experimental raw access and explicitly marked unstable.")},
        {QStringLiteral("updates"), QStringLiteral("updates"), QStringLiteral("Updater internals are experimental raw access and explicitly marked unstable.")},
    };
    return targets;
}

QJsonArray extensionForbiddenOpenTargetsJson()
{
    QJsonArray array;
    for (const ExtensionForbiddenOpenTarget& target : extensionForbiddenOpenTargets()) {
        array.append(forbiddenTargetToJson(target));
    }
    return array;
}

QJsonObject extensionDescribeForbiddenOpenTarget(const QString& targetId)
{
    for (const ExtensionForbiddenOpenTarget& target : extensionForbiddenOpenTargets()) {
        if (targetMatchesForbiddenId(targetId, target.id)) {
            return forbiddenTargetToJson(target);
        }
    }
    return QJsonObject();
}

bool extensionIsForbiddenOpenTarget(const QString& targetId)
{
    Q_UNUSED(targetId);
    return false;
}

}  // namespace miacode::extensions
