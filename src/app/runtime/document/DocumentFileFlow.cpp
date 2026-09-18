#include "runtime/document/DocumentSessionHost.h"
#include "app/services/UiRequestService.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/Id3TagReader.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "common/WaveformCache.h"
#include "app/services/PlaybackStateAuthority.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>

using namespace miacode::runtime::shared;
#include "runtime/document/DocumentFlow.Internal.h"

using namespace miacode::runtime::document_detail;

namespace {

struct PreparedDocumentOpenPayload {
    bool success = false;
    bool usedSystemEncoding = false;
    QString normalizedPath;
    SimaiDocument document;
    QString resolvedTrackPath;
    double trackDurationSeconds = 0.0;
    bool hasTrackDuration = false;
    qint64 readElapsedMs = 0;
    qint64 decodeElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 trackProbeElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

PreparedDocumentOpenPayload prepareDocumentOpenPayload(const QString& path, bool probeTrackDuration)
{
    PreparedDocumentOpenPayload payload;
    payload.normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (payload.normalizedPath.isEmpty()) {
        return payload;
    }

    QFile file(payload.normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return payload;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    const QByteArray bytes = file.readAll();
    payload.readElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            payload.usedSystemEncoding = true;
        }
    }
    payload.decodeElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    payload.document = SimaiDocument::fromText(text);
    payload.parseElapsedMs = phaseTimer.elapsed();

    if (probeTrackDuration) {
        payload.resolvedTrackPath = miacode::chart_assets::resolveTrackPath(payload.normalizedPath);
        phaseTimer.restart();
        if (!payload.resolvedTrackPath.isEmpty()) {
            payload.trackDurationSeconds = probeAudioDurationSeconds(payload.resolvedTrackPath);
            payload.hasTrackDuration = payload.trackDurationSeconds > 0.0;
        }
        payload.trackProbeElapsedMs = phaseTimer.elapsed();
    }

    payload.totalElapsedMs = totalTimer.elapsed();
    payload.success = true;
    return payload;
}

bool writeDocumentFileAtomically(
    const QString& path,
    const SimaiDocument& document,
    QString* failedStage = nullptr)
{
    QStringEncoder encoder(QStringConverter::Utf8);
    const QByteArray payload = encoder.encode(document.toText());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("open_maidata");
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("write_maidata");
        }
        return false;
    }
    if (!file.commit()) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("commit_maidata");
        }
        return false;
    }
    return true;
}

}  // namespace

void miacode::runtime::DocumentSessionHost::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    const auto decide = [onDecided = std::move(onDecided)](bool leave) {
        if (onDecided) {
            onDecided(leave);
        }
    };

    runAutosaveCheck(false);
    // 由工作区提交当前输入并判断整文修改，运行时脏标记可能尚未同步。
    if (session_.qmlLeaveDocumentHandler_) {
        session_.qmlLeaveDocumentHandler_([decide](bool mayLeave) { decide(mayLeave); });
        return;
    }

    // 缺少处理器时，未保存的文档保留在窗口内。
    decide(!state_.documentDirty_ && !state_.currentFieldDirty_);
}

namespace {

QString cleanDropFolderName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    const QString device = name.section(QLatin1Char('.'), 0, 0).toUpper();
    const bool reservedDevice = QSet<QString>{QStringLiteral("CON"), QStringLiteral("PRN"),
                                              QStringLiteral("AUX"), QStringLiteral("NUL")}.contains(device);
    const bool numberedDevice = (device.startsWith(QStringLiteral("COM"))
                                 || device.startsWith(QStringLiteral("LPT")))
        && device.size() == 4 && device.at(3) >= QLatin1Char('1') && device.at(3) <= QLatin1Char('9');
    if (reservedDevice || numberedDevice) {
        name.prepend(QLatin1Char('_'));
    }
    name = name.left(80).trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    return name;
}

