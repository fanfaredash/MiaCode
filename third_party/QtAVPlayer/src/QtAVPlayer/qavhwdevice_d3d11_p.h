/***************************************************************
 * Copyright (C) 2020, 2025, Val Doroshchuk <valbok@gmail.com> *
 *                                                             *
 * This file is part of QtAVPlayer.                            *
 * Free Qt Media Player based on FFmpeg.                       *
 ***************************************************************/

#ifndef QAVHWDEVICE_D3D11_P_H
#define QAVHWDEVICE_D3D11_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qavhwdevice_p.h"

QT_BEGIN_NAMESPACE

struct AVCodecContext;
struct AVBufferRef;

// MiaCode H2: build an AV_HWDEVICE_TYPE_D3D11VA device context backed by the
// shared render ID3D11Device published via qavSetSharedRenderD3D11Device(), so
// the FFmpeg decoder and the QRhi renderer share one device (no per-frame
// cross-device texture bridge). Returns a new AVBufferRef the caller owns, or
// nullptr when no device is published / the device lacks video support / init
// fails — the caller then falls back to av_hwdevice_ctx_create(). Windows only.
Q_AVPLAYER_EXPORT AVBufferRef *qavCreateSharedD3D11VADeviceContext();

class Q_AVPLAYER_EXPORT QAVHWDevice_D3D11 : public QAVHWDevice
{
public:
    QAVHWDevice_D3D11() = default;
    ~QAVHWDevice_D3D11() = default;

    void init(AVCodecContext *avctx) override;
    AVPixelFormat format() const override;
    AVHWDeviceType type() const override;
    QAVVideoBuffer *videoBuffer(const QAVVideoFrame &frame) const override;

private:
    Q_DISABLE_COPY(QAVHWDevice_D3D11)
};

class QAVHWDevice_D3D11_GL : public QAVHWDevice
{
public:
    QAVHWDevice_D3D11_GL() = default;
    ~QAVHWDevice_D3D11_GL() = default;

    AVPixelFormat format() const override;
    AVHWDeviceType type() const override;
    QAVVideoBuffer *videoBuffer(const QAVVideoFrame &frame) const override;

private:
    Q_DISABLE_COPY(QAVHWDevice_D3D11_GL)
};

QT_END_NAMESPACE

#endif
