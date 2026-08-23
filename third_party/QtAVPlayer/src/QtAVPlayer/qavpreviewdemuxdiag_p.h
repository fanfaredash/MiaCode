/***************************************************************
 * MiaCode addition (not upstream QtAVPlayer).
 *
 * Demuxer end-of-file provenance, for the false-EndOfMedia
 * investigation in
 * docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md §5.2.
 ***************************************************************/

#ifndef QAVPREVIEWDEMUXDIAG_P_H
#define QAVPREVIEWDEMUXDIAG_P_H

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

#include "qtavplayerglobal.h"

QT_BEGIN_NAMESPACE

// ---------------------------------------------------------------------------
// Why this exists
//
// QAVDemuxer::read() collapses two very different outcomes into one `eof` bit:
//
//   * av_read_frame() returned AVERROR_EOF  -> the stream really ended;
//   * avio_feof() reports the AVIOContext at EOF -> which FFmpeg ALSO latches
//     after any failed byte-level read (fill_buffer() sets eof_reached = 1 and
//     stores the errno in AVIOContext::error). Once latched it stays latched
//     until an avio_seek(), so one transient read failure silently converts the
//     rest of the file into "end of media".
//
// QAVPlayer then reports EndOfMedia, and MiaCode's PV freezes on whatever frame
// happened to be last — the exact signature captured in the audit (121 s clip,
// EndOfMedia after 1.4 s of playback with the last pts at 1.267 s).
//
// The counters below record which of the two produced the EOF and what the AVIO
// layer's own error field said at that moment. Nothing here changes decode
// behaviour: the vendored code only observes. The product-side classification
// and recovery live in PreviewStageMediaHost (preview/stage_media eom_class=…).
//
// All fields are cumulative for the process and written from the demuxer thread
// with relaxed atomics; the GUI thread drains them on a low-frequency cadence
// (end-of-media / seek), never per packet.
// ---------------------------------------------------------------------------
struct QAVPreviewDemuxEofDiag
{
    unsigned long long eofEvents;        // times read() latched eof
    unsigned long long eofFromAvErrorEof;// av_read_frame() == AVERROR_EOF
    unsigned long long eofFromAvioFeof;  // avio_feof() latched it instead
    unsigned long long readFailures;     // av_read_frame() < 0 that was not EOF
    long long lastReadResult;            // last negative av_read_frame() return
    long long lastAvioError;             // AVIOContext::error when eof latched
    long long lastEofBytePos;            // avio_tell() byte offset when eof latched
    unsigned long long seekResets;       // seeks that cleared the eof latch
};

Q_AVPLAYER_EXPORT void qavGetPreviewDemuxEofDiag(QAVPreviewDemuxEofDiag *out);

QT_END_NAMESPACE

#endif
