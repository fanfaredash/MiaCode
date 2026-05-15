#include "Id3TagReader.h"

#include <QFile>

namespace miacode::id3 {

namespace {

quint32 readSynchsafe32(const uchar* p)
{
    // Synchsafe integers (ID3v2 header and v2.4 frame sizes): every byte's
    // top bit is forced to zero so the size never collides with an MPEG
    // frame-sync marker. Bottom 7 bits of each byte concatenated MSB-first.
    return (quint32(p[0] & 0x7f) << 21)
         | (quint32(p[1] & 0x7f) << 14)
         | (quint32(p[2] & 0x7f) <<  7)
         | (quint32(p[3] & 0x7f) <<  0);
}

quint32 readBigEndian32(const uchar* p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) <<  8) | (quint32(p[3]) <<  0);
}

QString decodeUtf16(const QByteArray& bytes, bool littleEndian)
{
    const int charCount = bytes.size() / 2;
    QString out;
    out.reserve(charCount);
    for (int i = 0; i < charCount; ++i) {
        const uchar lo = uchar(bytes[i * 2]);
        const uchar hi = uchar(bytes[i * 2 + 1]);
        const ushort unit = littleEndian
            ? static_cast<ushort>(lo | (quint16(hi) << 8))
            : static_cast<ushort>((quint16(lo) << 8) | hi);
        if (unit == 0) {
            break;  // trailing null terminator
        }
        out.append(QChar(unit));
    }
    return out;
}

// Decode an ID3v2 text-frame payload: 1-byte encoding marker followed by
// the encoded string. The trailing null terminator (if any) is stripped.
QString decodeTextFrame(const QByteArray& payload)
{
    if (payload.isEmpty()) {
        return QString();
    }
    const uchar encoding = uchar(payload[0]);
    const QByteArray body = payload.mid(1);

    switch (encoding) {
    case 0: {  // ISO-8859-1
        int len = body.size();
        while (len > 0 && body[len - 1] == '\0') {
            --len;
        }
        return QString::fromLatin1(body.constData(), len);
    }
    case 1: {  // UTF-16 with BOM
        if (body.size() < 2) {
            return QString();
        }
        const uchar b0 = uchar(body[0]);
        const uchar b1 = uchar(body[1]);
        const bool littleEndian = (b0 == 0xff && b1 == 0xfe);
        const bool bigEndian = (b0 == 0xfe && b1 == 0xff);
        if (!littleEndian && !bigEndian) {
            // No BOM — assume little-endian per the most common encoder
            // behaviour. (Some encoders omit the BOM despite the spec.)
            return decodeUtf16(body, /*littleEndian=*/true);
        }
        return decodeUtf16(body.mid(2), littleEndian);
    }
    case 2:  // UTF-16BE without BOM
        return decodeUtf16(body, /*littleEndian=*/false);
    case 3: {  // UTF-8
        int len = body.size();
        while (len > 0 && body[len - 1] == '\0') {
            --len;
        }
        return QString::fromUtf8(body.constData(), len);
    }
    default:
        // Unknown encoding — best-effort UTF-8.
        return QString::fromUtf8(body);
    }
}

// Parse an APIC frame and pull out the embedded picture's MIME type and
// raw bytes. The frame layout is:
//   <1 byte text encoding>
//   <ISO-8859-1 MIME type, null-terminated>
//   <1 byte picture type>
//   <description, encoding-dependent, null-terminated>
//   <picture data, to end of frame>
void parseApic(const QByteArray& payload, Tag& tag)
{
    if (payload.size() < 4) {
        return;
    }
    int pos = 0;
    const uchar encoding = uchar(payload[pos]);
    ++pos;

    // MIME type — always ISO-8859-1, single-byte null terminator.
    const int mimeEnd = payload.indexOf('\0', pos);
    if (mimeEnd < 0) {
        return;
    }
    const QString mime = QString::fromLatin1(payload.constData() + pos, mimeEnd - pos);
    pos = mimeEnd + 1;

    // Picture-type byte (cover / artist photo / etc.) — we don't filter
    // on this, the first picture in tag order wins.
    if (pos >= payload.size()) {
        return;
    }
    ++pos;

    // Description, encoding-dependent.
    if (encoding == 0 || encoding == 3) {
        // Single-byte null terminator.
        const int descEnd = payload.indexOf('\0', pos);
        if (descEnd < 0) {
            return;
        }
        pos = descEnd + 1;
    } else {
        // UTF-16: double-byte null terminator at an even offset.
        int descEnd = -1;
        for (int i = pos; i + 1 < payload.size(); i += 2) {
            if (payload[i] == '\0' && payload[i + 1] == '\0') {
                descEnd = i;
                break;
            }
        }
        if (descEnd < 0) {
            return;
        }
        pos = descEnd + 2;
    }

    if (pos >= payload.size()) {
        return;
    }
    tag.pictureBytes = payload.mid(pos);
    tag.pictureMimeType = mime.trimmed();
}

}  // namespace

