#pragma once

#include "ChartDropImportService.h"
#include "core/chart/transform/ChartNormalization.h"

#include <QString>
#include <QVariantList>

#include <functional>

namespace miacode::v2 {

    // The document work that still needs the window.
    //
    // ChartWorkspace is the document. This is recent files, autosave-backup lists,
    // chart-normalization options, editor navigation, and the handler hooks a
    // widget-side path uses when the QML layer must answer.
//
// Deliberately Qt Widgets-free.
class DocumentBridge
{
public:
    // What kind of transaction produced the committed document. The window uses
    // it to decide how much of the timeline and preview to rebuild.
    enum class CommitKind {
        Incremental,
        DifficultySelection,
        Structure,
        SourceReplacement,
        Open,
        SavePoint,
    };

    virtual ~DocumentBridge() = default;

    virtual QString sourceText() const = 0;
    virtual QString filePath() const = 0;
    virtual int activeDifficultyId() const = 0;

    // ChartWorkspace has already committed. The window refreshes timeline and
    // preview from that workspace; it does not keep a second SimaiDocument.
    virtual bool applyCommittedDocument(const QString& sourceText, const QString& filePath,
                                        int activeDifficultyId, bool dirty, quint64 revision,
                                        CommitKind kind, bool usedSystemEncoding) = 0;

    // ---- lists the shell shows ----
    virtual QVariantList recentDocumentEntries() = 0;
    virtual void noteRecentDocument(const QString& path) = 0;
    virtual QVariantList backupDocumentEntries() = 0;
    virtual void restoreBackupDocument(const QString& path) = 0;

    // ---- chart normalization options ----
    virtual miacode::chart_transform::ChartNormalizationOptions normalizationOptions() const = 0;
    virtual void setNormalizationOptions(
        const miacode::chart_transform::ChartNormalizationOptions& options) = 0;

    // Move the editor caret / selection, optionally centring the view.
    virtual bool requestEditorNavigation(int line, int column, int endLine, int endColumn,
                                         bool selectToken, bool focusEditor,
                                         bool centerView) = 0;

    // ---- callbacks the window invokes when a non-QML path needs an answer ----
    //
    // These are the reverse direction: a widget-side flow (native File/Open, a
    // menu action, crash recovery) asks the QML layer to save, to replace the
    // chart text, or to decide whether leaving the document is allowed. They
    // are handlers rather than signals because each one needs a RESULT.
    virtual void setDocumentSaveHandler(std::function<bool(const QString&)> handler) = 0;
    virtual void setChartTextHandler(std::function<bool(const QString&)> handler) = 0;
    virtual void setLeaveDocumentHandler(
        std::function<void(std::function<void(bool)>)> handler) = 0;
    // Asks whatever handler is installed; the continuation runs once.
    virtual void requestLeaveDocument(std::function<void(bool)> onDecided) = 0;

    // ---- chart drop ----
    //
    // Dropping audio onto the root window creates a chart beside it. This is
    // document work, not window work: the bootstrap only happens to be where
    // the OS drag route lands.
    virtual void importDroppedAudio(const QStringList& audioPaths, quint64 requestId,
                                    quint64 generation,
                                    ChartDropImportService::Completion completion) = 0;
    // Invalidates any in-flight import so a late callback cannot reach a
    // half-destroyed shell.
    virtual void releaseChartDropImport() = 0;

protected:
    DocumentBridge() = default;
    DocumentBridge(const DocumentBridge&) = default;
    DocumentBridge& operator=(const DocumentBridge&) = default;
};

}  // namespace miacode::v2
