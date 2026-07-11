#include "EmbeddedExtensionRuntime.h"

#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

namespace miacode::extensions {

class EmbeddedExtensionRuntime::BridgeObject final : public QObject {
    Q_OBJECT

public:
    enum class Kind {
        Commands,
        Window,
        Workspace,
        Diagnostics,
        App,
        Document,
        Editor,
        Validation,
        Analysis,
        Timeline,
        Preview,
        Export,
        FileSystem,
        Resources,
        Network,
        Settings,
        Extensions,
        Ui,
        Tasks,
        Logs,
        Api,
        Context,
    };

    BridgeObject(EmbeddedExtensionRuntime* runtime, Kind kind, QString extensionId = {})
        : QObject(runtime)
        , runtime_(runtime)
        , kind_(kind)
        , extensionId_(std::move(extensionId))
    {
    }

    Q_INVOKABLE QJSValue registerCommand(const QString& command, const QJSValue& callback)
    {
        if (kind_ != Kind::Commands || runtime_ == nullptr) {
            return QJSValue();
        }
        runtime_->registerCommand(command, callback);
        return runtime_->disposableValue();
    }

    Q_INVOKABLE QJSValue executeCommand(const QString& command, const QJSValue& args = {})
    {
        return hostCall(QStringLiteral("commands/execute"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("args"), QJsonValue::fromVariant(args.toVariant())},
        });
    }

    Q_INVOKABLE QJSValue getCommands()
    {
        return hostCall(QStringLiteral("commands/getCommands"));
    }

    Q_INVOKABLE QJSValue showInformationMessage(const QString& message)
    {
        return showMessage(QStringLiteral("info"), message);
    }

    Q_INVOKABLE QJSValue showWarningMessage(const QString& message)
    {
        return showMessage(QStringLiteral("warning"), message);
    }

    Q_INVOKABLE QJSValue showErrorMessage(const QString& message)
    {
        return showMessage(QStringLiteral("error"), message);
    }