QString displayDropPath(const QString& path)
{
    const QFileInfo info(QDir::cleanPath(path));
    const QString absolutePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const QString root = absolutePath.size() >= 3 && absolutePath.at(1) == QLatin1Char(':')
        ? absolutePath.left(3)
        : QDir::toNativeSeparators(QDir::rootPath());
    const QString parentName = QDir(QDir::cleanPath(info.absolutePath())).dirName();
    const QString itemName = info.fileName();
    return QDir::toNativeSeparators(
        root + QStringLiteral("...\\") + parentName + QLatin1Char('\\') + itemName);
}

bool removeDropPath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return true;
    }
    return info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
}

} // namespace

miacode::DocumentImportAdapter miacode::runtime::DocumentSessionHost::chartDropImportAdapter()
{
    miacode::DocumentImportAdapter adapter;
    adapter.validate = [this](const QStringList& audioPaths, QString* error) {
        Q_UNUSED(error);
        const QStringList supported = miacode::chart_assets::supportedTrackFileExtensions();
        QList<DroppedChartCandidate> candidates;
        QHash<QString, QList<QFileInfo>> grouped;
        for (const QString& path : audioPaths) {
            const QFileInfo info(path);
            const QString extension = info.suffix().toLower();
            if (!info.isFile() || !supported.contains(extension)) {
                continue;
            }
            grouped[info.absolutePath().toCaseFolded()].append(info);
        }

        for (auto group = grouped.cbegin(); group != grouped.cend(); ++group) {
            const QList<QFileInfo>& droppedInDirectory = group.value();
            if (droppedInDirectory.size() > 1) {
                QSet<QString> reserved;
                for (const QFileInfo& info : droppedInDirectory) {
                    QString title = info.completeBaseName();
                    if (info.suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0) {
                        const auto tag = miacode::id3::readTagFromFile(info.absoluteFilePath());
                        if (!tag.title.trimmed().isEmpty()) {
                            title = tag.title.trimmed();
                        }
                    }
                    QString folder = cleanDropFolderName(title);
                    if (folder.isEmpty()) {
                        folder = QStringLiteral("Untitled");
                    }
                    QString target = QDir(info.absolutePath()).filePath(folder);
                    int suffix = 2;
                    while (QFileInfo::exists(target) || reserved.contains(target.toCaseFolded())) {
                        target = QDir(info.absolutePath()).filePath(
                            QStringLiteral("%1 (%2)").arg(folder).arg(suffix++));
                    }
                    reserved.insert(target.toCaseFolded());
                    candidates.append({info.absoluteFilePath(), info.absolutePath(), info.suffix().toLower(), target, {}});
                }
                continue;
            }

            const QFileInfo info = droppedInDirectory.first();
            QStringList audioInDirectory;
            const QDir directory(info.absolutePath());
            const QFileInfoList files = directory.entryInfoList(
                QStringList{QStringLiteral("*.mp3"), QStringLiteral("*.wav"), QStringLiteral("*.flac"), QStringLiteral("*.ogg")},
                QDir::Files | QDir::Readable, QDir::Name);
            for (const QFileInfo& file : files) {
                audioInDirectory.append(file.absoluteFilePath());
            }
            const bool sourceIsTrack = info.fileName().compare(
                QStringLiteral("track.%1").arg(info.suffix()), Qt::CaseInsensitive) == 0;
            bool hasTrack = false;
            for (const QString& trackName : miacode::chart_assets::trackCandidateFileNames()) {
                if (QFileInfo::exists(directory.filePath(trackName))) {
                    hasTrack = true;
                    break;
                }
            }
            if (audioInDirectory.size() > 1 && hasTrack && !sourceIsTrack) {
                audioInDirectory.removeAll(info.absoluteFilePath());
                candidates.append({info.absoluteFilePath(), info.absolutePath(), info.suffix().toLower(),
                                   info.absolutePath(), audioInDirectory});
                continue;
            }
            candidates.append({info.absoluteFilePath(), info.absolutePath(), info.suffix().toLower(),
                               info.absolutePath(), {}});
        }
        return candidates;
    };
    adapter.requestFirstConfirmation = [this](QList<DroppedChartCandidate>& candidates,
                                               std::function<void(bool)> onDecided) {
        miacode::UiRequestService* const requests = session_.uiRequestService();
        if (requests == nullptr) {
            if (onDecided) {
                onDecided(false);
            }
            return;
        }
        QList<DroppedChartCandidate>& prepared = candidates;
        bool hasFolderChoices = false;
        QStringList folderChoicePreview;
        int folderChoiceCount = 0;
        for (int index = 0; index < prepared.size(); ++index) {
            DroppedChartCandidate& candidate = prepared[index];
            if (candidate.extraAudioPaths.isEmpty()) {
                continue;
            }
            hasFolderChoices = true;
            ++folderChoiceCount;
            QString title = QFileInfo(candidate.sourcePath).completeBaseName();
            if (candidate.extension.compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0) {
                const auto tag = miacode::id3::readTagFromFile(candidate.sourcePath);
                if (!tag.title.trimmed().isEmpty()) {
                    title = tag.title.trimmed();
                }
            }
            QString folder = cleanDropFolderName(title);
            if (folder.isEmpty()) {
                folder = QStringLiteral("Untitled");
            }
            QString target = QDir(candidate.sourceDirectory).filePath(folder);
            int suffix = 2;
            while (QFileInfo::exists(target)) {
                target = QDir(candidate.sourceDirectory).filePath(
                    QStringLiteral("%1 (%2)").arg(folder).arg(suffix++));
            }
            folderChoicePreview << qtTrId("drop_chart.preview.multiple_audio");
            for (const QString& path : std::as_const(candidate.extraAudioPaths)) {
                folderChoicePreview << displayDropPath(path);
            }
            folderChoicePreview << qtTrId("drop_chart.preview.create_folder");
            folderChoicePreview << QDir::toNativeSeparators(QDir::cleanPath(target));
            candidate.targetDirectory = target;
        }

        if (folderChoiceCount == 1 && prepared.size() == 1) {
            const DroppedChartCandidate& candidate = prepared.constFirst();
            QString existingTrackName;
            for (const QString& trackName : miacode::chart_assets::trackCandidateFileNames()) {
                if (QFileInfo::exists(QDir(candidate.sourceDirectory).filePath(trackName))) {
                    existingTrackName = trackName;
                    break;
                }
            }
            QList<DroppedChartCandidate>* const preparedPtr = &prepared;
            requests->requestConfirmation(
                qtTrId("drop_chart.preview.title"),
                qtTrId("drop_chart.preview.single_track")
                    .arg(existingTrackName,
                         QDir::toNativeSeparators(QFileInfo(candidate.sourcePath).absoluteFilePath())),
                qtTrId("drop_chart.preview.create").arg(prepared.size()),
                [onDecided, preparedPtr](bool accepted) {
                    if (accepted) {
                        preparedPtr->first().extraAudioPaths.clear();
                    }
                    if (onDecided) {
                        onDecided(accepted);
                    }
                });
            return;
        }

        for (DroppedChartCandidate& candidate : prepared) {
            candidate.extraAudioPaths.clear();
        }

        const std::function<void(bool)> finishDecision = [onDecided](bool accepted) {
            if (onDecided) {
                onDecided(accepted);
            }
        };
        QStringList preview;
        QStringList conflicts;
        for (const DroppedChartCandidate& candidate : prepared) {
            preview << QDir::toNativeSeparators(QDir::cleanPath(candidate.targetDirectory));
            const QString targetDirectory = candidate.targetDirectory;
            if (!QFileInfo::exists(targetDirectory)) {
                continue;
            }
            const QString maidataPath = QDir(targetDirectory).filePath(QStringLiteral("maidata.txt"));
            const QFileInfo sourceInfo(candidate.sourcePath);
            for (const QString& trackName : miacode::chart_assets::trackCandidateFileNames()) {
                const QString trackPath = QDir(targetDirectory).filePath(trackName);
                if (QFileInfo::exists(trackPath)
                    && sourceInfo.absoluteFilePath().compare(QFileInfo(trackPath).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
                    conflicts << displayDropPath(trackPath);
                }
            }
            if (QFileInfo::exists(maidataPath)) {
                conflicts << displayDropPath(maidataPath);
            }
            const QString projectDataPath = QDir(targetDirectory).filePath(QStringLiteral(".miacode"));
            if (QFileInfo::exists(projectDataPath)) {
                conflicts << displayDropPath(projectDataPath);
            }
        }
        conflicts.removeDuplicates();
        if (hasFolderChoices) {
            preview << QString();
            preview.append(folderChoicePreview);
        }
        if (!conflicts.isEmpty()) {
            preview << QString();
            preview << qtTrId("drop_chart.preview.existing_project");
            preview.append(conflicts);
            preview << QString();
            preview << qtTrId("drop_chart.preview.overwrite_question");
        }
        requests->requestConfirmation(
            qtTrId("drop_chart.preview.title"),
            qtTrId("drop_chart.preview.message")
                .arg(prepared.size()) + preview.join(QLatin1Char('\n')),
            qtTrId("drop_chart.preview.create").arg(prepared.size()),
            finishDecision);
    };
    adapter.requestLeaveDocument = [this](std::function<void(bool)> onDecided) {
        requestLeaveDocument(std::move(onDecided));
    };
    adapter.createCharts = [this](const QList<DroppedChartCandidate>& candidates,
                                  std::function<void(const miacode::ChartDropCreateResult&)> onFinished) {
        QElapsedTimer timer;
        timer.start();
        finishChartsFromAudioDrop(candidates, timer, std::move(onFinished));
    };
    adapter.requestFinalSwitch = [this](const QString& target, std::function<void(bool)> onDecided) {
        miacode::UiRequestService* const requests = session_.uiRequestService();
        if (requests == nullptr) {
            if (onDecided) {
                onDecided(false);
            }
            return;
        }
        requests->requestConfirmation(
            qtTrId("drop_chart.created_title"),
            qtTrId("drop_chart.confirm_switch").arg(1),
            qtTrId("action.open"),
            [this, target, onDecided = std::move(onDecided)](bool accepted) mutable {
                if (accepted) {
                    session_.openStartupTarget(target);
                }
                if (onDecided) {
                    onDecided(accepted);
                }
            });
    };
    return adapter;
}

void miacode::runtime::DocumentSessionHost::finishChartsFromAudioDrop(
    const QList<DroppedChartCandidate>& candidates,
    QElapsedTimer dropTimer,
    std::function<void(const miacode::ChartDropCreateResult&)> onFinished)
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("create_confirmed count=%1").arg(candidates.size()));

    const SimaiDocument emptyDocument = SimaiDocument::createEmpty();
    int created = 0;
    int failed = 0;
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("batch_create_started count=%1").arg(candidates.size()));
    for (const DroppedChartCandidate& candidate : candidates) {
        const QString targetDirectory = candidate.targetDirectory;
        const QString trackPath = QDir(targetDirectory).filePath(
            QStringLiteral("track.%1").arg(candidate.extension));
        const QString maidataPath = QDir(targetDirectory).filePath(QStringLiteral("maidata.txt"));
        QString failedStage;
        bool renamed = false;
        bool copied = false;
        QList<QPair<QString, QString>> backups;
        bool ok = true;

        if (targetDirectory.compare(candidate.sourceDirectory, Qt::CaseInsensitive) != 0
            && !QDir().mkpath(targetDirectory)) {
            ++failed;
            continue;
        }

        const QString backupPrefix = QDir(targetDirectory).filePath(
            QStringLiteral(".miacode-drop-backup-%1-").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const auto backupExisting = [&backups, &backupPrefix](const QString& path) {
            if (!QFileInfo::exists(path)) {
                return true;
            }
            const QString backupPath = backupPrefix + QFileInfo(path).fileName();
            if (!QFile::rename(path, backupPath)) {
                return false;
            }
            backups.append({path, backupPath});
            return true;
        };
        const auto restoreBackups = [&backups]() {
            for (auto it = backups.crbegin(); it != backups.crend(); ++it) {
                removeDropPath(it->first);
                QFile::rename(it->second, it->first);
            }
        };

        const QFileInfo sourceInfo(candidate.sourcePath);
        const bool sourceIsTrack = sourceInfo.absoluteFilePath().compare(
            QFileInfo(trackPath).absoluteFilePath(), Qt::CaseInsensitive) == 0;
        for (const QString& trackName : miacode::chart_assets::trackCandidateFileNames()) {
            const QString existingTrackPath = QDir(targetDirectory).filePath(trackName);
            if (QFileInfo::exists(existingTrackPath)
                && sourceInfo.absoluteFilePath().compare(
                       QFileInfo(existingTrackPath).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
                failedStage = QStringLiteral("backup_track");
                ok = backupExisting(existingTrackPath);
                if (!ok) {
                    break;
                }
            }
        }
        if (ok && !sourceIsTrack) {
            if (targetDirectory.compare(candidate.sourceDirectory, Qt::CaseInsensitive) == 0) {
                failedStage = QStringLiteral("rename_audio");
                ok = QFile::rename(candidate.sourcePath, trackPath);
                renamed = ok;
            } else {
                failedStage = QStringLiteral("copy_audio");
                ok = QFile::copy(candidate.sourcePath, trackPath);
                copied = ok;
            }
        }
        if (ok && QFileInfo::exists(maidataPath)) {
            failedStage = QStringLiteral("backup_maidata");
            ok = backupExisting(maidataPath);
        }
        const QString projectDataPath = QDir(targetDirectory).filePath(QStringLiteral(".miacode"));
        QString projectDataBackup;
        if (ok && QFileInfo::exists(projectDataPath)) {
            projectDataBackup = backupPrefix + QStringLiteral(".miacode");
            failedStage = QStringLiteral("backup_project_data");
            ok = QFile::rename(projectDataPath, projectDataBackup);
            if (ok) {
                backups.append({projectDataPath, projectDataBackup});
            }
        }
        if (ok) {
            ok = writeDocumentFileAtomically(maidataPath, emptyDocument, &failedStage);
        }
        if (ok) {
            failedStage = QStringLiteral("create_project_data");
            ok = QDir().mkpath(projectDataPath);
        }
        if (!ok) {
            if (renamed) {
                QFile::rename(trackPath, candidate.sourcePath);
            }
            if (copied) {
                QFile::remove(trackPath);
            }
            restoreBackups();
        } else {
            for (const auto& backup : std::as_const(backups)) {
                removeDropPath(backup.second);
            }
        }
        if (!ok) {
            ++failed;
            miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/chart_drop"),
                QStringLiteral("chart_create_failed stage=%1 format=%2 created=%3 failed=%4 elapsed_ms=%5")
                    .arg(failedStage, candidate.extension)
                    .arg(created).arg(failed).arg(dropTimer.elapsed()));
            continue;
        }
        ++created;
        miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
            QStringLiteral("ui/chart_drop"),
            QStringLiteral("chart_create_succeeded format=%1").arg(candidate.extension));
    }

    miacode::UiRequestService* const requests = session_.uiRequestService();
    if (requests != nullptr) {
        if (created == 0) {
            requests->postNotice(miacode::NoticeSeverity::Error,
                qtTrId("drop_chart.error.title"),
                qtTrId("drop_chart.create_failed"));
        } else if (failed > 0) {
            requests->postNotice(miacode::NoticeSeverity::Warning,
                qtTrId("drop_chart.created_title"),
                qtTrId("drop_chart.created_with_failures")
                    .arg(created).arg(failed));
        }
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("batch_create_finished count=%1 elapsed_ms=%2")
            .arg(created).arg(dropTimer.elapsed()));
    if (onFinished) {
        onFinished({
            created,
            failed,
            candidates.size() == 1 && created == 1
                ? candidates.first().targetDirectory
                : QString(),
        });
    }
}

