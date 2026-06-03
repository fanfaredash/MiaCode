#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QString>

namespace miacode::fs {

// Content stamp of a file: "size:lastModifiedMs", or an empty string when the
// file is absent. Two files at the same path are considered "the same content"
// iff their stamps are equal.
//
// This is the basis of the preview audio / stage-media "should I reload?" skip
// decision. Keying that decision on the path string alone is unsafe because
// users routinely keep the same filename (track.mp3 / pv.mp4 / bg.png) while
// changing the bytes — the in-app audio/video tools rewrite them in place, and
// users swap same-named files in the file manager. Comparing the stamp instead
// detects same-name/new-content and forces a reload, while an unchanged file
// still skips. The function re-stats the file on every call (fresh QFileInfo),
// so it always reflects the current on-disk state — no caching.
inline QString fileContentStamp(const QString& path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return QString();
    }
    return QStringLiteral("%1:%2")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

}  // namespace miacode::fs
