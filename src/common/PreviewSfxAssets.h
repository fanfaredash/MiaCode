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

inline QString assetFileNameForKind(const QString& kind)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered == QStringLiteral("answer")) {
        return QStringLiteral("answer.wav");
    }
    if (lowered == QStringLiteral("judge")) {
        return QStringLiteral("judge.wav");
    }
    if (lowered == QStringLiteral("judge_break") || lowered == QStringLiteral("break_touch")) {
        return QStringLiteral("judge_break.wav");
    }
    if (lowered == QStringLiteral("slide")) {
        return QStringLiteral("slide.wav");
    }
    if (lowered == QStringLiteral("break")) {
        return QStringLiteral("break.wav");
    }
    if (lowered == QStringLiteral("break_slide_start")) {
        return QStringLiteral("break_slide_start.wav");
    }
    if (lowered == QStringLiteral("break_slide")) {
        return QStringLiteral("break_slide.wav");
    }
    if (lowered == QStringLiteral("judge_break_slide")) {
        return QStringLiteral("judge_break_slide.wav");
    }
    if (lowered == QStringLiteral("ex")) {
        return QStringLiteral("judge_ex.wav");
    }
    if (lowered == QStringLiteral("touch")) {
        return QStringLiteral("touch.wav");
    }
    if (lowered == QStringLiteral("touchhold")) {
        return QStringLiteral("touchHold_riser.wav");
    }
    if (lowered == QStringLiteral("firework")) {
        return QStringLiteral("firework.wav");
    }
    return QString();
}

inline QString assetFilePathForKind(const QString& sfxDir, const QString& kind)
{
    if (sfxDir.isEmpty()) {
        return QString();
    }
    const QString fileName = assetFileNameForKind(kind);
    if (fileName.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QDir(sfxDir).filePath(fileName));
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
