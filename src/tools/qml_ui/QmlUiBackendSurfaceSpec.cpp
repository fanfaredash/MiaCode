// Ratchet for docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md.
//
// Stage 3.5 item 2 of docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md wants
// QmlApplicationContext to stop holding a MainWindow&. That is not one edit —
// it is roughly 120 method calls plus 17 direct reads of MainWindow's private
// members, spread across the Qml*Model façades. A goal that size regresses
// silently unless the remaining surface is a number somebody can see.
//
// So this spec compares, by set equality, what src/app/qml_ui/ actually reaches
// on the hidden window against the inventory the doc lists:
//
//   * a name in the code but not in the doc  -> new coupling, rejected;
//   * a name in the doc but not in the code  -> it was migrated and the doc was
//     not updated, also rejected — that is what forces the count down in the
//     same commit as the work.
//
// It also pins the `friend class` grants MainWindow hands to QML types. Those
// are the opposite of the "narrow QObject façade" the stage asks for: a friend
// is not a narrow interface, it is no interface, which is why the doc says to
// clear them before trimming public methods.
//
// The repo root arrives as MIACODE_SOURCE_ROOT, so the spec reads the tree and
// the doc from disk instead of embedding a copy of either list.

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

bool require(bool condition, const QString& message)
{
    if (!condition) {
        err() << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString readFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QStringList sorted(const QSet<QString>& values)
{
    QStringList list(values.cbegin(), values.cend());
    list.sort();
    return list;
}

bool requireSetEquals(const QSet<QString>& actual,
                      const QSet<QString>& documented,
                      const QString& what)
{
    const QSet<QString> undocumented = actual - documented;
    const QSet<QString> stale = documented - actual;
    bool ok = require(undocumented.isEmpty(),
                      what + QStringLiteral(": reached in code but not listed (new coupling) -> ")
                          + sorted(undocumented).join(QStringLiteral(", ")));
    ok &= require(stale.isEmpty(),
                  what + QStringLiteral(": listed but no longer reached — delete the row in "
                                        "this commit -> ")
                      + sorted(stale).join(QStringLiteral(", ")));
    return ok;
}

// `backend_` is a raw MainWindow reference in the Qml*Model façades and a
// std::unique_ptr<MainWindow> in QmlUiBootstrap. These three are the smart
// pointer's own handle API, not calls on the window.
const QSet<QString>& smartPointerHandleMethods()
{
    static const QSet<QString> methods = {
        QStringLiteral("get"),
        QStringLiteral("reset"),
        QStringLiteral("release"),
    };
    return methods;
}

struct BackendSurface {
    QSet<QString> methods;
    QSet<QString> privateMembers;
    int filesScanned = 0;
};

BackendSurface scanQmlUiTree()
{
    BackendSurface surface;
    // `backend_->name` / `backend_.name` / `backend.name` — the constructor
    // parameter is spelled without the trailing underscore.
    static const QRegularExpression reach(
        QStringLiteral("backend_(?:->|\\.)([A-Za-z_][A-Za-z0-9_]*)"
                       "|\\bbackend\\.([A-Za-z_][A-Za-z0-9_]*)"));

    QDirIterator walk(QStringLiteral(MIACODE_SOURCE_ROOT) + QStringLiteral("/src/app/qml_ui"),
                      QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                                  QStringLiteral("*.mm")},
                      QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        QFile file(walk.next());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        ++surface.filesScanned;
        auto it = reach.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            const QString name = match.captured(1).isEmpty() ? match.captured(2)
                                                             : match.captured(1);
            // A call is followed by '('; anything else is a member read.
            const QString rest = text.mid(match.capturedEnd(), 4).trimmed();
            if (rest.startsWith(QLatin1Char('('))) {
                if (!smartPointerHandleMethods().contains(name)) {
                    surface.methods.insert(name);
                }
            } else {
                surface.privateMembers.insert(name);
            }
        }
    }
    return surface;
}

// Every `- \`name\`` bullet in the doc's inventory section.
QSet<QString> collectInventory(const QString& doc)
{
    QSet<QString> names;
    const qsizetype start = doc.indexOf(QStringLiteral("\n## 清单"));
    if (start < 0) {
        return names;
    }
    qsizetype end = doc.indexOf(QStringLiteral("\n## 更新规则"), start);
    if (end < 0) {
        end = doc.size();
    }
    static const QRegularExpression bullet(QStringLiteral("^- `([A-Za-z_][A-Za-z0-9_]*)`"),
                                           QRegularExpression::MultilineOption);
    auto it = bullet.globalMatch(doc.mid(start, end - start));
    while (it.hasNext()) {
        names.insert(it.next().captured(1));
    }
    return names;
}

