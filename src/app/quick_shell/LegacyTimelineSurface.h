#pragma once

#include <QObject>
#include <QWindow>

class LegacyTimelineSurface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window NOTIFY windowChanged)

public:
    explicit LegacyTimelineSurface(QObject* parent = nullptr);

    QWindow* window() const;
    void setWindow(QWindow* window);

signals:
    void windowChanged();

private:
    QWindow* window_ = nullptr;
};
