#pragma once

#include <QRect>
#include <QString>

namespace PreviewIntegration {

enum class PlayheadParseResult {
    NotPlayheadEvent,
    PlayheadEventNoSecond,
    Parsed,
};

struct SideBySideLayout {
    QRect previewRect;
    QRect editorRect;
};

PlayheadParseResult parsePlayheadEvent(const QString& line, double* secondOut);
SideBySideLayout computeSideBySideLayout(const QRect& workArea);
bool placePreviewWindow(qint64 processId, const QRect& previewRect, QString* detailOut);

}  // namespace PreviewIntegration

