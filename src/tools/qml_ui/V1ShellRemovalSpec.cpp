// Drift guard pinning the removal of the v1 QuickShell UI shell.
//
// This spec is EXPECTED TO FAIL until stage 0a of the v1 removal plan
// completes — see docs/specs/ui/plans/2026-08-25-v2-stage0a-remove-v1-shell.md.
// A red result here is the deliberate starting state, not a regression.
//
// The "QuickShellController is still present for v2" assertion pins it as a
// survivor (v2 still uses it) — this spec only checks that it no longer
// branches on a native surface host. When a later stage retires
// QuickShellController itself, this spec must be updated to match rather
// than having its failure read as a regression.

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    out.flush();
    if (!condition) ++*failed;
    return condition;
}

QString sourceRoot() { return QStringLiteral(MIACODE_SOURCE_ROOT); }

QString readSource(const QString& relativePath)
{
    QFile file(sourceRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    // v1 外壳的启动编排、原生表面再宿主、主题桥与 macOS 表面支持全部删除。
    // QuickShellController 不在此列：v2 仍在使用，按架构设计阶段 2 退役。
    const QStringList removedFiles{
        QStringLiteral("src/app/quick_shell/QuickShellBootstrap.h"),
        QStringLiteral("src/app/quick_shell/QuickShellBootstrap.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellNativeSurfaceHost.h"),
        QStringLiteral("src/app/quick_shell/QuickShellNativeSurfaceHost.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellStyleBridge.h"),
        QStringLiteral("src/app/quick_shell/QuickShellStyleBridge.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellMacSurfaceSupport.h"),
        QStringLiteral("src/app/quick_shell/QuickShellMacSurfaceSupport.mm"),
        QStringLiteral("resources/quick_shell_qml.qrc"),
    };
    QStringList survivors;
    for (const QString& path : removedFiles) {
        if (QFileInfo::exists(sourceRoot() + QLatin1Char('/') + path)) survivors.append(path);
    }
    if (!survivors.isEmpty()) out << "  still present: " << survivors.join(QStringLiteral(", ")) << '\n';
    expect(survivors.isEmpty(), QStringLiteral("v1 shell sources are gone"), out, &failed);

    expect(!QFileInfo::exists(sourceRoot() + QStringLiteral("/src/app/quick_shell/qml")),
           QStringLiteral("the v1 shell QML directory is gone"), out, &failed);

    // 只剩一条 UI 启动路径。
    const QString mainSource = readSource(QStringLiteral("src/app/main.cpp"));
    expect(!mainSource.isEmpty(), QStringLiteral("main.cpp is readable"), out, &failed);
    QStringList entryLeftovers;
    for (const QString& token : {QStringLiteral("UiSkin"), QStringLiteral("resolveUiSkin"),
                                 QStringLiteral("MIACODE_UI_SKIN"), QStringLiteral("--ui=")}) {
        if (mainSource.contains(token)) entryLeftovers.append(token);
    }
    if (!entryLeftovers.isEmpty()) {
        out << "  main.cpp still carries: " << entryLeftovers.join(QStringLiteral(", ")) << '\n';
    }
    expect(entryLeftovers.isEmpty(),
           QStringLiteral("main.cpp has a single UI entry with no skin switch"), out, &failed);

    // 入口 flag 的残留不限于 main.cpp：一处写在别处的 `--ui=v1` 注释会静默活过整个阶段，
    // 因为它既不是 MIACODE_* 环境变量（debug_flag_index_spec 管不到），也不在 main.cpp 里。
    QStringList entryFlagMentions;
    QDirIterator sourceIt(sourceRoot() + QStringLiteral("/src"),
                          {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.mm"),
                           QStringLiteral("*.qml")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (sourceIt.hasNext()) {
        const QString path = sourceIt.next();
        // This spec pins the token itself, so its own source is not a leftover.
        if (sourceIt.fileName() == QStringLiteral("V1ShellRemovalSpec.cpp")) continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        if (QString::fromUtf8(file.readAll()).contains(QStringLiteral("--ui=v1"))) {
            entryFlagMentions.append(QDir(sourceRoot()).relativeFilePath(path));
        }
    }
    if (!entryFlagMentions.isEmpty()) {
        out << "  --ui=v1 still mentioned in: " << entryFlagMentions.join(QStringLiteral(", ")) << '\n';
        out.flush();
    }
    expect(entryFlagMentions.isEmpty(),
           QStringLiteral("no source file still documents the removed --ui=v1 entry"), out, &failed);

    // 已删除的类名同样不能只在 CMakeLists 里查。一处写在源码或 src/README.md 里的
    // 引用编译期不可见、也不含 MIACODE_* 环境变量，会静默活过整个阶段——`--ui=v1`
    // 那条扫描当初就是为同样的理由加的。
    QStringList deletedTypeMentions;
    QDirIterator typeIt(sourceRoot() + QStringLiteral("/src"),
                        {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.mm"),
                         QStringLiteral("*.qml"), QStringLiteral("*.md")},
                        QDir::Files, QDirIterator::Subdirectories);
    while (typeIt.hasNext()) {
        const QString path = typeIt.next();
        // This spec names the deleted types on purpose, so its own source is not a leftover.
        if (typeIt.fileName() == QStringLiteral("V1ShellRemovalSpec.cpp")) continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(file.readAll());
        for (const QString& type : {QStringLiteral("QuickShellBootstrap"),
                                    QStringLiteral("QuickShellNativeSurfaceHost"),
                                    QStringLiteral("QuickShellStyleBridge"),
                                    QStringLiteral("QuickShellMacSurfaceSupport")}) {
            if (text.contains(type)) {
                deletedTypeMentions.append(
                    QDir(sourceRoot()).relativeFilePath(path) + QStringLiteral(": ") + type);
            }
        }
    }
    if (!deletedTypeMentions.isEmpty()) {
        out << "  deleted types still named in: "
            << deletedTypeMentions.join(QStringLiteral(", ")) << '\n';
        out.flush();
    }
    expect(deletedTypeMentions.isEmpty(),
           QStringLiteral("no source file still names a deleted v1 shell type"), out, &failed);

    // 表面再宿主分支随 v1 一起消失，否则阶段 2 会继承 29 处死分支。
    const QString controller = readSource(QStringLiteral("src/app/quick_shell/QuickShellController.cpp"));
    const QString controllerHeader = readSource(QStringLiteral("src/app/quick_shell/QuickShellController.h"));
    expect(!controller.isEmpty() && !controllerHeader.isEmpty(),
           QStringLiteral("QuickShellController is still present for v2"), out, &failed);
    QStringList surfaceHostLeftovers;
    for (const QString& token : {QStringLiteral("surfaceHost_"), QStringLiteral("QuickShellNativeSurfaceHost")}) {
        if (controller.contains(token))
            surfaceHostLeftovers.append(QStringLiteral("QuickShellController.cpp: ") + token);
        if (controllerHeader.contains(token))
            surfaceHostLeftovers.append(QStringLiteral("QuickShellController.h: ") + token);
    }
    if (!surfaceHostLeftovers.isEmpty()) {
        out << "  still branches on: " << surfaceHostLeftovers.join(QStringLiteral(", ")) << '\n';
    }
    expect(surfaceHostLeftovers.isEmpty(),
           QStringLiteral("QuickShellController no longer branches on a native surface host"),
           out, &failed);

    // 构建系统不得再引用已删除的源文件。
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));
    QStringList cmakeLeftovers;
    for (const QString& token : {QStringLiteral("QuickShellBootstrap"),
                                 QStringLiteral("QuickShellNativeSurfaceHost"),
                                 QStringLiteral("QuickShellStyleBridge"),
                                 QStringLiteral("QuickShellMacSurfaceSupport"),
                                 QStringLiteral("quick_shell_qml.qrc")}) {
        if (cmake.contains(token)) cmakeLeftovers.append(token);
    }
    if (!cmakeLeftovers.isEmpty()) {
        out << "  CMakeLists.txt still lists: " << cmakeLeftovers.join(QStringLiteral(", ")) << '\n';
    }
    expect(cmakeLeftovers.isEmpty(),
           QStringLiteral("the build no longer references removed v1 shell sources"), out, &failed);

    if (failed != 0) {
        out << "V1ShellRemoval spec failed: " << failed << '\n';
        return 1;
    }
    out << "V1ShellRemoval spec passed.\n";
    return 0;
}
