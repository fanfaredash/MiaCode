#pragma once

#include "export/CoverExportSession.h"

#include <QIcon>
#include <QObject>
#include <QPointer>

#include <memory>

class QQmlApplicationEngine;
class QQuickWindow;

namespace miacode::ui {
class WorkbenchSettings;

class CoverExportWindow final : public QObject
{
    Q_OBJECT

public:
    CoverExportWindow(miacode::ExportEngine& exportEngine,
                         miacode::UiRequestService& requests,
                         miacode::PlaybackControl*& playbackControlSlot,
                         WorkbenchSettings& preferences,
                         const QIcon& icon,
                         QObject* parent = nullptr);
    ~CoverExportWindow() override;

    bool show(QQuickWindow* owner, int difficultyId);
    void raise();
    Q_INVOKABLE void close();

private:
    WorkbenchSettings& preferences_;
    QIcon icon_;
    CoverExportSession session_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QPointer<QQuickWindow> window_;
    bool closePending_ = false;
};
} // namespace miacode::ui
