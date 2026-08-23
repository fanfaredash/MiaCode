#pragma once

#include <QChar>
#include <QString>
#include <QStringList>
#include <Qt>

namespace miacode::editor {

// Value-only smart-input contract shared by the widget event paths. Positions
// are UTF-16 offsets, matching QTextCursor::position().
struct SimaiTextEditRequest {
    QString text;
    int anchor = 0;
    int position = 0;
    QString input;
    int key = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool isImeCommit = false;
    bool halfWidthInputEnabled = true;
    bool overwriteMode = false;
    bool autoCompletionEnabled = true;
    QString wholeBpm;
    // The widget owns popup display, but the policy needs this context to
    // distinguish a literal filter character from a new completion entry.
    bool completionActive = false;
};

struct SimaiTextEditTransaction {
    QString text;
    int anchor = 0;
    int position = 0;
    bool hasEdit = false;
    bool undoGroup = false;
    // The exact original-document span and replacement needed by a widget
    // adapter. This avoids reconstructing an edit by diffing whole documents.
    int replacementStart = 0;
    int replacementEnd = 0;
    QString replacementText;
    bool insertsBlock = false;
};

struct SimaiCompletionSession {
    bool active = false;
    QChar opening;
    bool closingPresent = false;
    int startPosition = -1;
    QStringList candidates;
};

struct SimaiTextEditResult {
    bool consumed = false;
    SimaiTextEditTransaction transaction;
    SimaiCompletionSession completion;
};

SimaiTextEditResult applySimaiTextEditPolicy(const SimaiTextEditRequest& request);

} // namespace miacode::editor
