#include <QFile>
#include <QString>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

QString readRawSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// Comment-stripped view of a source file. Without this a contract can be
// "satisfied" by a commented-out line: contains() cannot tell code from prose,
// and a disabled call still reads as present.
QString stripComments(const QString& source)
{
    QString out;
    out.reserve(source.size());
    bool inLine = false;
    bool inBlock = false;
    for (int i = 0; i < source.size(); ++i) {
        const QChar c = source.at(i);
        const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();
        if (inLine) {
            if (c == QLatin1Char('\n')) {
                inLine = false;
                out.append(c);
            }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlock = false;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLine = true;
            ++i;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlock = true;
            ++i;
            continue;
        }
        out.append(c);
    }
    return out;
}

QString readSource(const QString& relativePath)
{
    return stripComments(readRawSource(relativePath));
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

// Body of the first brace-balanced definition whose signature line contains
// `signature`. Pinning containment inside a specific function survives
// reformatting, which a whole-file contains() check does not.
QString functionBody(const QString& source, const QString& signature)
{
    const int signatureAt = source.indexOf(signature);
    if (signatureAt < 0) {
        return {};
    }
    const int open = source.indexOf(QLatin1Char('{'), signatureAt);
    if (open < 0) {
        return {};
    }
    int depth = 0;
    for (int i = open; i < source.size(); ++i) {
        const QChar c = source.at(i);
        if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}')) {
            if (--depth == 0) {
                return source.mid(open, i - open + 1);
            }
        }
    }
    return {};
}

// Both needles must exist and appear in this order. A bare indexOf comparison
// silently passes when a needle is missing, because indexOf() returns -1 and
// -1 sorts before every real position.
bool orderedBefore(const QString& body, const QString& first, const QString& second)
{
    const int firstAt = body.indexOf(first);
    const int secondAt = body.indexOf(second);
    return firstAt >= 0 && secondAt >= 0 && firstAt < secondAt;
}