// Every first-column `\`Type\`` cell of the friend-grant table.
QSet<QString> collectDocumentedFriends(const QString& doc)
{
    QSet<QString> names;
    const qsizetype start = doc.indexOf(QStringLiteral("## friend 授权"));
    if (start < 0) {
        return names;
    }
    qsizetype end = doc.indexOf(QStringLiteral("\n## "), start + 8);
    if (end < 0) {
        end = doc.size();
    }
    static const QRegularExpression row(QStringLiteral("^\\|\\s*`([^`]+)`\\s*\\|"),
                                        QRegularExpression::MultilineOption);
    auto it = row.globalMatch(doc.mid(start, end - start));
    while (it.hasNext()) {
        names.insert(it.next().captured(1).trimmed());
    }
    return names;
}

// `friend class X;` in MainWindow.h, minus the widget-side one the doc
// deliberately excludes.
QSet<QString> collectQmlFriendGrants(const QString& mainWindowHeader)
{
    QSet<QString> names;
    static const QRegularExpression grant(
        QStringLiteral("friend\\s+class\\s+([A-Za-z_][A-Za-z0-9_:]*)\\s*;"));
    auto it = grant.globalMatch(mainWindowHeader);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (name.contains(QStringLiteral("latency::"))) {
            continue;
        }
        names.insert(name);
    }
    return names;
}

// The recorded counts must match reality, so the doc cannot advertise progress
// it did not make.
bool verifyRecordedCounts(const QString& doc, const BackendSurface& surface, int friendCount)
{
    static const QRegularExpression counts(
        // The counts sentence wraps across a blockquote line, so allow the
        // "> " continuation between the second and third number.
        QStringLiteral("方法 \\*\\*([0-9]+)\\*\\*，直接读取的 `MainWindow` 私有成员 "
                       "\\*\\*([0-9]+)\\*\\*，[\\s>]*friend 授权 \\*\\*([0-9]+)\\*\\*"));
    const auto match = counts.match(doc);
    if (!require(match.hasMatch(), QStringLiteral("the doc records the three counts"))) {
        return false;
    }
    bool ok = require(match.captured(1).toInt() == surface.methods.size(),
                      QStringLiteral("recorded method count %1 matches the %2 actually reached")
                          .arg(match.captured(1))
                          .arg(surface.methods.size()));
    ok &= require(match.captured(2).toInt() == surface.privateMembers.size(),
                  QStringLiteral("recorded private-member count %1 matches the %2 actually read")
                      .arg(match.captured(2))
                      .arg(surface.privateMembers.size()));
    ok &= require(match.captured(3).toInt() == friendCount,
                  QStringLiteral("recorded friend-grant count %1 matches the %2 in MainWindow.h")
                      .arg(match.captured(3))
                      .arg(friendCount));
    return ok;
}

}  // namespace

int main()
{
    const QString doc = readFile(QStringLiteral("docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md"));
    const QString mainWindowHeader = readFile(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    bool ok = require(!doc.isEmpty(),
                      QStringLiteral("docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md is readable"));
    ok &= require(!mainWindowHeader.isEmpty(),
                  QStringLiteral("src/app/mainwindow/MainWindow.h is readable"));
    if (!ok) {
        return 1;
    }

    const BackendSurface surface = scanQmlUiTree();
    ok &= require(surface.filesScanned > 0,
                  QStringLiteral("the src/app/qml_ui scan found files"));

    const QSet<QString> inventory = collectInventory(doc);
    ok &= require(!inventory.isEmpty(), QStringLiteral("the inventory section has entries"));
    ok &= requireSetEquals(surface.methods + surface.privateMembers, inventory,
                           QStringLiteral("MainWindow surface reached from src/app/qml_ui"));

    const QSet<QString> friends = collectQmlFriendGrants(mainWindowHeader);
    ok &= requireSetEquals(friends, collectDocumentedFriends(doc),
                           QStringLiteral("MainWindow friend grants to QML types"));

    ok &= verifyRecordedCounts(doc, surface, friends.size());

    if (ok) {
        QTextStream(stdout)
            << "qml_ui_backend_surface_spec: OK — " << surface.methods.size()
            << " methods, " << surface.privateMembers.size() << " private members, "
            << friends.size() << " friend grants remaining" << Qt::endl;
    }
    return ok ? 0 : 1;
}
