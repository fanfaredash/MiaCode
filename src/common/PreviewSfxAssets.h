#pragma once

#include "common/AssetPaths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace miacode::preview_sfx {

inline QStringList assetFileNamesForKind(const QString& kind)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered == QStringLiteral("answer")) {
        return {QStringLiteral("answer.wav")};
    }
    if (lowered == QStringLiteral("judge")) {
        return {QStringLiteral("tap_perfect.wav"), QStringLiteral("judge.wav")};
    }
    if (lowered == QStringLiteral("judge_break") || lowered == QStringLiteral("break_touch")) {
        return {QStringLiteral("break_tap.wav"), QStringLiteral("judge_break.wav")};
    }
    if (lowered == QStringLiteral("slide")) {
        return {QStringLiteral("slide.wav")};
    }
    if (lowered == QStringLiteral("break")) {
        return {QStringLiteral("break.wav")};
    }
    if (lowered == QStringLiteral("break_slide_start")) {
        return {QStringLiteral("slide_break_start.wav"), QStringLiteral("break_slide_start.wav")};
    }
    if (lowered == QStringLiteral("break_slide") || lowered == QStringLiteral("break_slide_finish")) {
        return {QStringLiteral("slide_break_slide.wav"), QStringLiteral("break_slide.wav")};
    }
    if (lowered == QStringLiteral("break_slide_tail_break")) {
        return {QStringLiteral("break.wav")};
    }
    if (lowered == QStringLiteral("judge_break_slide")) {
        return {QStringLiteral("slide_break_slide.wav"), QStringLiteral("judge_break_slide.wav")};
    }
    if (lowered == QStringLiteral("ex")) {
        return {QStringLiteral("tap_ex.wav"), QStringLiteral("judge_ex.wav")};
    }
    if (lowered == QStringLiteral("touch")) {
        return {QStringLiteral("touch.wav")};
    }
    if (lowered == QStringLiteral("touchhold")) {
        return {QStringLiteral("touch_Hold_riser.wav"), QStringLiteral("touchHold_riser.wav")};
    }
    if (lowered == QStringLiteral("firework")) {
        return {QStringLiteral("touch_hanabi.wav"), QStringLiteral("firework.wav"), QStringLiteral("hanabi.wav")};
    }
    if (lowered == QStringLiteral("clock")) {
        return {QStringLiteral("clock.wav")};
    }
    if (lowered == QStringLiteral("track_start")) {
        return {QStringLiteral("track_start.wav")};
    }
    return {};
}

inline QString assetFileNameForKind(const QString& kind)
{
    const QStringList fileNames = assetFileNamesForKind(kind);
    return fileNames.isEmpty() ? QString() : fileNames.constFirst();
}

inline QString assetFilePathForKind(const QString& sfxDir, const QString& kind)
{
    if (sfxDir.isEmpty()) {
        return QString();
    }
    const QStringList fileNames = assetFileNamesForKind(kind);
    if (fileNames.isEmpty()) {
        return QString();
    }
    const QDir dir(sfxDir);
    for (const QString& fileName : fileNames) {
        const QString path = QDir::cleanPath(dir.filePath(fileName));
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QDir::cleanPath(dir.filePath(fileNames.constFirst()));
}

inline QByteArray encodedAssetFilePathForKind(const QString& sfxDir, const QString& kind)
{
    const QString path = assetFilePathForKind(sfxDir, kind);
    if (path.isEmpty()) {
        return QByteArray();
    }
    return QFile::encodeName(path);
}

inline QString resolveSfxDirectory()
{
    const auto hasAnswerClip = [](const QString& path) {
        const QString answerPath = assetFilePathForKind(path, QStringLiteral("answer"));
        return !answerPath.isEmpty() && QFileInfo::exists(answerPath);
    };

    const QString envDir = QDir::cleanPath(
        qEnvironmentVariable("MIACODE_PREVIEW_SFX_DIR", qEnvironmentVariable("MAIMURI_PREVIEW_SFX_DIR")).trimmed()
    );
    if (!envDir.isEmpty() && hasAnswerClip(envDir)) {
        return envDir;
    }

    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString& candidate) {
        if (candidate.isEmpty()) {
            return;
        }
        const QString cleanPath = QDir::cleanPath(candidate);
        if (!candidates.contains(cleanPath)) {
            candidates.append(cleanPath);
        }
    };

    appendCandidate(miacode::assets::assetPath("SFX"));
    appendCandidate(miacode::assets::assetPath("sfx"));

    const QDir appDir(QCoreApplication::applicationDirPath());
    appendCandidate(appDir.filePath("assets/SFX"));
    appendCandidate(appDir.filePath("SFX"));
    appendCandidate(appDir.filePath("sfx"));
    appendCandidate(appDir.filePath("../Resources/assets/SFX"));

    for (const QString& candidate : candidates) {
        if (hasAnswerClip(candidate)) {
            return candidate;
        }
    }
    return QString();
}

}  // namespace miacode::preview_sfx
