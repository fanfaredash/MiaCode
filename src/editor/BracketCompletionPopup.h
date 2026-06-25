#pragma once

#include <QListWidget>
#include <QString>
#include <QStringList>

class QPoint;

// Non-focusing dropdown shown beneath the caret while the user is filling a
// just-opened bracket. It is a pure display surface: it never steals focus from
// the editor (Qt::Tool + WA_ShowWithoutActivating), so the editor keeps
// receiving every keystroke and drives selection/accept/dismiss itself. The
// only thing it emits back is a mouse press on a row.
class BracketCompletionPopup : public QListWidget
{
    Q_OBJECT

public:
    explicit BracketCompletionPopup(QWidget* parent = nullptr);

    // Populate with the full candidate set and show anchored at globalTopLeft
    // (the point just below the caret). Resets the master list used for filtering.
    void showCandidates(const QStringList& items, const QPoint& globalTopLeft);

    // Re-anchor / re-size in place (after the caret moved while filtering).
    void moveToAnchor(const QPoint& globalTopLeft);

    // Move the highlighted row by delta, clamped to the visible range.
    void moveSelection(int delta);

    // Text of the highlighted row, or empty when there is none.
    QString currentCandidate() const;

    // Keep only the candidates that start with prefix (against the master list).
    // Returns false when nothing matches (caller should then close the popup).
    bool applyFilter(const QString& prefix);

signals:
    void candidateActivated(const QString& text);

private:
    void relayout(const QPoint& globalTopLeft);

    QStringList allItems_;
};
