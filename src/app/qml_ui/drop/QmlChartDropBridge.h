#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>

class QMimeData;
class QTimer;

namespace miacode::qml_ui {

struct QmlChartDropResult {
    quint64 requestId = 0;
    quint64 generation = 0;
    bool accepted = false;
    bool completed = false;
    bool cancelled = false;
    int createdCount = 0;
    int failedCount = 0;
    QString targetPath;
};

class QmlChartDropBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dragActive READ dragActive NOTIFY dragActiveChanged)
    Q_PROPERTY(QStringList acceptedPaths READ acceptedPaths NOTIFY acceptedPathsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    using Submit = std::function<void(const QStringList&, quint64, quint64,
                                      std::function<void(const QmlChartDropResult&)>)>;
    using Completion = std::function<void(const QmlChartDropResult&)>;

    QmlChartDropBridge(QObject& window, std::function<void()> enableDropTarget, Submit submit,
                       Completion completion, QObject* parent = nullptr);
    ~QmlChartDropBridge() override;

    bool dragActive() const { return dragActive_; }
    QStringList acceptedPaths() const { return acceptedPaths_; }
    bool busy() const { return busy_; }
    void release();

signals:
    void dragActiveChanged();
    void acceptedPathsChanged();
    void busyChanged();
    void dropBusy();
    void dropCompleted(const QmlChartDropResult& result);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QStringList supportedPaths(const QMimeData* mimeData) const;
    void setDragState(bool active, const QStringList& paths = {});
    void scheduleDragLeave();
    void clearDragLeaveTimer();
    void submitDrop(const QStringList& paths);

    QPointer<QObject> window_;
    std::function<void()> enableDropTarget_;
    Submit submit_;
    Completion completion_;
    QTimer* leaveTimer_ = nullptr;
    QStringList acceptedPaths_;
    quint64 nextRequestId_ = 0;
    quint64 activeRequestId_ = 0;
    quint64 generation_ = 1;
    bool dragActive_ = false;
    bool busy_ = false;
    bool released_ = false;
};

} // namespace miacode::qml_ui
