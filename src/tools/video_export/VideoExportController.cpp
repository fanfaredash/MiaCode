#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/IntroConfig.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "common/PreviewSfxTimeline.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// Core translation unit for VideoExportController. After the god-file split this
// keeps only the thin VideoExportController::exportFullPreview entry point; every
// free helper now lives in namespace miacode::video_export::detail across the
// VideoExport{Encoder,Diagnostics,FrameRender,Pipeline}.cpp group TUs (declared in
// VideoExportControllerInternal.h), and exportPreparedTask lives in
// VideoExportPreparedTask.cpp.
using namespace miacode::video_export::detail;

VideoExportResult VideoExportController::exportFullPreview(
    const VideoExportTask& task
)
{
    MC_OP("VideoExportController::exportFullPreview");
    _mc_op_.note(QStringLiteral("output=%1").arg(task.outputPath));
    return exportPreparedTask(task);
}
