#pragma once

#include <functional>

#include <QElapsedTimer>
#include <QRect>

class QWindow;
class QString;
class Session;

namespace miacode::runtime {

// QML owns the visible shell. This host now contains only the small lifecycle
// bridge that still belongs to Session: root-window bookkeeping and the
// asynchronous close transaction. Widget focus, layout and styling code was
// part of the retired native shell and is no longer in the product target.
class ShellHost final {
public:
    explicit ShellHost(::Session& session);

    void requestShellClose(std::function<void(bool)> onDecided);
    bool finishShellClose(QElapsedTimer totalTimer);
    void appendOutput(const QString& scope, const QString& payload) const;

private:
    ::Session& session_;
};

}  // namespace miacode::runtime
