#pragma once

#include <QString>
#include <QVector>

namespace miacode::audio_decode {

enum class Backend {
    None,
    Bass,
    Miniaudio,
};

enum class BackendPreference {
    Bass,
    Miniaudio,
};

struct DecodedMonoAudio {
    QVector<float> samples;
    int sampleRate = 0;
    double durationSeconds = 0.0;
    Backend backend = Backend::None;

    bool isEmpty() const { return samples.isEmpty() || sampleRate <= 0; }
};

DecodedMonoAudio decodeFileToMono(
    const QString& path,
    int targetSampleRate,
    BackendPreference preference);
bool bassBackendAvailable();
QString backendLabel(Backend backend);

}  // namespace miacode::audio_decode
