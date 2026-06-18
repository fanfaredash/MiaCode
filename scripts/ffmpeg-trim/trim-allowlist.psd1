@{
    # =====================================================================
    # FFmpeg decode-only trim allowlist — the single source of truth for
    # which FFmpeg components the trimmed preview-decode DLLs keep.
    #
    # Consumed by build-trimmed-ffmpeg.ps1 (turns these into FFmpeg
    # `./configure --enable-*` flags) and extended by survey-chart-codecs.ps1
    # (which ADDS observed codecs/containers from real chart PVs — it never
    # removes). See scripts/ffmpeg-trim/README.md for the methodology.
    #
    # Editing rule: this list is conservative-superset by design. Adding a
    # decoder/demuxer costs tens-to-hundreds of KB; OMITTING one a user's PV
    # needs makes that video silently fail. When unsure, keep it.
    # =====================================================================

    # FFmpeg source tag to build. MUST stay on the n7.1 series so the produced
    # DLL/import-lib basenames keep the major versions the rest of the build
    # is pinned to (CMakeLists.txt + scripts/package-win.ps1).
    FFmpegVersion = 'n7.1'

    # ABI-pinned outputs this build must produce (major-version-locked).
    # The build script asserts each of these DLLs + a matching import lib
    # exists before it overwrites the dev SDK. avdevice is intentionally
    # absent (dropped — capture-device only; QtAVPlayer is patched not to use
    # it, see QT_AVPLAYER_NO_AVDEVICE).
    ExpectedDlls = @(
        'avcodec-61', 'avformat-61', 'avutil-59',
        'swresample-5', 'swscale-8', 'avfilter-10'
    )

    # ---------------------------------------------------------------------
    # MANDATORY — always enabled regardless of the codec survey. These are
    # playback-critical plumbing, not content codecs; trimming any of them
    # breaks ALL playback, not just one video. NEVER remove.
    # ---------------------------------------------------------------------
    Mandatory = @{
        # QtAVPlayer only builds an avfilter graph when a filter string is set via
        # setFilter()/setFilters() (qavfilters.cpp: empty desc -> no graph). MiaCode's
        # preview host never sets one, so in normal playback avfilter is linked but
        # NOT exercised — pixel-format conversion happens in QAVVideoFrame's swscale
        # path, not avfilter. These are kept as cheap insurance for any setFilter use
        # (now/future): the 4 graph endpoints QtAVPlayer resolves by name via
        # avfilter_get_by_name() — buffer/buffersink (qavvideo{in,out}putfilter.cpp),
        # abuffer/abuffersink (qavaudio{in,out}putfilter.cpp) — plus the common
        # format/scale/fps/resample conversions a graph would auto-insert. FFmpeg's
        # configure auto-resolves each filter's library deps (scale->swscale,
        # aresample->swresample, ...), so trimming can't break a transitive dependency.
        # The 4 QtAVPlayer filtergraph ENDPOINTS resolved by name via
        # avfilter_get_by_name(): buffer/buffersink (qavvideo{in,out}putfilter.cpp),
        # abuffer/abuffersink (qavaudio{in,out}putfilter.cpp). These are NOT
        # --enable-filter components and never appear in `configure --list-filters`;
        # FFmpeg's configure appends them (asrc_abuffer/vsrc_buffer/asink_abuffer/
        # vsink_buffer) to filter_list UNCONDITIONALLY whenever libavfilter is built,
        # so they are ALWAYS present and must NOT be passed to --enable-filter.
        # Listed here only so the post-build runtime verify can assert they survived.
        AlwaysOnFilters = @('buffer', 'buffersink', 'abuffer', 'abuffersink')
        # Toggleable conversion filters a graph would auto-insert — these DO take
        # --enable-filter. configure auto-resolves each filter's library deps
        # (scale->swscale, aresample->swresample, ...), so trimming can't break a
        # transitive dependency.
        Filters = @(
            'format', 'scale', 'fps', 'setpts', 'null', 'copy',
            'aformat', 'aresample', 'asetpts', 'anull'
        )
        # Without 'file' nothing local opens — every PV fails to load.
        Protocols = @('file')
        # Windows D3D11VA hardware decode (QtAVPlayer's d3d11 hwdevice). If a
        # hwaccel for the input codec is missing, decode falls back to software
        # (slower but still works) — provided the software decoder is kept below.
        Hwaccels = @(
            'h264_d3d11va', 'h264_d3d11va2',
            'hevc_d3d11va', 'hevc_d3d11va2',
            'vp9_d3d11va',  'vp9_d3d11va2',
            'av1_d3d11va',  'av1_d3d11va2'
        )
        # Bitstream filters required to feed mp4/mov-stored streams to (hw)
        # decoders — missing these makes an enabled decoder still fail on mp4.
        Bsfs = @(
            'h264_mp4toannexb', 'hevc_mp4toannexb',
            'extract_extradata', 'vp9_superframe'
        )
        # Raw configure feature flags (not component lists).
        #  --enable-d3d11va/--enable-dxva2: hardware-decode backends are autodetect
        #    features that --disable-everything turns off; re-enable explicitly.
        #  --disable-iconv: avformat only uses libiconv for metadata/subtitle charset
        #    conversion — not needed for muted video preview. Dropping it removes the
        #    external libiconv-2.dll dependency (and ~1 MB).
        #  --extra-ldflags=-static: SELF-CONTAINMENT. A MinGW *shared* build otherwise
        #    leaves the produced av*.dll dynamically depending on MinGW runtime DLLs
        #    (zlib1.dll, libwinpthread-1.dll, libgcc_s_*.dll) that we don't ship next
        #    to MiaCode.exe — the loader then fails with ERROR_MOD_NOT_FOUND (126) and
        #    the app won't open. -static links those (and zlib, kept for PNG) INTO the
        #    DLLs, matching how the full BtbN SDK is self-contained. The build verifies
        #    no such external dep remains (objdump assert).
        #  --enable-libdav1d: software AV1 decoder (see the 'libdav1d' decoder note
        #    below). Needs mingw-w64-x86_64-dav1d installed in the toolchain step.
        #    dav1d must link STATICALLY into avcodec-61.dll or the self-containment
        #    objdump assert (no external libdav1d.dll/libwinpthread) will fail; if it
        #    trips, add '--pkg-config-flags=--static' here and rebuild.
        ExtraConfigureArgs = @('--enable-d3d11va', '--enable-dxva2', '--disable-iconv', '--enable-libdav1d', '--extra-ldflags=-static')
    }

    # ---------------------------------------------------------------------
    # DECODERS — the content codecs. Safe superset; survey-chart-codecs.ps1
    # extends this from real PVs. Keep audio decoders too: QtAVPlayer opens the
    # audio stream even though MiaCode keeps it muted, and a missing audio
    # decoder can still surface as InvalidMedia on some containers.
    # ---------------------------------------------------------------------
    Decoders = @(
        # video
        'h264', 'hevc', 'vp8', 'vp9', 'av1',
        # AV1 SOFTWARE decode. The built-in 'av1' decoder above is HWACCEL-ONLY
        # ("Supported hardware devices: d3d11va", no software path) — forcing
        # software with it yields ZERO frames (black screen). libdav1d (BSD-2,
        # LGPL-compatible) is the real software AV1 decoder; it also outputs
        # display-size frames (no decoder alignment padding), which sidesteps the
        # 1080->1152 hardware-padding 花屏 on AV1. Requires --enable-libdav1d
        # (ExtraConfigureArgs) + mingw-w64-x86_64-dav1d in the toolchain step.
        'libdav1d',
        # Legacy codecs that were unsupported (no decoder) even though their
        # containers are already demuxable: VC-1 / WMV (asf), Theora (ogg),
        # Sorenson FLV1 (flv), VP6 (flv/swf). Native LGPL decoders, tens of KB
        # each; added per this list's conservative-superset rule so a stray PV
        # in one of these formats doesn't silently fail.
        'vc1', 'wmv1', 'wmv2', 'wmv3', 'msmpeg4v2', 'msmpeg4v3',
        'theora', 'flv1', 'vp6', 'vp6f',
        'mpeg4', 'mpeg2video', 'mpeg1video', 'mjpeg', 'prores', 'rawvideo',
        'png', 'gif',
        # audio
        'aac', 'mp3', 'ac3', 'eac3', 'opus', 'vorbis', 'flac',
        'pcm_s16le', 'pcm_s24le', 'pcm_f32le'
    )

    # Container demuxers. mov = mp4/m4v/mov; matroska = mkv/webm.
    Demuxers = @(
        'mov', 'matroska', 'avi', 'flv', 'mpegts', 'mpegps', 'asf',
        'mp3', 'wav', 'ogg', 'flac', 'aac', 'image2', 'gif'
    )

    # Parsers — paired with the decoders above. A decoder without its parser
    # can still fail; configure auto-pulls most, but list the common ones.
    Parsers = @(
        'h264', 'hevc', 'vp8', 'vp9', 'av1',
        'mpeg4video', 'mpegvideo', 'mjpeg', 'png',
        # vc1 parser pairs with the vc1/wmv3 decoders added above; the other new
        # decoders' parsers are auto-pulled by configure as dependencies.
        'vc1',
        'aac', 'ac3', 'mpegaudio', 'flac', 'opus', 'vorbis'
    )
}