bool miacode::runtime::DocumentSessionHost::openFileAtPath(const QString& path, bool showErrors)
{
    MC_OP("miacode::runtime::DocumentSessionHost::openFileAtPath");
    _mc_op_.note(QStringLiteral("path=%1").arg(path));
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty path"));
        return false;
    }

    cancelPendingStartupRestore();
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
    if (!payload.success) {
        if (showErrors) {
            if (miacode::UiRequestService* const requests = session_.uiRequestService()) {
                requests->postNotice(
                    miacode::NoticeSeverity::Error,
                    QStringLiteral("Open Failed"),
                    QStringLiteral("Cannot open file:\n") + normalizedPath
                );
            }
        }
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }

    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8,
        payload.document,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

bool miacode::runtime::DocumentSessionHost::restoreLastSessionFile()
{
    MC_OP("miacode::runtime::DocumentSessionHost::restoreLastSessionFile");
    _mc_op_.note(QStringLiteral("path=%1").arg(state_.lastSessionFilePath_));
    if (state_.lastSessionFilePath_.isEmpty()) {
        return false;  // not a failure — first run or cleared session
    }
    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        _mc_op_.fail(QStringLiteral("session_file_missing"));
        state_.lastSessionFilePath_.clear();
        return false;
    }
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(fileInfo.absoluteFilePath(), true);
    if (!payload.success) {
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }
    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8,
        payload.document,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

