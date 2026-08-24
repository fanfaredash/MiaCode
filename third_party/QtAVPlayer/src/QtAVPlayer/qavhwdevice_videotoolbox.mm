/*********************************************************
 * Copyright (C) 2020, Val Doroshchuk <valbok@gmail.com> *
 *                                                       *
 * This file is part of QtAVPlayer.                      *
 * Free Qt Media Player based on FFmpeg.                 *
 *********************************************************/

#include "qavhwdevice_videotoolbox_p.h"
#include "qavvideobuffer_gpu_p.h"

#import <CoreVideo/CoreVideo.h>
#if defined(Q_OS_MACOS)
#import <IOSurface/IOSurface.h>
#else
#import <IOSurface/IOSurfaceRef.h>
#endif
#import <Metal/Metal.h>

#include <QList>
#include <QVariant>
#include <QDebug>

QT_BEGIN_NAMESPACE

class QAVHWDevice_VideoToolboxPrivate
{
public:
    id<MTLDevice> device = nullptr;
    CVPixelBufferRef pbuf = nullptr;
};

QAVHWDevice_VideoToolbox::QAVHWDevice_VideoToolbox()
    : d_ptr(new QAVHWDevice_VideoToolboxPrivate)
{
}

QAVHWDevice_VideoToolbox::~QAVHWDevice_VideoToolbox()
{
    Q_D(QAVHWDevice_VideoToolbox);
    if (d->pbuf)
        CVPixelBufferRelease(d->pbuf);
    [d->device release];
}

AVPixelFormat QAVHWDevice_VideoToolbox::format() const
{
    return AV_PIX_FMT_VIDEOTOOLBOX;
}

AVHWDeviceType QAVHWDevice_VideoToolbox::type() const
{
    return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
}

class VideoBuffer_MTL : public QAVVideoBuffer_GPU
{
public:
    VideoBuffer_MTL(QAVHWDevice_VideoToolboxPrivate *hw, const QAVVideoFrame &frame)
        : QAVVideoBuffer_GPU(frame)
        , m_hw(hw)
    {
    }

    ~VideoBuffer_MTL() override
    {
        releaseTextureObjects();
    }

    QAVVideoFrame::HandleType handleType() const override
    {
        return QAVVideoFrame::MTLTextureHandle;
    }

    QVariant handle(QRhi */*rhi*/) const override
    {
        QList<QVariant> textures = { 0, 0 };
        if (m_texturesReady) {
            textures[0] = quint64(m_textureObjects[0]);
            textures[1] = quint64(m_textureObjects[1]);
            return textures;
        }
        releaseTextureObjects();

        CVPixelBufferRef pbuf = (CVPixelBufferRef)frame().frame()->data[3];

        if (!pbuf)
            return textures;

        CVPixelBufferRetain(pbuf);
        if (m_hw->pbuf)
            CVPixelBufferRelease(m_hw->pbuf);
        m_hw->pbuf = pbuf;

        if (CVPixelBufferGetDataSize(m_hw->pbuf) <= 0)
            return textures;

        const OSType format = CVPixelBufferGetPixelFormatType(m_hw->pbuf);
        bool tenBit = false;
        switch (format) {
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
            break;
        case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
            tenBit = true;
            break;
        default:
            qWarning() << "Unsupported VideoToolbox pixel format" << format;
            return textures;
        }

        if (!m_hw->device)
            m_hw->device = MTLCreateSystemDefaultDevice();

        IOSurfaceRef surface = CVPixelBufferGetIOSurface(m_hw->pbuf);
        const int planes = CVPixelBufferGetPlaneCount(m_hw->pbuf);
        if (!surface || planes != 2) {
            qWarning() << "Invalid VideoToolbox IOSurface planes" << planes;
            return textures;
        }
        for (int i = 0; i < planes; ++i) {
            int w = IOSurfaceGetWidthOfPlane(surface, i);
            int h = IOSurfaceGetHeightOfPlane(surface, i) ;
            MTLPixelFormat f = tenBit
                ? (i ? MTLPixelFormatRG16Unorm : MTLPixelFormatR16Unorm)
                : (i ? MTLPixelFormatRG8Unorm : MTLPixelFormatR8Unorm);
            MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];

            m_textureObjects[i] = [m_hw->device newTextureWithDescriptor:desc iosurface:surface plane:i];
            textures[i] = quint64(m_textureObjects[i]);
        }

        m_texturesReady = m_textureObjects[0] && m_textureObjects[1];
        if (!m_texturesReady)
            releaseTextureObjects();

        return textures;
    }

private:
    void releaseTextureObjects() const
    {
        for (id<MTLTexture> texture : m_textureObjects) {
            [texture release];
        }
        m_textureObjects[0] = nil;
        m_textureObjects[1] = nil;
        m_texturesReady = false;
    }

    QAVHWDevice_VideoToolboxPrivate *m_hw = nullptr;
    mutable id<MTLTexture> m_textureObjects[2] = { nullptr, nullptr };
    mutable bool m_texturesReady = false;
};

QAVVideoBuffer *QAVHWDevice_VideoToolbox::videoBuffer(const QAVVideoFrame &frame) const
{
    return new VideoBuffer_MTL(d_ptr.get(), frame);
}

QT_END_NAMESPACE
