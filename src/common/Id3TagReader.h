#pragma once

#include <QByteArray>
#include <QString>

// Minimal, dependency-free ID3v2 tag reader.
//
// Supports the two versions seen in practice today (v2.3 and v2.4), reads
// TIT2 / TPE1 / TALB text frames and the first APIC (attached picture)
// frame, and handles the four standard text encodings (Latin-1, UTF-16 +
// BOM, UTF-16BE, UTF-8). Synchronous and side-effect-free — meant for the
// chart-metadata UI buttons that pull values out of `track.mp3`.
//
// Not a general-purpose tag library: unsynchronization, frame encryption,
// frame compression, and ID3v1 trailers are not supported. Anything we
// can't parse cleanly is dropped silently (the caller gets an empty
// field) rather than throwing — chart workflows tolerate missing tags
// far better than a crash on a malformed MP3.
namespace miacode::id3 {

struct Tag
{
    bool valid = false;        // true iff an ID3v2 header was found and parsed
    QString title;             // TIT2
    QString artist;            // TPE1
    QString album;             // TALB (carried in case a future caller wants it)
    QByteArray pictureBytes;   // raw APIC payload, e.g. JPEG/PNG bytes verbatim
    QString pictureMimeType;   // APIC mime, e.g. "image/jpeg"
};

// Parse the ID3v2 header at the start of `filePath` and return the
// extracted tag. On any failure (missing file, no ID3 header, truncated
// frames, unsupported features) a `Tag` with `valid == false` is returned.
Tag readTagFromFile(const QString& filePath);

}  // namespace miacode::id3
