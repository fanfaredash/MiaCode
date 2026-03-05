#pragma once

#include <QJsonObject>

struct PreviewAudioSettings {
    double bgmVolume = 0.6;
    double answerVolume = 0.15;
    double slideVolume = 0.05;
    double breakVolume = 0.10;
    double exVolume = 0.10;
    double touchVolume = 0.10;
    double touchholdVolume = 0.10;

    static double clamp(double value);
    void normalize();

    int bgmPercent() const;
    int answerPercent() const;
    int slidePercent() const;
    int breakPercent() const;
    int exPercent() const;
    int touchPercent() const;
    int touchholdPercent() const;
    void setBgmPercent(int value);
    void setAnswerPercent(int value);
    void setSlidePercent(int value);
    void setBreakPercent(int value);
    void setExPercent(int value);
    void setTouchPercent(int value);
    void setTouchholdPercent(int value);

    QJsonObject toJson() const;
    static PreviewAudioSettings fromJson(const QJsonObject& object);
};
