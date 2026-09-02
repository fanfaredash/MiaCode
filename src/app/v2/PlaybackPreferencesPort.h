#pragma once

#include "audio/PreviewAudioSettings.h"

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace miacode::v2 {

// The playback coordinator's one seam onto preferences and persisted state:
// portable-state disk I/O, the 预览设置 page's render-setting map, and the
// 音频设置 page's software-default mixer round trip.
//
// These eight methods are not one feature — their eventual owners are three
// different hosts (savePortableState/loadProjectRenderState end up on
// EditorHost, setLastOpenDirectory on DocumentSessionHost, the render/audio
// preference read-writes stay on Session itself) — so the port is cut by
// capability ("read/write/persist a preference"), not by host. Session
// implements it because reaching across those three hosts is exactly the
// orchestration job Session keeps once each domain has its own host.
//
// Deliberately free of Session, QWidget, and QML/QSG types: PreferencesPortSpec
// proves that at link time, by implementing this port with a fake that pulls
// in neither.
class PlaybackPreferencesPort
{
public:
    virtual ~PlaybackPreferencesPort() = default;

    virtual void savePortableState() const = 0;
    virtual void setPreviewRenderSetting(const QString& key, const QVariant& value) = 0;
    virtual QVariantMap previewRenderSettings() const = 0;
    virtual void loadProjectRenderState() = 0;
    virtual void savePreviewAudioSettingsAsSoftwareDefault() = 0;
    virtual void restorePreviewAudioSettingsFromSoftwareDefault() = 0;
    virtual void applyPreviewAudioSettingsFromUi(const PreviewAudioSettings& settings) = 0;
    virtual void setLastOpenDirectory(const QString& pathOrDir) = 0;
};

}  // namespace miacode::v2