void miacode::runtime::DocumentSessionHost::scheduleStartupRestoreLastSessionFile()
{
    if (!state_.autoRestoreLastSessionFile_ || state_.lastSessionFilePath_.isEmpty()) {
        state_.startupRestorePending_ = false;
        return;
    }

    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        state_.lastSessionFilePath_.clear();
        state_.startupRestorePending_ = false;
        return;
    }

    state_.startupRestorePending_ = true;
    const quint64 generation = ++state_.startupRestoreGeneration_;
    const QString normalizedPath = fileInfo.absoluteFilePath();
    QPointer<Session> guard(&session_);
    QThreadPool* const pool = state_.previewWarmupPool_ != nullptr
        ? state_.previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, normalizedPath]() {
        const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, payload]() {
                if (guard.isNull() || generation != guard->state_.startupRestoreGeneration_ || !guard->state_.startupRestorePending_) {
                    return;
                }

                guard->state_.startupRestorePending_ = false;
                if (!payload.success) {
                    appendStartupTimingStage("mainwindow/startup_restore_prepare_failed", 0, 0);
                    guard->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
                    return;
                }

                Session::PreparedStartupRestoreDocument prepared;
                prepared.generation = generation;
                prepared.normalizedPath = payload.normalizedPath;
                prepared.document = payload.document;
                prepared.encodingUsed = payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8;
                prepared.resolvedTrackPath = payload.resolvedTrackPath;
                prepared.trackDurationSeconds = payload.trackDurationSeconds;
                prepared.hasTrackDuration = payload.hasTrackDuration;
                prepared.readElapsedMs = payload.readElapsedMs;
                prepared.decodeElapsedMs = payload.decodeElapsedMs;
                prepared.parseElapsedMs = payload.parseElapsedMs;
                prepared.trackProbeElapsedMs = payload.trackProbeElapsedMs;
                prepared.totalElapsedMs = payload.totalElapsedMs;
                guard->applyPreparedStartupRestoreDocument(prepared);
            },
            Qt::QueuedConnection
        );
    });
}

