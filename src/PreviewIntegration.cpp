#include "PreviewIntegration.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace PreviewIntegration {

PlayheadParseResult parsePlayheadEvent(const QString& line, double* secondOut)
{
    QString jsonText = line.trimmed();
    const int jsonStart = jsonText.indexOf('{');
    const int jsonEnd = jsonText.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart) {
        jsonText = jsonText.mid(jsonStart, jsonEnd - jsonStart + 1);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject payload = doc.object();
            if (payload.value("event").toString() == "playhead") {
                const QJsonValue secondValue = payload.value("second");
                if (secondValue.isDouble()) {
                    if (secondOut != nullptr) {
                        *secondOut = secondValue.toDouble();
                    }
                    return PlayheadParseResult::Parsed;
                }
                return PlayheadParseResult::PlayheadEventNoSecond;
            }
            return PlayheadParseResult::NotPlayheadEvent;
        }
    }

    if (line.contains("\"event\"") && line.contains("playhead")) {
        static const QRegularExpression secondPattern("\"second\"\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)");
        const QRegularExpressionMatch match = secondPattern.match(line);
        if (match.hasMatch()) {
            if (secondOut != nullptr) {
                *secondOut = match.captured(1).toDouble();
            }
            return PlayheadParseResult::Parsed;
        }
        return PlayheadParseResult::PlayheadEventNoSecond;
    }

    return PlayheadParseResult::NotPlayheadEvent;
}

SideBySideLayout computeSideBySideLayout(const QRect& workArea)
{
    constexpr int kGap = 26;
    const int previewWidth = qBound(520, static_cast<int>(workArea.width() * 0.34), 900);

    SideBySideLayout layout;
    layout.previewRect = QRect(workArea.left(), workArea.top(), previewWidth, workArea.height());
    layout.editorRect = QRect(
        layout.previewRect.right() + 1 + kGap,
        workArea.top(),
        qMax(640, workArea.width() - previewWidth - kGap),
        workArea.height()
    );
    return layout;
}

#ifdef Q_OS_WIN
namespace {
BOOL CALLBACK findByCaptionProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    wchar_t title[512] = {0};
    if (GetWindowTextW(hwnd, title, 512) <= 0) {
        return TRUE;
    }
    const QString caption = QString::fromWCharArray(title);
    if (caption.startsWith("MaiMuri Preview", Qt::CaseInsensitive)) {
        auto* outHandle = reinterpret_cast<HWND*>(lParam);
        *outHandle = hwnd;
        return FALSE;
    }
    return TRUE;
}

struct WindowByPidSearch {
    DWORD pid = 0;
    HWND handle = nullptr;
};

BOOL CALLBACK findByPidProc(HWND hwnd, LPARAM lParam)
{
    auto* search = reinterpret_cast<WindowByPidSearch*>(lParam);
    if (search == nullptr || search->pid == 0) {
        return FALSE;
    }
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != search->pid) {
        return TRUE;
    }
    search->handle = hwnd;
    return FALSE;
}

HWND findPreviewWindowByPid(qint64 processId)
{
    if (processId <= 0) {
        return nullptr;
    }
    WindowByPidSearch search;
    search.pid = static_cast<DWORD>(processId);
    EnumWindows(findByPidProc, reinterpret_cast<LPARAM>(&search));
    return search.handle;
}

HWND findPreviewWindowByCaption()
{
    HWND handle = nullptr;
    EnumWindows(findByCaptionProc, reinterpret_cast<LPARAM>(&handle));
    return handle;
}
}  // namespace
#endif

bool placePreviewWindow(qint64 processId, const QRect& previewRect, QString* detailOut)
{
#ifdef Q_OS_WIN
    HWND handle = findPreviewWindowByPid(processId);
    if (handle == nullptr) {
        handle = findPreviewWindowByCaption();
    }
    if (handle == nullptr) {
        if (detailOut != nullptr) {
            *detailOut = QString("window_not_found pid=%1").arg(processId);
        }
        return false;
    }

    const BOOL ok = MoveWindow(
        handle,
        previewRect.left(),
        previewRect.top(),
        previewRect.width(),
        previewRect.height(),
        TRUE
    );
    if (!ok) {
        if (detailOut != nullptr) {
            *detailOut = QString("MoveWindow failed pid=%1 err=%2").arg(processId).arg(GetLastError());
        }
        return false;
    }
    if (detailOut != nullptr) {
        *detailOut = QString("ok hwnd=0x%1 pid=%2 rect=[%3,%4 %5x%6]")
                         .arg(reinterpret_cast<quintptr>(handle), 0, 16)
                         .arg(processId)
                         .arg(previewRect.left())
                         .arg(previewRect.top())
                         .arg(previewRect.width())
                         .arg(previewRect.height());
    }
    return true;
#else
    Q_UNUSED(processId);
    Q_UNUSED(previewRect);
    if (detailOut != nullptr) {
        *detailOut = QString("unsupported_platform");
    }
    return false;
#endif
}

}  // namespace PreviewIntegration