bool verifySelectionRangeExportContract(QTextStream& err)
{
    const QString syncHeader = readSource(QStringLiteral("src/app/v2/EditorSyncController.h"));
    const QString syncImpl = readSource(QStringLiteral("src/app/v2/EditorSyncController.cpp"));
    const QString sessionHeader =
        readSource(QStringLiteral("src/app/qml_ui/export/QmlExportSession.h"));
    const QString sessionImpl =
        readSource(QStringLiteral("src/app/qml_ui/export/QmlExportSession.cpp"));
    const QString pageHost = readSource(QStringLiteral("src/app/qml_ui/QmlEditorPageHost.cpp"));
    const QString sourceEditor =
        readSource(QStringLiteral("src/app/qml_ui/editor/SourceEditor.qml"));
    const QString runtimeForwarding =
        readSource(QStringLiteral("src/app/runtime/playback/SessionForwarding.TimelineFlow.cpp"));
    const QString timelineFlow =
        readSource(QStringLiteral("src/app/runtime/playback/TimelineFlow.cpp"));
    const QString timelineModel = readSource(QStringLiteral("src/timeline/TimelineQuickModel.h"));

    bool ok = require(
        !syncHeader.isEmpty() && !syncImpl.isEmpty() && !sessionHeader.isEmpty()
            && !sessionImpl.isEmpty() && !pageHost.isEmpty() && !sourceEditor.isEmpty()
            && !runtimeForwarding.isEmpty() && !timelineFlow.isEmpty() && !timelineModel.isEmpty(),
        QStringLiteral("selection-range-export contract sources are readable"),
        err);
    if (!ok) {
        return false;
    }

    // The resolver takes plain text as well, so runtime hosts holding the live
    // chart text never construct a QTextDocument just to reach it.
    ok &= require(
        timelineModel.count(QStringLiteral("resolveExportRangeForSelection")) >= 2
            && timelineModel.contains(QStringLiteral("const QString& text")),
        QStringLiteral("TimelineQuickModel offers a text-based export-range resolver"),
        err);

    // The editor->runtime hop reuses the (difficultyId, revision) gate rather
    // than inventing a second staleness guard.
    ok &= require(
        syncHeader.contains(QStringLiteral("Q_INVOKABLE bool requestSelectionRangeExport"))
            && syncHeader.contains(QStringLiteral("void selectionRangeExportRequested(")),
        QStringLiteral("EditorSyncController exposes the selection-range export request and signal"),
        err);
    const QString requestBody =
        functionBody(syncImpl, QStringLiteral("EditorSyncController::requestSelectionRangeExport"));
    ok &= require(
        requestBody.contains(QStringLiteral("readinessAccepts(difficultyId, revision)")),
        QStringLiteral("the export request is refused when the editor identity is stale"),
        err);
    const QString deliveryBody = functionBody(
        syncImpl, QStringLiteral("EditorSyncController::scheduleSelectionRangeExportDelivery"));
    ok &= require(
        deliveryBody.contains(QStringLiteral("readinessAccepts(")),
        QStringLiteral("the queued export request revalidates identity before delivery"),
        err);
    ok &= require(
        functionBody(syncImpl, QStringLiteral("EditorSyncController::setEditorReadiness"))
            .contains(QStringLiteral("selectionRangeExportPending_ = false")),
        QStringLiteral("a readiness change drops any queued export request"),
        err);

    // Staging, not direct assignment: seedFromDifficulty() rewrites the whole
    // task (range included) on every page entry, so a range written before the
    // seed would be silently discarded.
    ok &= require(
        sessionHeader.contains(QStringLiteral("void requestSelectionRangeExport(double startSecond, double endSecond);"))
            && !sessionHeader.contains(QStringLiteral("Q_INVOKABLE void requestSelectionRangeExport")),
        QStringLiteral("the export session stages the range through a runtime-only entry point"),
        err);
    const QString seedBody =
        functionBody(sessionImpl, QStringLiteral("QmlExportSession::seedFromDifficulty"));
    ok &= require(
        seedBody.contains(QStringLiteral("applyPendingSelectionRangeExport()")),
        QStringLiteral("the staged range is applied from inside seedFromDifficulty"),
        err);
    const QString applyBody =
        functionBody(sessionImpl, QStringLiteral("QmlExportSession::applyPendingSelectionRangeExport"));
    ok &= require(
        orderedBefore(applyBody,
                      QStringLiteral("hasPendingSelectionRangeExport_ = false"),
                      QStringLiteral("setExportRangeSeconds(")),
        QStringLiteral("the staged range is consumed exactly once"),
        err);

    // requestPageSwitch() is asynchronous, so a refused switch arrives as a
    // signal; without this the stale range would ambush a later page entry.
    ok &= require(
        sessionHeader.contains(QStringLiteral("void clearPendingSelectionRangeExport();"))
            && pageHost.contains(QStringLiteral("QmlEditorPageHost::navigationRejected"))
            && pageHost.contains(QStringLiteral("clearPendingSelectionRangeExport()")),
        QStringLiteral("a rejected page switch drops the staged range"),
        err);

    // One causal chain: the runtime stages the range and only then asks for the
    // page. The menu must not race it by navigating on its own.
    const QString runtimeBody =
        functionBody(runtimeForwarding, QStringLiteral("Session::requestSelectionRangeExport"));
    ok &= require(
        orderedBefore(runtimeBody,
                      QStringLiteral("requestSelectionRangeExport(startSecond, endSecond)"),
                      QStringLiteral("selectionRangeExportPageRequested")),
        QStringLiteral("the runtime stages the range before requesting the export page"),
        err);
    ok &= require(
        functionBody(timelineFlow,
                     QStringLiteral("PlaybackCoordinator::resolveExportRangeForSelection"))
            .contains(QStringLiteral("timelineQuickModel_.resolveExportRangeForSelection")),
        QStringLiteral("the coordinator resolves the range from the live timeline model"),
        err);

    const QString menuAction = functionBody(sourceEditor, QStringLiteral("function exportSelectionRange"));
    ok &= require(
        sourceEditor.contains(QStringLiteral("UiText.text(\"导出选区\")"))
            && sourceEditor.contains(QStringLiteral("onTriggered: root.exportSelectionRange()")),
        QStringLiteral("the editor context menu offers the selection export action"),
        err);
    ok &= require(
        menuAction.contains(QStringLiteral("syncController.requestSelectionRangeExport"))
            && !menuAction.contains(QStringLiteral("openVideoExportPage")),
        QStringLiteral("the menu action defers navigation to the runtime instead of racing it"),
        err);

    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    const bool ok = verifySelectionRangeExportContract(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_selection_range_export_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