void miacode::runtime::DocumentSessionHost::applyPreparedStartupRestoreDocument(const Session::PreparedStartupRestoreDocument& prepared)
{
    if (prepared.generation != state_.startupRestoreGeneration_) {
        return;
    }

    appendStartupTimingStage("mainwindow/startup_restore_read_bytes", prepared.readElapsedMs, prepared.readElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_decode_text", prepared.decodeElapsedMs, prepared.decodeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_parse_document", prepared.parseElapsedMs, prepared.parseElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_probe_track_duration", prepared.trackProbeElapsedMs, prepared.trackProbeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_prepare_total", prepared.totalElapsedMs, prepared.totalElapsedMs);

    QElapsedTimer applyTimer;
    applyTimer.start();
    applyOpenedDocumentState(
        prepared.normalizedPath,
        prepared.encodingUsed,
        prepared.document,
        prepared.hasTrackDuration ? prepared.trackDurationSeconds : -1.0
    );
    const qint64 applyElapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/startup_restore_apply_document_ui", applyElapsedMs, applyElapsedMs);
    appendStartupTimingStage(
        "mainwindow/restored_last_document_applied",
        prepared.totalElapsedMs + applyElapsedMs,
        prepared.totalElapsedMs + applyElapsedMs
    );
    session_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void miacode::runtime::DocumentSessionHost::applyOpenedDocumentState(
    const QString& normalizedPath,
    Session::TextEncoding encodingUsed,
    const SimaiDocument& document,
    double knownTrackDurationSeconds)
{
    MC_OP("miacode::runtime::DocumentSessionHost::applyOpenedDocumentState");
    _mc_op_.note(QStringLiteral("path=%1 dur=%2")
                     .arg(normalizedPath)
                     .arg(knownTrackDurationSeconds, 0, 'f', 3));
    state_.currentEncoding_ = encodingUsed;
    session_.applyWaveformData(
        miacode::waveform::makeWaveformPlaceholder(
            knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0));
    session_.setCurrentFilePath(normalizedPath, true);
    session_.addRecentFilePath(normalizedPath);

    // Eagerly create the crash-recovery directory BEFORE the user can
    // edit. Without this, a crash in the first ~1 ms after a keystroke
    // (before the lazy mkpath inside updateSnapshot has run) would find
    // the parent directory missing and fail CreateFileW. mkpath is
    // re-entrant and cheap on warm runs (one stat()).
    miacode::crash_recovery::prepareForChart(normalizedPath);

    // Abnormal-exit recovery intentionally reuses File -> Restore Backup.
    // Opening the chart must finish first so the restore prompt appears over
    // the fully loaded window and the old on-disk content remains the restore
    // baseline, exactly like a manual menu action.
    const bool previousSessionAbandoned =
        miacode::crash_recovery::consumeAbandonedSessionChartMatch(normalizedPath);
    const QString crashRecoveryPath = miacode::crash_recovery::crashRecoveryFilePath(normalizedPath);
    const bool crashRecoveryFileExists =
        !crashRecoveryPath.isEmpty() && QFileInfo(crashRecoveryPath).exists();
    if (previousSessionAbandoned || crashRecoveryFileExists) {
        state_.pendingAbnormalExitBackupRestorePath_ =
            latestBackupRestoreFilePathForChart(normalizedPath);
        state_.pendingAbnormalExitBackupRestoreChartPath_ =
            state_.pendingAbnormalExitBackupRestorePath_.isEmpty() ? QString() : normalizedPath;
        if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("crash_recovery"),
                QStringLiteral("action=defer_restore_backup path=%1 chart=%2")
                    .arg(state_.pendingAbnormalExitBackupRestorePath_, normalizedPath));
        }
    }

    miacode::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    const QString source = document.toText();
    const miacode::ChartWorkspaceSnapshot snapshot = workspace.snapshot();
    if (!snapshot.hasDocument
        || snapshot.filePath != normalizedPath
        || workspace.document().toText() != source) {
        workspace.openSource(source, normalizedPath);
    }
    // Reads the project preference and reconciles it with what was just
    // loaded. Never writes a document field: the chart stays byte for byte
    // what is on disk, so this open cannot arrive dirty.
    reconcileUnifiedDocumentDesigner(
        miacode::DocumentBridge::UnifiedDesignerReconcileReason::DocumentOpened);
    loadDocument();
    session_.refreshWaveformCache(knownTrackDurationSeconds);
    if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
        schedulePendingAbnormalExitBackupRestore();
    }
}

