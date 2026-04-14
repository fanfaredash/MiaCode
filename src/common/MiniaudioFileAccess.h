#pragma once

#include <QDir>
#include <QFile>
#include <QString>

#include "../../third_party/miniaudio/miniaudio.h"

namespace miacode::audio_io {

inline ma_encoding_format detectedEncodingFormat(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ma_encoding_format_unknown;
    }

    const QByteArray header = file.read(16);
    if (header.size() >= 12
        && header.startsWith("RIFF")
        && header.mid(8, 4) == "WAVE") {
        return ma_encoding_format_wav;
    }
    if (header.size() >= 4 && header.startsWith("fLaC")) {
        return ma_encoding_format_flac;
    }
    if (header.size() >= 4 && header.startsWith("OggS")) {
        return ma_encoding_format_vorbis;
    }
    if (header.size() >= 3 && header.startsWith("ID3")) {
        return ma_encoding_format_mp3;
    }
    if (header.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(header.at(0));
        const unsigned char b1 = static_cast<unsigned char>(header.at(1));
        if (b0 == 0xFF && (b1 & 0xE0) == 0xE0) {
            return ma_encoding_format_mp3;
        }
    }

    return ma_encoding_format_unknown;
}

inline ma_result decoderInitFile(const QString& path, const ma_decoder_config* config, ma_decoder* decoder)
{
    ma_decoder_config resolvedConfig = config != nullptr
        ? *config
        : ma_decoder_config_init(ma_format_unknown, 0, 0);
    if (const ma_encoding_format detectedFormat = detectedEncodingFormat(path);
        detectedFormat != ma_encoding_format_unknown) {
        resolvedConfig.encodingFormat = detectedFormat;
    }

#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(path);
    return ma_decoder_init_file_w(
        reinterpret_cast<const wchar_t*>(nativePath.utf16()),
        &resolvedConfig,
        decoder
    );
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    return ma_decoder_init_file(encodedPath.constData(), &resolvedConfig, decoder);
#endif
}

inline ma_result soundInitFromFile(
    ma_engine* engine,
    const QString& path,
    ma_uint32 flags,
    ma_sound_group* group,
    ma_fence* doneFence,
    ma_sound* sound)
{
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(path);
    return ma_sound_init_from_file_w(
        engine,
        reinterpret_cast<const wchar_t*>(nativePath.utf16()),
        flags,
        group,
        doneFence,
        sound
    );
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    return ma_sound_init_from_file(
        engine,
        encodedPath.constData(),
        flags,
        group,
        doneFence,
        sound
    );
#endif
}

}  // namespace miacode::audio_io