    Q_INVOKABLE QJSValue showInputBox(const QJSValue& options)
    {
        return hostCall(QStringLiteral("window/showInputBox"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue showQuickPick(const QJSValue& items, const QJSValue& options)
    {
        QJsonObject params = runtime_->scriptValueToJson(options);
        params.insert(QStringLiteral("items"), QJsonArray::fromStringList(items.toVariant().toStringList()));
        return hostCall(QStringLiteral("window/showQuickPick"), params);
    }

    Q_INVOKABLE QJSValue createStatusBarItem(const QJSValue& options)
    {
        return hostCall(QStringLiteral("window/createStatusBarItem"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue getInfo()
    {
        return hostCall(QStringLiteral("app/getInfo"));
    }

    Q_INVOKABLE QJSValue openPreferences()
    {
        return hostCall(QStringLiteral("app/openPreferences"));
    }

    Q_INVOKABLE QJSValue reloadExtensions()
    {
        return hostCall(QStringLiteral("app/reloadExtensions"));
    }

    Q_INVOKABLE QJSValue getRecentFiles()
    {
        return hostCall(QStringLiteral("workspace/getRecentFiles"));
    }

    Q_INVOKABLE QJSValue getProjectData(const QString& key = {})
    {
        return hostCall(QStringLiteral("workspace/getProjectData"), QJsonObject{{QStringLiteral("key"), key}});
    }

    Q_INVOKABLE QJSValue setProjectData(const QString& key, const QJSValue& value)
    {
        return hostCall(QStringLiteral("workspace/setProjectData"), QJsonObject{
            {QStringLiteral("key"), key},
            {QStringLiteral("value"), QJsonValue::fromVariant(value.toVariant())},
        });
    }

    Q_INVOKABLE QJSValue scanChartFolders(const QString& rootPath)
    {
        return hostCall(QStringLiteral("workspace/scanChartFolders"), QJsonObject{{QStringLiteral("rootPath"), rootPath}});
    }

    Q_INVOKABLE QJSValue onDidOpenDocument(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/workspace.onDidOpenDocument"), callback);
    }

    Q_INVOKABLE QJSValue onDidSaveDocument(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/workspace.onDidSaveDocument"), callback);
    }

    Q_INVOKABLE QJSValue onDidChangeText(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/document.onDidChangeText"), callback);
    }

    Q_INVOKABLE QJSValue onDidChangeSelection(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/editor.onDidChangeSelection"), callback);
    }

    Q_INVOKABLE QJSValue onDidChangeState(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/preview.onDidChangeState"), callback);
    }

    Q_INVOKABLE QJSValue onDidSeek(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/timeline.onDidSeek"), callback);
    }

    Q_INVOKABLE QJSValue getActiveDocument()
    {
        if (kind_ != Kind::Workspace || runtime_ == nullptr) {
            return QJSValue();
        }
        return runtime_->jsonToScriptValue(runtime_->requestHost(QStringLiteral("workspace/getActiveDocument")));
    }

    Q_INVOKABLE QJSValue getChartMetadata()
    {
        return hostCall(QStringLiteral("workspace/getChartMetadata"));
    }

    Q_INVOKABLE QJSValue updateChartMetadata(const QJSValue& patch)
    {
        return hostCall(QStringLiteral("workspace/updateChartMetadata"), runtime_->scriptValueToJson(patch));
    }

    Q_INVOKABLE QJSValue getChartFolder()
    {
        return hostCall(QStringLiteral("workspace/getChartFolder"));
    }

    Q_INVOKABLE QJSValue getMediaFiles()
    {
        return hostCall(QStringLiteral("workspace/getMediaFiles"));
    }

    Q_INVOKABLE QJSValue save()
    {
        return hostCall(QStringLiteral("workspace/save"));
    }

    Q_INVOKABLE QJSValue saveAs(const QString& path)
    {
        return hostCall(QStringLiteral("workspace/saveAs"), QJsonObject{{QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue applyDocumentEdit(const QJSValue& edit)
    {
        if (kind_ != Kind::Workspace || runtime_ == nullptr) {
            return QJSValue();
        }
        if (!edit.isObject() || !edit.property(QStringLiteral("text")).isString()) {
            return runtime_->jsonToScriptValue(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("applyDocumentEdit expects an object with a string 'text' property.")},
            });
        }
        const QString text = edit.property(QStringLiteral("text")).toString();
        return runtime_->jsonToScriptValue(runtime_->requestHost(
            QStringLiteral("workspace/applyDocumentEdit"),
            QJsonObject{{QStringLiteral("text"), text}}));
    }

    Q_INVOKABLE QJSValue getDifficulties()
    {
        return hostCall(QStringLiteral("document/getDifficulties"));
    }

    Q_INVOKABLE QJSValue getActiveDifficulty()
    {
        return hostCall(QStringLiteral("document/getActiveDifficulty"));
    }

    Q_INVOKABLE QJSValue setActiveDifficulty(int id)
    {
        return hostCall(QStringLiteral("document/setActiveDifficulty"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue replaceActiveDifficultyText(const QString& text)
    {
        return hostCall(QStringLiteral("document/replaceActiveDifficultyText"), QJsonObject{{QStringLiteral("text"), text}});
    }

    Q_INVOKABLE QJSValue getParsedNoteMarkers()
    {
        return hostCall(QStringLiteral("document/getParsedNoteMarkers"));
    }

    Q_INVOKABLE QJSValue getTimingMetadata()
    {
        return hostCall(QStringLiteral("document/getTimingMetadata"));
    }

    Q_INVOKABLE QJSValue applyTextEdits(const QJSValue& edits)
    {
        return hostCall(QStringLiteral("document/applyTextEdits"), QJsonObject{
            {QStringLiteral("edits"), QJsonArray::fromVariantList(edits.toVariant().toList())},
        });
    }

    Q_INVOKABLE QJSValue format()
    {
        return hostCall(QStringLiteral("document/format"));
    }

    Q_INVOKABLE QJSValue createDifficulty(const QJSValue& options)
    {
        return hostCall(QStringLiteral("document/createDifficulty"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue deleteDifficulty(int id)
    {
        return hostCall(QStringLiteral("document/deleteDifficulty"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue renameDifficulty(int id, const QString& label)
    {
        return hostCall(QStringLiteral("document/renameDifficulty"), QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}});
    }

    Q_INVOKABLE QJSValue getSelection()
    {
        return hostCall(QStringLiteral("editor/getSelection"));
    }

    Q_INVOKABLE QJSValue getCursor()
    {
        return hostCall(QStringLiteral("editor/getCursor"));
    }

    Q_INVOKABLE QJSValue insertText(const QString& text)
    {
        return hostCall(QStringLiteral("editor/insertText"), QJsonObject{{QStringLiteral("text"), text}});
    }

    Q_INVOKABLE QJSValue replaceSelection(const QString& text)
    {
        return hostCall(QStringLiteral("editor/replaceSelection"), QJsonObject{{QStringLiteral("text"), text}});
    }

    Q_INVOKABLE QJSValue setSelection(const QJSValue& range)
    {
        return hostCall(QStringLiteral("editor/setSelection"), runtime_->scriptValueToJson(range));
    }

    Q_INVOKABLE QJSValue addDecoration(const QJSValue& range, const QJSValue& options)
    {
        QJsonObject params = runtime_->scriptValueToJson(range);
        params.insert(QStringLiteral("options"), runtime_->scriptValueToJson(options));
        return hostCall(QStringLiteral("editor/addDecoration"), params);
    }

    Q_INVOKABLE QJSValue clearDecorations(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("editor/clearDecorations"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue getLine(int line)
    {
        return hostCall(QStringLiteral("editor/getLine"), QJsonObject{{QStringLiteral("line"), line}});
    }

    Q_INVOKABLE QJSValue getCurrentLine()
    {
        return hostCall(QStringLiteral("editor/getCurrentLine"));
    }

    Q_INVOKABLE QJSValue getCurrentToken()
    {
        return hostCall(QStringLiteral("editor/getCurrentToken"));
    }

    Q_INVOKABLE QJSValue replaceRange(const QJSValue& range, const QString& text)
    {
        QJsonObject params = runtime_->scriptValueToJson(range);
        params.insert(QStringLiteral("text"), text);
        return hostCall(QStringLiteral("editor/replaceRange"), params);
    }

    Q_INVOKABLE QJSValue showHover(const QJSValue& range, const QString& markdown)
    {
        QJsonObject params = runtime_->scriptValueToJson(range);
        params.insert(QStringLiteral("markdown"), markdown);
        return hostCall(QStringLiteral("editor/showHover"), params);
    }

    Q_INVOKABLE QJSValue addGutterIcon(const QJSValue& options)
    {
        return hostCall(QStringLiteral("editor/addGutterIcon"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue clearGutterIcons(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("editor/clearGutterIcons"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue fold(const QJSValue& range)
    {
        return hostCall(QStringLiteral("editor/fold"), runtime_->scriptValueToJson(range));
    }

    Q_INVOKABLE QJSValue unfold(const QJSValue& range)
    {
        return hostCall(QStringLiteral("editor/unfold"), runtime_->scriptValueToJson(range));
    }

    Q_INVOKABLE QJSValue registerHoverProvider(const QJSValue& provider)
    {
        return registerContribution(QStringLiteral("providers/hover"), provider);
    }

    Q_INVOKABLE QJSValue registerCompletionProvider(const QJSValue& provider)
    {
        return registerContribution(QStringLiteral("providers/completion"), provider);
    }

    Q_INVOKABLE QJSValue registerCodeActionProvider(const QJSValue& provider)
    {
        return registerContribution(QStringLiteral("providers/codeAction"), provider);
    }

    Q_INVOKABLE QJSValue validateDocument()
    {
        if (kind_ != Kind::Diagnostics || runtime_ == nullptr) {
            return QJSValue();
        }
        return runtime_->jsonToScriptValue(runtime_->requestHost(QStringLiteral("diagnostics/validateDocument")));
    }

    Q_INVOKABLE QJSValue run()
    {
        return hostCall(QStringLiteral("validation/run"));
    }

    Q_INVOKABLE QJSValue getLastResult()
    {
        return hostCall(QStringLiteral("validation/getLastResult"));
    }

    Q_INVOKABLE QJSValue addDiagnostics(const QString& ownerId, const QJSValue& diagnostics)
    {
        return hostCall(QStringLiteral("validation/addDiagnostics"), QJsonObject{
            {QStringLiteral("ownerId"), ownerId},
            {QStringLiteral("diagnostics"), QJsonArray::fromVariantList(diagnostics.toVariant().toList())},
        });
    }

    Q_INVOKABLE QJSValue clearDiagnostics(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("validation/clearDiagnostics"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue runMuriAnalysis()
    {
        return hostCall(QStringLiteral("analysis/runMuriAnalysis"));
    }

    Q_INVOKABLE QJSValue getLastMuriResult()
    {
        return hostCall(QStringLiteral("analysis/getLastMuriResult"));
    }

    Q_INVOKABLE QJSValue getSnapshot()
    {
        return hostCall(QStringLiteral("timeline/getSnapshot"));
    }

    Q_INVOKABLE QJSValue getCurrentSecond()
    {
        return hostCall(QStringLiteral("timeline/getCurrentSecond"));
    }

    Q_INVOKABLE QJSValue seek(double second)
    {
        return hostCall(kind_ == Kind::Preview ? QStringLiteral("preview/seek") : QStringLiteral("timeline/seek"),
                        QJsonObject{{QStringLiteral("second"), second}});
    }

    Q_INVOKABLE QJSValue addMarker(const QJSValue& marker)
    {
        return hostCall(QStringLiteral("timeline/addMarker"), runtime_->scriptValueToJson(marker));
    }

    Q_INVOKABLE QJSValue clearMarkers(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("timeline/clearMarkers"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue addBand(const QJSValue& band)
    {
        return hostCall(QStringLiteral("timeline/addBand"), runtime_->scriptValueToJson(band));
    }

    Q_INVOKABLE QJSValue addVerticalLine(const QJSValue& line)
    {
        return hostCall(QStringLiteral("timeline/addVerticalLine"), runtime_->scriptValueToJson(line));
    }

    Q_INVOKABLE QJSValue clearVisuals(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("timeline/clearVisuals"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue registerMarkerClickCommand(const QString& command)
    {
        return hostCall(QStringLiteral("timeline/registerMarkerClickCommand"), QJsonObject{{QStringLiteral("command"), command}});
    }

    Q_INVOKABLE QJSValue play()
    {
        return hostCall(QStringLiteral("preview/play"));
    }

    Q_INVOKABLE QJSValue pause()
    {
        return hostCall(QStringLiteral("preview/pause"));
    }

    Q_INVOKABLE QJSValue stop()
    {
        return hostCall(QStringLiteral("preview/stop"));
    }

    Q_INVOKABLE QJSValue getState()
    {
        return hostCall(QStringLiteral("preview/getState"));
    }

    Q_INVOKABLE QJSValue setSpeed(double value)
    {
        return hostCall(QStringLiteral("preview/setSpeed"), QJsonObject{{QStringLiteral("value"), value}});
    }

    Q_INVOKABLE QJSValue addOverlay(const QJSValue& overlay)
    {
        return hostCall(QStringLiteral("preview/addOverlay"), runtime_->scriptValueToJson(overlay));
    }

    Q_INVOKABLE QJSValue clearOverlays(const QString& ownerId = {})
    {
        return hostCall(QStringLiteral("preview/clearOverlays"), QJsonObject{{QStringLiteral("ownerId"), ownerId}});
    }

    Q_INVOKABLE QJSValue onFrame(const QJSValue& callback)
    {
        return registerCallbackContribution(QStringLiteral("events/preview.onFrame"), callback);
    }

    Q_INVOKABLE QJSValue getPresets()
    {
        return hostCall(QStringLiteral("export/getPresets"));
    }

    Q_INVOKABLE QJSValue registerPreset(const QJSValue& preset)
    {
        return hostCall(QStringLiteral("export/registerPreset"), runtime_->scriptValueToJson(preset));
    }

    Q_INVOKABLE QJSValue startVideoExport(const QJSValue& options)
    {
        return hostCall(QStringLiteral("export/startVideoExport"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue startCoverExport(const QJSValue& options)
    {
        return hostCall(QStringLiteral("export/startCoverExport"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue registerBeforeExportHook(const QJSValue& hook)
    {
        return registerContribution(QStringLiteral("export/beforeHook"), hook);
    }

    Q_INVOKABLE QJSValue registerAfterExportHook(const QJSValue& hook)
    {
        return registerContribution(QStringLiteral("export/afterHook"), hook);
    }

    Q_INVOKABLE QJSValue registerCoverTemplate(const QJSValue& templateSpec)
    {
        return registerContribution(QStringLiteral("export/coverTemplate"), templateSpec);
    }

    Q_INVOKABLE QJSValue registerBatchJobProvider(const QJSValue& provider)
    {
        return registerContribution(QStringLiteral("export/batchJobProvider"), provider);
    }

    Q_INVOKABLE QJSValue readText(const QString& path)
    {
        return hostCall(QStringLiteral("fs/readText"), QJsonObject{{QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue writeText(const QString& path, const QString& text)
    {
        return hostCall(QStringLiteral("fs/writeText"), QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("text"), text}});
    }

    Q_INVOKABLE QJSValue exists(const QString& path)
    {
        return hostCall(QStringLiteral("fs/exists"), QJsonObject{{QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue listDir(const QString& path)
    {
        return hostCall(QStringLiteral("fs/listDir"), QJsonObject{{QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue getMediaInfo()
    {
        return hostCall(QStringLiteral("resources/getMediaInfo"));
    }

    Q_INVOKABLE QJSValue getAssetPath(const QString& id)
    {
        return hostCall(QStringLiteral("resources/getAssetPath"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue setAssetPath(const QString& id, const QString& path)
    {
        return hostCall(QStringLiteral("resources/setAssetPath"), QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue fetch(const QString& url, const QJSValue& options = {})
    {
        QJsonObject params = runtime_->scriptValueToJson(options);
        params.insert(QStringLiteral("url"), url);
        return hostCall(QStringLiteral("net/fetch"), params);
    }

    Q_INVOKABLE QJSValue download(const QString& url, const QString& targetPath)
    {
        return hostCall(QStringLiteral("net/download"), QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("targetPath"), targetPath}});
    }

    Q_INVOKABLE QJSValue get(const QString& key)
    {
        if (kind_ == Kind::Extensions) {
            return hostCall(QStringLiteral("extensions/get"), QJsonObject{{QStringLiteral("id"), key}});
        }
        return hostCall(QStringLiteral("settings/get"), QJsonObject{{QStringLiteral("key"), key}});
    }

    Q_INVOKABLE QJSValue set(const QString& key, const QJSValue& value)
    {
        return hostCall(QStringLiteral("settings/set"), QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), QJsonValue::fromVariant(value.toVariant())}});
    }

    Q_INVOKABLE QJSValue all()
    {
        return hostCall(QStringLiteral("extensions/all"));
    }

    Q_INVOKABLE QJSValue getExtension(const QString& id)
    {
        return hostCall(QStringLiteral("extensions/get"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue enable(const QString& id)
    {
        return hostCall(QStringLiteral("extensions/enable"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue disable(const QString& id)
    {
        return hostCall(QStringLiteral("extensions/disable"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue installFromFolder(const QString& path)
    {
        return hostCall(QStringLiteral("extensions/installFromFolder"), QJsonObject{{QStringLiteral("path"), path}});
    }

    Q_INVOKABLE QJSValue remove(const QString& id)
    {
        return hostCall(QStringLiteral("extensions/remove"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue registerSidebarView(const QJSValue& view)
    {
        return registerContribution(QStringLiteral("ui/sidebarView"), view);
    }

    Q_INVOKABLE QJSValue registerBottomTabView(const QJSValue& view)
    {
        return registerContribution(QStringLiteral("ui/bottomTabView"), view);
    }

    Q_INVOKABLE QJSValue registerPreferencesPage(const QJSValue& page)
    {
        return registerContribution(QStringLiteral("ui/preferencesPage"), page);
    }

    Q_INVOKABLE QJSValue registerToolbarButton(const QJSValue& button)
    {
        return registerContribution(QStringLiteral("ui/toolbarButton"), button);
    }

    Q_INVOKABLE QJSValue getContributions()
    {
        return hostCall(QStringLiteral("ui/getContributions"));
    }

    Q_INVOKABLE QJSValue withProgress(const QJSValue& options, const QJSValue& callback)
    {
        Q_UNUSED(callback);
        return hostCall(QStringLiteral("tasks/withProgress"), runtime_->scriptValueToJson(options));
    }

    Q_INVOKABLE QJSValue registerTask(const QJSValue& task)
    {
        return registerContribution(QStringLiteral("tasks/task"), task);
    }

    Q_INVOKABLE QJSValue reportProgress(const QString& taskId, int percent, const QString& message = {})
    {
        return hostCall(QStringLiteral("tasks/reportProgress"), QJsonObject{
            {QStringLiteral("taskId"), taskId},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("message"), message},
        });
    }

    Q_INVOKABLE QJSValue append(const QString& channel, const QString& message)
    {
        return hostCall(QStringLiteral("logs/append"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("message"), message}});
    }

    Q_INVOKABLE QJSValue getPath(const QString& channel = {})
    {
        return hostCall(QStringLiteral("logs/getPath"), QJsonObject{{QStringLiteral("channel"), channel}});
    }

    Q_INVOKABLE QJSValue open(const QString& channel = {})
    {
        return hostCall(QStringLiteral("logs/open"), QJsonObject{{QStringLiteral("channel"), channel}});
    }

    Q_INVOKABLE QJSValue list()
    {
        return hostCall(QStringLiteral("api/list"));
    }

    Q_INVOKABLE QJSValue has(const QString& id)
    {
        return hostCall(QStringLiteral("api/has"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue describe(const QString& id)
    {
        return hostCall(QStringLiteral("api/describe"), QJsonObject{{QStringLiteral("id"), id}});
    }

    Q_INVOKABLE QJSValue describeNamespace(const QString& namespaceId)
    {
        return hostCall(QStringLiteral("api/describeNamespace"), QJsonObject{{QStringLiteral("namespace"), namespaceId}});
    }

    Q_INVOKABLE QJSValue call(const QString& id, const QJSValue& params = {})
    {
        QJsonObject object = runtime_->scriptValueToJson(params);
        object.insert(QStringLiteral("id"), id);
        return hostCall(QStringLiteral("api/call"), object);
    }

    Q_INVOKABLE QJSValue invoke(const QString& method, const QJSValue& params = {})
    {
        QJsonObject object;
        object.insert(QStringLiteral("method"), method);
        object.insert(QStringLiteral("params"), runtime_->scriptValueToJson(params));
        return hostCall(QStringLiteral("api/invoke"), object);
    }

    Q_INVOKABLE QJSValue request(const QJSValue& request)
    {
        return hostCall(QStringLiteral("api/request"), runtime_->scriptValueToJson(request));
    }

    Q_INVOKABLE QJSValue log(const QString& message)
    {
        if (kind_ != Kind::Context || runtime_ == nullptr) {
            return QJSValue();
        }
        return runtime_->jsonToScriptValue(runtime_->requestHost(
            QStringLiteral("log"),
            QJsonObject{{QStringLiteral("message"), QStringLiteral("[%1] %2").arg(extensionId_, message)}}));
    }

private:
    QJSValue showMessage(const QString& severity, const QString& message)
    {
        if (kind_ != Kind::Window || runtime_ == nullptr) {
            return QJSValue();
        }
        return runtime_->jsonToScriptValue(runtime_->requestHost(
            QStringLiteral("window/showMessage"),
            QJsonObject{
                {QStringLiteral("severity"), severity},
                {QStringLiteral("message"), message},
            }));
    }

    QJSValue hostCall(const QString& method, QJsonObject params = {})
    {
        if (runtime_ == nullptr) {
            return QJSValue();
        }
        return runtime_->jsonToScriptValue(runtime_->requestHost(method, params));
    }

    QJSValue registerContribution(const QString& kind, const QJSValue& contribution)
    {
        QJsonObject params = runtime_->scriptValueToJson(contribution);
        params.insert(QStringLiteral("kind"), kind);
        return hostCall(QStringLiteral("contributions/register"), params);
    }

    QJSValue registerCallbackContribution(const QString& kind, const QJSValue& callback)
    {
        Q_UNUSED(callback);
        return hostCall(QStringLiteral("events/register"), QJsonObject{{QStringLiteral("kind"), kind}});
    }

    EmbeddedExtensionRuntime* runtime_ = nullptr;
    Kind kind_;
    QString extensionId_;
};

EmbeddedExtensionRuntime::EmbeddedExtensionRuntime(QObject* parent)
    : QObject(parent)
{
    auto* commands = new BridgeObject(this, BridgeObject::Kind::Commands);
    auto* window = new BridgeObject(this, BridgeObject::Kind::Window);
    auto* workspace = new BridgeObject(this, BridgeObject::Kind::Workspace);
    auto* diagnostics = new BridgeObject(this, BridgeObject::Kind::Diagnostics);
    auto* app = new BridgeObject(this, BridgeObject::Kind::App);
    auto* document = new BridgeObject(this, BridgeObject::Kind::Document);
    auto* editor = new BridgeObject(this, BridgeObject::Kind::Editor);
    auto* validation = new BridgeObject(this, BridgeObject::Kind::Validation);
    auto* analysis = new BridgeObject(this, BridgeObject::Kind::Analysis);
    auto* timeline = new BridgeObject(this, BridgeObject::Kind::Timeline);
    auto* preview = new BridgeObject(this, BridgeObject::Kind::Preview);
    auto* exportApi = new BridgeObject(this, BridgeObject::Kind::Export);
    auto* fs = new BridgeObject(this, BridgeObject::Kind::FileSystem);
    auto* resources = new BridgeObject(this, BridgeObject::Kind::Resources);
    auto* net = new BridgeObject(this, BridgeObject::Kind::Network);
    auto* settings = new BridgeObject(this, BridgeObject::Kind::Settings);
    auto* extensions = new BridgeObject(this, BridgeObject::Kind::Extensions);
    auto* ui = new BridgeObject(this, BridgeObject::Kind::Ui);
    auto* tasks = new BridgeObject(this, BridgeObject::Kind::Tasks);
    auto* logs = new BridgeObject(this, BridgeObject::Kind::Logs);
    auto* registryApi = new BridgeObject(this, BridgeObject::Kind::Api);

    QJSValue api = engine_.newObject();
    api.setProperty(QStringLiteral("commands"), engine_.newQObject(commands));
    api.setProperty(QStringLiteral("window"), engine_.newQObject(window));
    api.setProperty(QStringLiteral("workspace"), engine_.newQObject(workspace));
    api.setProperty(QStringLiteral("diagnostics"), engine_.newQObject(diagnostics));
    api.setProperty(QStringLiteral("app"), engine_.newQObject(app));
    api.setProperty(QStringLiteral("document"), engine_.newQObject(document));
    api.setProperty(QStringLiteral("editor"), engine_.newQObject(editor));
    api.setProperty(QStringLiteral("validation"), engine_.newQObject(validation));
    api.setProperty(QStringLiteral("analysis"), engine_.newQObject(analysis));
    api.setProperty(QStringLiteral("timeline"), engine_.newQObject(timeline));
    api.setProperty(QStringLiteral("preview"), engine_.newQObject(preview));
    api.setProperty(QStringLiteral("export"), engine_.newQObject(exportApi));
    api.setProperty(QStringLiteral("fs"), engine_.newQObject(fs));
    api.setProperty(QStringLiteral("resources"), engine_.newQObject(resources));
    api.setProperty(QStringLiteral("net"), engine_.newQObject(net));
    api.setProperty(QStringLiteral("settings"), engine_.newQObject(settings));
    api.setProperty(QStringLiteral("extensions"), engine_.newQObject(extensions));
    api.setProperty(QStringLiteral("ui"), engine_.newQObject(ui));
    api.setProperty(QStringLiteral("tasks"), engine_.newQObject(tasks));
    api.setProperty(QStringLiteral("logs"), engine_.newQObject(logs));
    api.setProperty(QStringLiteral("api"), engine_.newQObject(registryApi));
    engine_.globalObject().setProperty(QStringLiteral("miacode"), api);
}

EmbeddedExtensionRuntime::~EmbeddedExtensionRuntime()
{
    stop();
}

void EmbeddedExtensionRuntime::setHostRequestHandler(HostRequestHandler handler)
{
    hostRequestHandler_ = std::move(handler);
}

bool EmbeddedExtensionRuntime::start(const QJsonArray& extensions, QString* errorMessage)
{
    if (running_) {
        return true;
    }
    running_ = true;
    for (const QJsonValue& value : extensions) {
        const QJsonObject extension = value.toObject();
        const QString qualifiedId = extension.value(QStringLiteral("qualifiedId")).toString();
        if (!qualifiedId.isEmpty()) {
            extensionById_.insert(qualifiedId, extension);
        }
        const QJsonArray commands = extension.value(QStringLiteral("commands")).toArray();
        for (const QJsonValue& commandValue : commands) {
            const QString command = commandValue.toString().trimmed();
            if (!command.isEmpty()) {
                commandOwnerById_.insert(command, qualifiedId);
            }
        }
        if (extension.value(QStringLiteral("activateOnStartup")).toBool(false)) {
            QString error;
            if (!activateExtension(extension, &error)) {
                emit runtimeErrorMessage(error);
                if (errorMessage != nullptr && errorMessage->isEmpty()) {
                    *errorMessage = error;
                }
            }
        }
    }
    return true;
}

void EmbeddedExtensionRuntime::stop()
{
    if (!running_) {
        return;
    }
    deactivateExtensions();
    commandCallbacks_.clear();
    extensionById_.clear();
    commandOwnerById_.clear();
    loadedExports_.clear();
    loadedExtensionIds_.clear();
    running_ = false;
}

bool EmbeddedExtensionRuntime::isRunning() const
{
    return running_;
}

bool EmbeddedExtensionRuntime::executeCommand(const QString& command, QString* errorMessage)
{
    if (!running_) {
        const QString error = QStringLiteral("Embedded extension runtime is not running.");
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        emit runtimeErrorMessage(error);
        return false;
    }
    QJSValue callback = commandCallbacks_.value(command);
    if (!callback.isCallable()) {
        const QString owner = commandOwnerById_.value(command);
        if (!owner.isEmpty() && extensionById_.contains(owner)) {
            QString error;
            if (!activateExtension(extensionById_.value(owner), &error)) {
                if (errorMessage != nullptr) {
                    *errorMessage = error;
                }
                emit runtimeErrorMessage(error);
                return false;
            }
            callback = commandCallbacks_.value(command);
        }
    }
    if (!callback.isCallable()) {
        const QString owner = commandOwnerById_.value(command);
        const QString error = owner.isEmpty()
            ? QStringLiteral("Command not registered: %1").arg(command)
            : QStringLiteral("Command not registered: %1. Extension '%2' activated, but it did not call miacode.commands.registerCommand for this command.")
                  .arg(command, owner);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        emit runtimeErrorMessage(error);
        return false;
    }
    currentCallExtensionId_ = commandOwnerById_.value(command);
    QJSValue result = callback.call();
    currentCallExtensionId_.clear();
    if (result.isError()) {
        const QString error = QStringLiteral("Command '%1' failed: %2").arg(command, describeError(result));
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        emit runtimeErrorMessage(error);
        return false;
    }
    return true;
}

QJsonObject EmbeddedExtensionRuntime::requestHost(const QString& method, const QJsonObject& params)
{
    if (!hostRequestHandler_) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("No host request handler is installed.")},
        };
    }
    QJsonObject enriched = params;
    const QString extensionId = currentCallExtensionId_.isEmpty() ? currentExtensionId_ : currentCallExtensionId_;
    if (!extensionId.isEmpty() && !enriched.contains(QStringLiteral("extensionId"))) {
        enriched.insert(QStringLiteral("extensionId"), extensionId);
    }
    return hostRequestHandler_(method, enriched);
}

QJsonObject EmbeddedExtensionRuntime::scriptValueToJson(const QJSValue& value)
{
    if (!value.isObject()) {
        return QJsonObject();
    }
    const QJsonDocument document = QJsonDocument::fromVariant(value.toVariant());
    return document.isObject() ? document.object() : QJsonObject();
}

QJSValue EmbeddedExtensionRuntime::jsonToScriptValue(const QJsonObject& object)
{
    return engine_.toScriptValue(object.toVariantMap());
}

QJSValue EmbeddedExtensionRuntime::disposableValue()
{
    return engine_.evaluate(QStringLiteral("({ dispose: function() {} })"));
}

void EmbeddedExtensionRuntime::registerCommand(const QString& command, const QJSValue& callback)
{
    if (command.trimmed().isEmpty() || !callback.isCallable()) {
        emit runtimeErrorMessage(QStringLiteral("registerCommand requires a command id and callback."));
        return;
    }
    commandCallbacks_.insert(command, callback);
    requestHost(QStringLiteral("commands/register"), QJsonObject{
        {QStringLiteral("command"), command},
        {QStringLiteral("extensionId"), currentExtensionId_},
    });
    emit extensionCommandRegistered(command);
}

bool EmbeddedExtensionRuntime::activateExtension(const QJsonObject& extension, QString* errorMessage)
{
    const QString qualifiedId = extension.value(QStringLiteral("qualifiedId")).toString();
    if (loadedExports_.contains(qualifiedId)) {
        return true;
    }
    const QString main = extension.value(QStringLiteral("main")).toString();
    QFile file(main);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Cannot read extension entry '%1': %2").arg(main, file.errorString());
        }
        return false;
    }

    const QString source = QString::fromUtf8(file.readAll());
    QJSValue module = engine_.newObject();
    module.setProperty(QStringLiteral("exports"), engine_.newObject());
    QJSValue context = engine_.newObject();
    context.setProperty(QStringLiteral("extensionPath"), extension.value(QStringLiteral("rootPath")).toString());
    context.setProperty(QStringLiteral("subscriptions"), engine_.newArray());
    engine_.globalObject().setProperty(
        QStringLiteral("__miacode_context_bridge"),
        engine_.newQObject(new BridgeObject(this, BridgeObject::Kind::Context, qualifiedId)));
    context.setProperty(
        QStringLiteral("log"),
        engine_.evaluate(QStringLiteral("(function(message){ return __miacode_context_bridge.log(String(message)); })")));

    engine_.globalObject().setProperty(QStringLiteral("__miacode_module"), module);
    engine_.globalObject().setProperty(QStringLiteral("__miacode_context"), context);
    const QString wrapped = QStringLiteral(
        "(function(){"
        "var module = __miacode_module;"
        "var exports = module.exports;"
        "(function(module, exports, miacode, context){\n%1\n})(module, exports, miacode, __miacode_context);"
        "return module.exports;"
        "})()")
                                .arg(source);

    currentExtensionId_ = qualifiedId;
    QJSValue exports = engine_.evaluate(wrapped, main);
    currentExtensionId_.clear();
    engine_.globalObject().deleteProperty(QStringLiteral("__miacode_module"));
    engine_.globalObject().deleteProperty(QStringLiteral("__miacode_context"));
    engine_.globalObject().deleteProperty(QStringLiteral("__miacode_context_bridge"));

    if (exports.isError()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Activation load failed for %1: %2").arg(qualifiedId, describeError(exports));
        }
        return false;
    }
    loadedExports_.insert(qualifiedId, exports);
    loadedExtensionIds_.append(qualifiedId);

    QJSValue activate = exports.property(QStringLiteral("activate"));
    if (activate.isCallable()) {
        QJSValue result = activate.callWithInstance(exports, QJSValueList{context});
        if (result.isError()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Activation failed for %1: %2").arg(qualifiedId, describeError(result));
            }
            return false;
        }
    }
    requestHost(QStringLiteral("log"), QJsonObject{{QStringLiteral("message"), QStringLiteral("Activated %1").arg(qualifiedId)}});
    return true;
}

void EmbeddedExtensionRuntime::deactivateExtensions()
{
    for (auto it = loadedExtensionIds_.crbegin(); it != loadedExtensionIds_.crend(); ++it) {
        const QString extensionId = *it;
        QJSValue exports = loadedExports_.value(extensionId);
        QJSValue deactivate = exports.property(QStringLiteral("deactivate"));
        if (!deactivate.isCallable()) {
            continue;
        }
        QJSValue result = deactivate.callWithInstance(exports);
        if (result.isError()) {
            emit runtimeErrorMessage(QStringLiteral("Deactivate failed for %1: %2").arg(extensionId, describeError(result)));
        }
    }
}

QString EmbeddedExtensionRuntime::describeError(const QJSValue& value) const
{
    const QString stack = value.property(QStringLiteral("stack")).toString();
    if (!stack.isEmpty()) {
        return stack;
    }
    return value.toString();
}

}  // namespace miacode::extensions

#include "EmbeddedExtensionRuntime.moc"