void miacode::runtime::DocumentSessionHost::resetWorkingPosition()
{
    if (!state_.backendActive_) {
        return;
    }
    session_.resetWorkingPositionPending_ = true;
    state_.pendingDifficultySwitchPreviewRestore_ = false;
    state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
    state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
    state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
    session_.setTouchPadAuthoringAnchor(-1.0, -1.0);
    clearTimelineAndPreview();
    if (auto* authority = session_.applicationServices_.playbackStateAuthority(); authority != nullptr) {
        authority->repositionSilently(0.0, "reset_working_position");
    }
}

void miacode::runtime::DocumentSessionHost::syncRuntimeFromWorkspace()
{
    const miacode::ChartWorkspaceSnapshot snapshot =
        session_.applicationServices_.workspace().snapshot();
    if (snapshot.revision <= session_.appliedQmlWorkspaceRevision_) {
        return;
    }
    session_.appliedQmlWorkspaceRevision_ = snapshot.revision;

    const quint64 previousOpenGeneration = session_.appliedDocumentOpenGeneration_;
    const bool documentIdentityChanged =
        snapshot.documentOpenGeneration != previousOpenGeneration;
    session_.appliedDocumentOpenGeneration_ = snapshot.documentOpenGeneration;
    const bool resetInheritedWorkingPosition =
        documentIdentityChanged && previousOpenGeneration != 0 && state_.backendActive_;

    if (!snapshot.hasDocument) {
        state_.documentDirty_ = false;
        state_.currentFieldDirty_ = false;
        state_.activeDifficultyId_ = 0;
        session_.setCurrentFilePath(QString(), true);
        if (resetInheritedWorkingPosition) {
            resetWorkingPosition();
        }
        session_.resetWorkingPositionPending_ = false;
        return;
    }

    if (resetInheritedWorkingPosition) {
        resetWorkingPosition();
        state_.activeDifficultyId_ = 0;
    }

    const bool pathChanged = snapshot.filePath != state_.currentFilePath_;
    if (pathChanged) {
        session_.setCurrentFilePath(snapshot.filePath, true);
        if (!snapshot.filePath.isEmpty()) {
            // An untitled document had nowhere to record a shared-designer
            // choice; the save that just gave it a path also gives it one.
            flushPendingUnifiedDesignerPreference();
            session_.addRecentFilePath(snapshot.filePath);
            miacode::crash_recovery::prepareForChart(snapshot.filePath);
            const bool previousSessionAbandoned =
                miacode::crash_recovery::consumeAbandonedSessionChartMatch(snapshot.filePath);
            const QString crashRecoveryPath =
                miacode::crash_recovery::crashRecoveryFilePath(snapshot.filePath);
            const bool crashRecoveryFileExists =
                !crashRecoveryPath.isEmpty() && QFileInfo(crashRecoveryPath).exists();
            if (previousSessionAbandoned || crashRecoveryFileExists) {
                state_.pendingAbnormalExitBackupRestorePath_ =
                    latestBackupRestoreFilePathForChart(snapshot.filePath);
                state_.pendingAbnormalExitBackupRestoreChartPath_ =
                    state_.pendingAbnormalExitBackupRestorePath_.isEmpty()
                        ? QString()
                        : snapshot.filePath;
                if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
                    schedulePendingAbnormalExitBackupRestore();
                }
            }
        }
        // The workspace path is authoritative for the document, while the
        // preview duration still needs the media length before the user can
        // start playback. Keep the same eager duration handoff as the native
        // open path; waveform extraction remains asynchronous below it.
        const double knownTrackDurationSeconds =
            probeAudioDurationSeconds(state_.lastTrackPath_);
        session_.refreshWaveformCache(knownTrackDurationSeconds);
        resetAutosaveState(snapshot.sourceText);
    }

    state_.documentDirty_ = snapshot.dirty;
    state_.currentFieldDirty_ = false;
    if (snapshot.dirty) {
        noteDocumentEditedForAutosave();
    }
    // Overlay audition pages deliberately keep their source outside the
    // workspace's active difficulty. A metadata-only workspace write must not
    // turn that intentional mismatch into a real difficulty switch.
    const bool auditionPageOwnsSource =
        state_.latencySandboxAuditionActive_ || state_.exportPreviewAuditionActive_;
    const bool difficultyChanged =
        !auditionPageOwnsSource
        && SimaiDocument::isDifficultyId(snapshot.activeDifficultyId)
        && snapshot.activeDifficultyId != state_.activeDifficultyId_;
    if (difficultyChanged) {
        switchToDifficultyField(snapshot.activeDifficultyId);
    } else {
        session_.resetWorkingPositionPending_ = false;
        session_.scheduleTimelineRefresh();
    }
}
