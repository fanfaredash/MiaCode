#pragma once

#include "QmlCoverExportSession.h"

#include <QIcon>
#include <QObject>
#include <QPointer>

#include <memory>

class QQmlApplicationEngine;
class QQuickWindow;
class QmlUiSettings;

// One lifetime for the window, its QML engine, request callbacks and cover assets.
class QmlCoverExportWindow final : public QObject
{
    Q_OBJECT

public:
    QmlCoverExportWindow(miacode::v2::ExportEngine& exportEngine,
                         miacode::v2::PlaybackControl*& playbackControlSlot,
                         QmlUiSettings& preferences,
                         const QIcon& icon,
                         QObject* parent = nullptr);
    ~QmlCoverExportWindow() override;

    bool show(QQuickWindow* owner, int difficultyId);
    void raise();
    Q_INVOKABLE void close();

private:
    QmlUiSettings& preferences_;
    QIcon icon_;
    miacode::v2::UiRequestService requests_;
    QmlCoverExportSession session_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QPointer<QQuickWindow> window_;
    bool closePending_ = false;
};
