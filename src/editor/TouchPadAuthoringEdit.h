#pragma once

#include <QString>

class QTextDocument;
class QTextCursor;

namespace miacode::editor {

struct TouchPadAuthoringEditPlan {
    int tokenStart = 0;
    int insertionPosition = 0;
    int removalLength = 0;
    QString insertionText;
    bool valid = false;
};

TouchPadAuthoringEditPlan planTouchPadAuthoringEdit(
    const QString& text,
    int cursorPosition,
    const QString& pad,
    QChar separator);

bool applyTouchPadAuthoringEdit(
    QTextDocument* document,
    QTextCursor* cursor,
    const TouchPadAuthoringEditPlan& plan);

} // namespace miacode::editor