Tag readTagFromFile(const QString& filePath)
{
    Tag tag;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return tag;
    }

    // 10-byte ID3v2 header: "ID3" + version (2 bytes) + flags + 4-byte
    // synchsafe size.
    const QByteArray header = file.read(10);
    if (header.size() < 10) {
        return tag;
    }
    if (header.left(3) != QByteArrayLiteral("ID3")) {
        return tag;
    }

    const int versionMajor = uchar(header[3]);
    if (versionMajor < 2 || versionMajor > 4) {
        return tag;  // only v2.2/2.3/2.4 are in the wild; we handle 2.3/2.4
    }
    const uchar flags = uchar(header[5]);
    const quint32 tagSize = readSynchsafe32(reinterpret_cast<const uchar*>(header.constData() + 6));

    const QByteArray body = file.read(tagSize);
    if (body.size() < static_cast<int>(tagSize)) {
        return tag;
    }

    int pos = 0;

    // Skip extended header if present. Size encoding differs between
    // v2.3 (regular big-endian) and v2.4 (synchsafe).
    if (flags & 0x40) {
        if (body.size() < pos + 4) {
            return tag;
        }
        if (versionMajor >= 4) {
            const quint32 extSize = readSynchsafe32(
                reinterpret_cast<const uchar*>(body.constData() + pos));
            pos += static_cast<int>(extSize);
        } else {
            const quint32 extSize = readBigEndian32(
                reinterpret_cast<const uchar*>(body.constData() + pos));
            pos += 4 + static_cast<int>(extSize);
        }
        if (pos > body.size()) {
            return tag;
        }
    }

    // ID3v2.2 uses 3-byte frame IDs; v2.3 and v2.4 use 4-byte frame IDs
    // with the size encoding noted above. We only parse v2.3+ here — v2.2
    // is exceedingly rare in modern files.
    if (versionMajor < 3) {
        tag.valid = true;
        return tag;
    }

    while (pos + 10 <= body.size()) {
        const QByteArray frameId = body.mid(pos, 4);
        if (frameId.size() < 4 || frameId.at(0) == '\0') {
            break;  // padding / end of tag
        }
        const uchar* sizeBytes = reinterpret_cast<const uchar*>(body.constData() + pos + 4);
        const quint32 frameSize = (versionMajor >= 4)
            ? readSynchsafe32(sizeBytes)
            : readBigEndian32(sizeBytes);
        // 2-byte frame-flags field at pos+8 is ignored — none of the
        // optional flags (compression, encryption, group identity,
        // unsynchronisation) are supported.
        pos += 10;
        if (frameSize == 0 || pos + static_cast<int>(frameSize) > body.size()) {
            break;
        }

        const QByteArray content = body.mid(pos, frameSize);
        pos += static_cast<int>(frameSize);

        if (frameId == QByteArrayLiteral("TIT2")) {
            tag.title = decodeTextFrame(content);
        } else if (frameId == QByteArrayLiteral("TPE1")) {
            tag.artist = decodeTextFrame(content);
        } else if (frameId == QByteArrayLiteral("TALB")) {
            tag.album = decodeTextFrame(content);
        } else if (frameId == QByteArrayLiteral("APIC") && tag.pictureBytes.isEmpty()) {
            parseApic(content, tag);
        }
    }

    tag.valid = true;
    return tag;
}

}  // namespace miacode::id3
