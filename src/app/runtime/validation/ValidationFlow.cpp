#include "runtime/validation/ValidationHost.h"
#include <QtCore>

miacode::runtime::ValidationHost::ValidationHost(
    Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

namespace {

enum class ValidationSeverityLevel {
    Error,
    Warning,
};

struct ValidationMessageParts {
    QString severityPrefix;
    QString body;
    ValidationSeverityLevel severity = ValidationSeverityLevel::Error;
};

ValidationMessageParts parseValidationMessage(const QString& rawMessage)
{
    ValidationMessageParts parts;
    parts.body = rawMessage.trimmed();
    const auto matchPrefix = [&](const QString& prefix, ValidationSeverityLevel severity) -> bool {
        if (!parts.body.startsWith(prefix)) {
            return false;
        }
        parts.severityPrefix = prefix;
        parts.severity = severity;
        parts.body = parts.body.mid(prefix.size()).trimmed();
        return true;
    };

    if (matchPrefix(QStringLiteral("[ERROR]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[WARNING]"), ValidationSeverityLevel::Warning)) return parts;
    if (matchPrefix(QStringLiteral("[错误]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[警告]"), ValidationSeverityLevel::Warning)) return parts;

    parts.severityPrefix = QStringLiteral("[ERROR]");
    parts.severity = ValidationSeverityLevel::Error;
    return parts;
}



}  // namespace

void miacode::runtime::ValidationHost::clearValidationDecorations()
{
    state_.validationDecorations_.clear();
}

void miacode::runtime::ValidationHost::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }
    if (endCol < col) {
        endCol = col;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    Session::ValidationDecoration decoration;
    decoration.line = qMax(1, line);
    decoration.col = qMax(1, col);
    decoration.endCol = qMax(decoration.col, endCol);
    decoration.message = message;
    decoration.warning = (parts.severity == ValidationSeverityLevel::Warning);
    state_.validationDecorations_.append(decoration);
}

void Session::clearPreviewFollowDecoration()
{
    if (editorSyncController_ != nullptr) {
        miacode::v2::EditorFollowState follow;
        follow.playbackActive = playing_;
        editorSyncController_->publishFollow(follow);
    }
}

void Session::clearValidationDecorations()
{
    validation_->clearValidationDecorations();
}

void Session::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    validation_->addValidationDecoration(line, col, message, endCol);
}
