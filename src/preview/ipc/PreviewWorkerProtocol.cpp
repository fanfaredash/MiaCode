#include "preview/ipc/PreviewWorkerProtocol.h"

#include <QJsonDocument>
#include <QTextStream>

namespace miacode::preview::ipc {

namespace {

QByteArray jsonLine(const QJsonObject& object)
{
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    return bytes;
}

}  // namespace

QByteArray buildAttachCommand(quint64 editorHwnd, quint32 parentPid,
                              const QString& sessionId, const QString& logDirectory,
                              const QString& snapshotRingShmKey,
                              int snapshotSlotByteSize, int snapshotSlotCount)
{
    QJsonObject object;
    object.insert(QStringLiteral("cmd"), QString::fromLatin1(kCmdAttach));
    object.insert(QStringLiteral("protocol"), kPreviewWorkerProtocolVersion);
    object.insert(QStringLiteral("editor_hwnd"), static_cast<qint64>(editorHwnd));
    object.insert(QStringLiteral("parent_pid"), static_cast<qint64>(parentPid));
    object.insert(QStringLiteral("session_id"), sessionId);
    if (!logDirectory.isEmpty()) {
        object.insert(QStringLiteral("log_dir"), logDirectory);
    }
    if (!snapshotRingShmKey.isEmpty()) {
        object.insert(QStringLiteral("snapshot_shm_key"), snapshotRingShmKey);
        object.insert(QStringLiteral("snapshot_slot_bytes"), snapshotSlotByteSize);
        object.insert(QStringLiteral("snapshot_slot_count"), snapshotSlotCount);
    }
    return jsonLine(object);
}

QByteArray buildShutdownCommand()
{
    QJsonObject object;
    object.insert(QStringLiteral("cmd"), QString::fromLatin1(kCmdShutdown));
    return jsonLine(object);
}

QByteArray buildSetVisualTransformCommand(int xPx, int yPx, int displayWPx, int displayHPx)
{
    QJsonObject object;
    object.insert(QStringLiteral("cmd"), QString::fromLatin1(kCmdSetVisualTransform));
    object.insert(QStringLiteral("x"), xPx);
    object.insert(QStringLiteral("y"), yPx);
    object.insert(QStringLiteral("display_w"), displayWPx);
    object.insert(QStringLiteral("display_h"), displayHPx);
    return jsonLine(object);
}

QByteArray buildResizeCommand(int wPx, int hPx)
{
    QJsonObject object;
    object.insert(QStringLiteral("cmd"), QString::fromLatin1(kCmdResize));
    object.insert(QStringLiteral("w"), wPx);
    object.insert(QStringLiteral("h"), hPx);
    return jsonLine(object);
}

void writeWorkerEvent(const QJsonObject& event)
{
    QTextStream out(stdout);
    out << QJsonDocument(event).toJson(QJsonDocument::Compact) << '\n';
    out.flush();
}

void emitWorkerReadyEvent()
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), QString::fromLatin1(kEvtWorkerReady));
    object.insert(QStringLiteral("protocol"), kPreviewWorkerProtocolVersion);
    writeWorkerEvent(object);
}

void emitAttachedEvent(const QString& sessionId, quint64 popupHwnd)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), QString::fromLatin1(kEvtAttached));
    object.insert(QStringLiteral("session_id"), sessionId);
    object.insert(QStringLiteral("popup_hwnd"), static_cast<qint64>(popupHwnd));
    writeWorkerEvent(object);
}

void emitDeviceRemovedEvent(const QString& reason, quint64 framesTotal)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), QString::fromLatin1(kEvtDeviceRemoved));
    object.insert(QStringLiteral("reason"), reason);
    object.insert(QStringLiteral("frames_total"), static_cast<qint64>(framesTotal));
    writeWorkerEvent(object);
}

void emitFatalEvent(const QString& tag, const QString& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), QString::fromLatin1(kEvtFatal));
    object.insert(QStringLiteral("tag"), tag);
    object.insert(QStringLiteral("message"), message);
    writeWorkerEvent(object);
}

}  // namespace miacode::preview::ipc
