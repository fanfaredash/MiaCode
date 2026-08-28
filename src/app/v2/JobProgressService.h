#pragma once

#include <QObject>
#include <QString>

namespace miacode::v2 {

// One long-running job's progress, modelled as state the shell renders rather
// than as a modal dialog the job owns.
//
// The job pushes label/percent while it runs and reads cancelRequested() at its
// own checkpoints; the shell only sets that flag. Cancellation is therefore
// cooperative and never tears a job down from underneath itself. Only one job
// runs at a time — begin() on an already-active service replaces the previous
// job's presentation, which is what the single shell overlay can show anyway.
class JobProgressService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed)
    Q_PROPERTY(bool cancelRequested READ cancelRequested NOTIFY changed)

public:
    explicit JobProgressService(QObject* parent = nullptr);

    bool active() const { return active_; }
    QString title() const { return title_; }
    QString label() const { return label_; }
    int percent() const { return percent_; }
    bool cancellable() const { return cancellable_; }
    bool cancelRequested() const { return cancelRequested_; }

    void begin(const QString& title, const QString& label, bool cancellable);
    void report(int percent, const QString& label);
    void end();

    Q_INVOKABLE void requestCancel();

signals:
    void changed();

private:
    bool active_ = false;
    bool cancellable_ = false;
    bool cancelRequested_ = false;
    int percent_ = 0;
    QString title_;
    QString label_;
};

}  // namespace miacode::v2
