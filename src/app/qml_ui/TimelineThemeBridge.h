#pragma once

#include <QColor>
#include <QObject>
#include <QtQmlIntegration>

// Carries the timeline chrome colours from Theme.qml (the single source of
// truth) into the C++ snapshot the native timeline paints from. Theme.qml's
// `colors.timeline` group is bound onto this object's properties in
// BottomPanel.qml; every setter writes the snapshot via
// miacode::timeline::setTimelineChromeColors() so scene builders and the
// texture-cache signature pick the values up on their next build.
class TimelineThemeBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor windowColor READ windowColor WRITE setWindowColor NOTIFY windowColorChanged)
    Q_PROPERTY(QColor headerColor READ headerColor WRITE setHeaderColor NOTIFY headerColorChanged)
    Q_PROPERTY(QColor sidebarColor READ sidebarColor WRITE setSidebarColor NOTIFY sidebarColorChanged)
    Q_PROPERTY(QColor baseColor READ baseColor WRITE setBaseColor NOTIFY baseColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY axisColorChanged)
    Q_PROPERTY(QColor gridMajorColor READ gridMajorColor WRITE setGridMajorColor NOTIFY gridMajorColorChanged)
    Q_PROPERTY(QColor gridSubdivisionColor READ gridSubdivisionColor WRITE setGridSubdivisionColor NOTIFY gridSubdivisionColorChanged)
    Q_PROPERTY(QColor gridMinorColor READ gridMinorColor WRITE setGridMinorColor NOTIFY gridMinorColorChanged)
    Q_PROPERTY(QColor laneEvenColor READ laneEvenColor WRITE setLaneEvenColor NOTIFY laneEvenColorChanged)
    Q_PROPERTY(QColor laneOddColor READ laneOddColor WRITE setLaneOddColor NOTIFY laneOddColorChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY labelColorChanged)
    Q_PROPERTY(QColor textSecondaryColor READ textSecondaryColor WRITE setTextSecondaryColor NOTIFY textSecondaryColorChanged)
    Q_PROPERTY(QColor waveStrokeColor READ waveStrokeColor WRITE setWaveStrokeColor NOTIFY waveStrokeColorChanged)

public:
    explicit TimelineThemeBridge(QObject* parent = nullptr);

    QColor windowColor() const { return window_; }
    void setWindowColor(QColor value);

    QColor headerColor() const { return header_; }
    void setHeaderColor(QColor value);

    QColor sidebarColor() const { return sidebar_; }
    void setSidebarColor(QColor value);

    QColor baseColor() const { return base_; }
    void setBaseColor(QColor value);

    QColor borderColor() const { return border_; }
    void setBorderColor(QColor value);

    QColor axisColor() const { return axis_; }
    void setAxisColor(QColor value);

    QColor gridMajorColor() const { return gridMajor_; }
    void setGridMajorColor(QColor value);

    QColor gridSubdivisionColor() const { return gridSubdivision_; }
    void setGridSubdivisionColor(QColor value);

    QColor gridMinorColor() const { return gridMinor_; }
    void setGridMinorColor(QColor value);

    QColor laneEvenColor() const { return laneEven_; }
    void setLaneEvenColor(QColor value);

    QColor laneOddColor() const { return laneOdd_; }
    void setLaneOddColor(QColor value);

    QColor labelColor() const { return label_; }
    void setLabelColor(QColor value);

    QColor textSecondaryColor() const { return textSecondary_; }
    void setTextSecondaryColor(QColor value);

    QColor waveStrokeColor() const { return waveStroke_; }
    void setWaveStrokeColor(QColor value);

signals:
    void windowColorChanged();
    void headerColorChanged();
    void sidebarColorChanged();
    void baseColorChanged();
    void borderColorChanged();
    void axisColorChanged();
    void gridMajorColorChanged();
    void gridSubdivisionColorChanged();
    void gridMinorColorChanged();
    void laneEvenColorChanged();
    void laneOddColorChanged();
    void labelColorChanged();
    void textSecondaryColorChanged();
    void waveStrokeColorChanged();

private:
    void pushChrome();

    QColor window_;
    QColor header_;
    QColor sidebar_;
    QColor base_;
    QColor border_;
    QColor axis_;
    QColor gridMajor_;
    QColor gridSubdivision_;
    QColor gridMinor_;
    QColor laneEven_;
    QColor laneOdd_;
    QColor label_;
    QColor textSecondary_;
    QColor waveStroke_;
};
