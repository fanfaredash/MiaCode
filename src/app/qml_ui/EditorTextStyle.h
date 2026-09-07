#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QtQmlIntegration>

// Applies the 行距 preference to the source editor's QTextDocument.
//
// Qt Quick's TextEdit has no line-height of its own, so the spacing lives where
// the Widgets editor put it: a bottom margin on every QTextBlockFormat. A block
// created by splitting another inherits that format, so ordinary typing keeps
// the spacing; a wholesale setText does not, which is why every content change
// is re-covered.
//
// The re-cover is deferred to the event loop rather than run inside
// contentsChange: merging a block format is itself a document edit, and doing
// that from inside the document's own change signal re-enters the edit that is
// still in flight.
class EditorTextStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(int blockSpacing READ blockSpacing WRITE setBlockSpacing NOTIFY blockSpacingChanged)

public:
    explicit EditorTextStyle(QObject* parent = nullptr);

    QQuickTextDocument* textDocument() const;
    void setTextDocument(QQuickTextDocument* value);
    int blockSpacing() const;
    void setBlockSpacing(int pixels);

signals:
    void textDocumentChanged();
    void blockSpacingChanged();

private:
    void onContentsChange(int position, int charsRemoved, int charsAdded);
    void flushPendingRange();
    void applyToRange(int start, int end);
    void applyToDocument();
    QTextDocument* document() const;

    QPointer<QQuickTextDocument> textDocument_;
    int blockSpacing_ = 0;
    bool applying_ = false;
    bool rangePending_ = false;
    int pendingStart_ = 0;
    int pendingEnd_ = 0;
};
