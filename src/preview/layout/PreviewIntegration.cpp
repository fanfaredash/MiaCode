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
    const int previewSide = qMin(previewWidth, workArea.height());
    const int previewTop = workArea.top() + qMax(0, (workArea.height() - previewSide) / 2);

    SideBySideLayout layout;
    layout.previewRect = QRect(workArea.left(), previewTop, previewSide, previewSide);
    layout.editorRect = QRect(
        layout.previewRect.right() + 1 + kGap,
        workArea.top(),
        qMax(640, workArea.width() - previewSide - kGap),
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

QString sanitizeWindowText(QString text)
{
    text.replace('\r', QStringLiteral("\\r"));
    text.replace('\n', QStringLiteral("\\n"));
    if (text.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    constexpr int kMaxLength = 96;
    if (text.size() > kMaxLength) {
        text = text.left(kMaxLength) + QStringLiteral("...");
    }
    return text;
}

QString describeWindowForDetail(HWND hwnd)
{
    if (hwnd == nullptr) {
        return QStringLiteral("hwnd=0x0");
    }

    wchar_t classNameBuf[256] = {};
    const int classNameLen = GetClassNameW(hwnd, classNameBuf, 256);
    const QString className = classNameLen > 0
        ? sanitizeWindowText(QString::fromWCharArray(classNameBuf, classNameLen))
        : QStringLiteral("(none)");

    wchar_t titleBuf[512] = {};
    const int titleLen = GetWindowTextW(hwnd, titleBuf, 512);
    const QString title = titleLen > 0
        ? sanitizeWindowText(QString::fromWCharArray(titleBuf, titleLen))
        : QStringLiteral("(empty)");

    RECT rect{};
    const BOOL hasRect = GetWindowRect(hwnd, &rect);
    const int rectX = hasRect ? rect.left : -1;
    const int rectY = hasRect ? rect.top : -1;
    const int rectW = hasRect ? (rect.right - rect.left) : -1;
    const int rectH = hasRect ? (rect.bottom - rect.top) : -1;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);

    return QString(
               "hwnd=0x%1 class=%2 title=%3 pid=%4 vis=%5 iconic=%6 zoomed=%7 owner=0x%8 root_owner=0x%9 "
               "rect=[%10,%11 %12x%13]"
           )
        .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
        .arg(className)
        .arg(title)
        .arg(pid)
        .arg(IsWindowVisible(hwnd) ? 1 : 0)
        .arg(IsIconic(hwnd) ? 1 : 0)
        .arg(IsZoomed(hwnd) ? 1 : 0)
        .arg(reinterpret_cast<quintptr>(owner), 0, 16)
        .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
        .arg(rectX)
        .arg(rectY)
        .arg(rectW)
        .arg(rectH);
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
    QString handleSource = "pid";
    if (handle == nullptr) {
        const HWND captionHandle = findPreviewWindowByCaption();
        if (captionHandle != nullptr) {
            DWORD captionPid = 0;
            GetWindowThreadProcessId(captionHandle, &captionPid);
            const DWORD expectedPid = processId > 0 ? static_cast<DWORD>(processId) : 0;
            if (processId <= 0 || captionPid == expectedPid) {
                handle = captionHandle;
                handleSource = "caption";
            } else if (detailOut != nullptr) {
                *detailOut = QString("caption_pid_mismatch expected=%1 actual=%2")
                                 .arg(processId)
                                 .arg(captionPid);
            }
        }
    }
    if (handle == nullptr) {
        if (detailOut != nullptr) {
            const QString suffix = (detailOut->isEmpty() ? QString() : (" " + *detailOut));
            *detailOut = QString("window_not_found pid=%1%2").arg(processId).arg(suffix);
        }
        return false;
    }

    const QString beforeMove = describeWindowForDetail(handle);
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
            *detailOut = QString("MoveWindow failed pid=%1 err=%2 before={%3}")
                             .arg(processId)
                             .arg(GetLastError())
                             .arg(beforeMove);
        }
        return false;
    }
    const QString afterMove = describeWindowForDetail(handle);
    if (detailOut != nullptr) {
        *detailOut = QString("ok src=%1 hwnd=0x%2 pid=%3 target=[%4,%5 %6x%7] before={%8} after={%9}")
                         .arg(handleSource)
                         .arg(reinterpret_cast<quintptr>(handle), 0, 16)
                         .arg(processId)
                         .arg(previewRect.left())
                         .arg(previewRect.top())
                         .arg(previewRect.width())
                         .arg(previewRect.height())
                         .arg(beforeMove)
                         .arg(afterMove);
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
