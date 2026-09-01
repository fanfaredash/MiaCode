// Stage 4 guard: media-tool behavior belongs to the non-Widget service, while
// QML continues to reach it through the application-services engine slot.

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QRegularExpression>
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

QString readSource(const QString& relativePath, bool* readable = nullptr)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (readable != nullptr) {
            *readable = false;
        }
        return QString();
    }
    if (readable != nullptr) {
        *readable = true;
    }
    return QString::fromUtf8(file.readAll());
}

// Keep newlines and identifiers, but replace comments and literals with
// spaces. This is intentionally a small lexer rather than a regex: contract
// words in comments, strings, and character literals must not count as code.
QString lexicallyVisible(QString source, bool* complete = nullptr)
{
    enum class State {
        Code, LineComment, BlockComment, String, Character, RawString, BracketComment
    };
    State state = State::Code;
    bool escaped = false;
    QString rawTerminator;
    for (int i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();
        if (state == State::Code) {
            if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
                source[i] = QLatin1Char(' ');
                source[++i] = QLatin1Char(' ');
                state = State::LineComment;
            } else if (ch == QLatin1Char('#')) {
                // Also support CMakeLists.txt comments, including #[[...]] and
                // #[=[...]=], whose delimiters are not C++ comments.
                const int equalsStart = i + 2;
                int bracket = equalsStart;
                while (bracket < source.size() && source.at(bracket) == QLatin1Char('=')) {
                    ++bracket;
                }
                if (bracket < source.size() && source.at(bracket) == QLatin1Char('[')) {
                    rawTerminator = QLatin1Char(']')
                        + QString(bracket - equalsStart, QLatin1Char('=')) + QLatin1Char(']');
                    for (int j = i; j <= bracket; ++j) {
                        source[j] = QLatin1Char(' ');
                    }
                    i = bracket;
                    state = State::BracketComment;
                } else {
                    source[i] = QLatin1Char(' ');
                    state = State::LineComment;
                }
            } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
                source[i] = QLatin1Char(' ');
                source[++i] = QLatin1Char(' ');
                state = State::BlockComment;
            } else if (ch == QLatin1Char('"')) {
                source[i] = QLatin1Char(' ');
                state = State::String;
                escaped = false;
            } else {
                const QStringList rawPrefixes = {
                    QStringLiteral("u8R\""), QStringLiteral("uR\""),
                    QStringLiteral("UR\""), QStringLiteral("LR\""), QStringLiteral("R\"")};
                QString rawPrefix;
                for (const QString& candidate : rawPrefixes) {
                    if (source.mid(i, candidate.size()) == candidate
                        && (i == 0 || !source.at(i - 1).isLetterOrNumber())) {
                        rawPrefix = candidate;
                        break;
                    }
                }
                const int openParen = rawPrefix.isEmpty()
                    ? -1 : source.indexOf(QLatin1Char('('), i + rawPrefix.size());
                if (openParen >= i + rawPrefix.size() && openParen - i <= 24) {
                    const QString delimiter = source.mid(
                        i + rawPrefix.size(), openParen - i - rawPrefix.size());
                    rawTerminator = QLatin1Char(')') + delimiter + QLatin1Char('"');
                    for (int j = i; j <= openParen; ++j) {
                        source[j] = QLatin1Char(' ');
                    }
                    i = openParen;
                    state = State::RawString;
                }
            }
            if (state == State::Code && ch == QLatin1Char('\'')) {
                source[i] = QLatin1Char(' ');
                state = State::Character;
                escaped = false;
            }
        } else if (state == State::LineComment) {
            if (ch == QLatin1Char('\n')) {
                state = State::Code;
            } else {
                source[i] = QLatin1Char(' ');
            }
        } else if (state == State::BlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                source[i] = QLatin1Char(' ');
                source[++i] = QLatin1Char(' ');
                state = State::Code;
            } else if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
                source[i] = QLatin1Char(' ');
            }
        } else if (state == State::RawString || state == State::BracketComment) {
            if (source.mid(i, rawTerminator.size()) == rawTerminator) {
                for (int j = 0; j < rawTerminator.size(); ++j) {
                    source[i + j] = QLatin1Char(' ');
                }
                i += rawTerminator.size() - 1;
                rawTerminator.clear();
                state = State::Code;
            } else if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
                source[i] = QLatin1Char(' ');
            }
        } else {
            if (escaped) {
                if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
                    source[i] = QLatin1Char(' ');
                }
                escaped = false;
            } else if (ch == QLatin1Char('\\')) {
                source[i] = QLatin1Char(' ');
                escaped = true;
            } else if ((state == State::String && ch == QLatin1Char('"'))
                       || (state == State::Character && ch == QLatin1Char('\''))) {
                source[i] = QLatin1Char(' ');
                state = State::Code;
            } else if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
                source[i] = QLatin1Char(' ');
            }
        }
    }
    if (complete != nullptr) {
        *complete = state == State::Code;
    }
    return source;
}

QString commandBlock(const QString& source, const QString& command)
{
    const int commandStart = source.indexOf(command);
    const int open = commandStart < 0 ? -1 : source.indexOf(QLatin1Char('('), commandStart);
    if (open < 0) {
        return QString();
    }
    int depth = 0;
    for (int i = open; i < source.size(); ++i) {
        if (source.at(i) == QLatin1Char('(')) {
            ++depth;
        } else if (source.at(i) == QLatin1Char(')') && --depth == 0) {
            return source.mid(commandStart, i - commandStart + 1);
        }
    }
    return QString();
}

QString commandBlockContaining(const QString& source, const QString& command,
                               const QStringList& requiredTokens)
{
    int searchFrom = 0;
    while (true) {
        const int commandStart = source.indexOf(command, searchFrom);
        if (commandStart < 0) {
            return QString();
        }
        const QString block = commandBlock(source.mid(commandStart), command);
        bool containsAll = !block.isEmpty();
        for (const QString& token : requiredTokens) {
            containsAll &= block.contains(token);
        }
        if (containsAll) {
            return block;
        }
        searchFrom = commandStart + command.size();
    }
}

bool hasCallWithArgument(const QString& source, const QString& receiver,
                         const QString& method, const QString& argument)
{
    const QString pattern = QStringLiteral(R"(%1\s*\.\s*%2\s*\(\s*%3\s*\))")
        .arg(QRegularExpression::escape(receiver), QRegularExpression::escape(method),
             QRegularExpression::escape(argument));
    return QRegularExpression(pattern).match(source).hasMatch();
}

QString functionBlock(const QString& source, const QString& signature)
{
    const int signatureStart = source.indexOf(signature);
    const int open = signatureStart < 0
        ? -1 : source.indexOf(QLatin1Char('{'), signatureStart);
    if (open < 0) {
        return QString();
    }
    int depth = 0;
    for (int i = open; i < source.size(); ++i) {
        if (source.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (source.at(i) == QLatin1Char('}') && --depth == 0) {
            return source.mid(signatureStart, i - signatureStart + 1);
        }
    }
    return QString();
}

bool hasCompleteLexicalScan(const QString& source, const QString& path)
{
    bool complete = true;
    lexicallyVisible(source, &complete);
    return require(complete, QStringLiteral("unterminated comment or literal in: ") + path);
}

bool hasCmakeSourceToken(const QString& source, const QString& token)
{
    const QString pattern = QStringLiteral("(?:^|\\s)")
        + QRegularExpression::escape(token) + QStringLiteral("(?=\\s|$)");
    return QRegularExpression(pattern).match(source).hasMatch();
}

bool hasMethodDeclaration(const QString& source, const QString& returnType,
                          const QString& method, const QString& arguments)
{
    const QString pattern = QStringLiteral(
        R"((?m)^\s*(?:virtual\s+)?%1\s+%2\s*\(\s*%3\s*\)\s*(?:override\s*)?;)" )
        .arg(QRegularExpression::escape(returnType), QRegularExpression::escape(method),
             arguments);
    return lexicallyVisible(source).contains(QRegularExpression(pattern));
}

bool hasMethodDefinition(const QString& source, const QString& returnType,
                         const QString& method, const QString& arguments)
{
    const QString pattern = QStringLiteral(
        R"((?m)^\s*%1\s+MediaToolsService\s*::\s*%2\s*\(\s*%3\s*\)\s*(?:const\s*)?\{)" )
        .arg(QRegularExpression::escape(returnType), QRegularExpression::escape(method), arguments);
    return lexicallyVisible(source).contains(QRegularExpression(pattern));
}

bool hasQualifiedMethod(const QString& source, const QString& qualifier, const QString& method)
{
    const QString pattern = QStringLiteral(
        R"((?m)^\s*(?:[A-Za-z_][\w:<>*&\s]*\s+)?)")
        + QRegularExpression::escape(qualifier) + QStringLiteral(R"(\s*::\s*)")
        + QRegularExpression::escape(method) + QStringLiteral(R"(\s*\()" );
    return lexicallyVisible(source).contains(QRegularExpression(pattern));
}

}  // namespace

int main()
{
    bool cmakeReadable = false;
    bool serviceHeaderReadable = false;
    bool serviceSourceReadable = false;
    bool mainWindowHeaderReadable = false;
    bool bootstrapHeaderReadable = false;
    bool bootstrapSourceReadable = false;
    bool frameBootstrapReadable = false;
    bool windowRuntimeReadable = false;
    bool applicationContextReadable = false;
    bool mediaModelHeaderReadable = false;
    bool mediaModelSourceReadable = false;
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"), &cmakeReadable);
    const QString cmakeCode = lexicallyVisible(cmake);
    const QString mainTargetSources = commandBlock(
        cmakeCode, QStringLiteral("add_executable(MiaCode"));
    const QString serviceHeader = readSource(QStringLiteral("src/app/v2/MediaToolsService.h"),
                                             &serviceHeaderReadable);
    const QString serviceSource = readSource(QStringLiteral("src/app/v2/MediaToolsService.cpp"),
                                             &serviceSourceReadable);
    const QString mainWindowHeader = readSource(QStringLiteral("src/app/mainwindow/MainWindow.h"),
                                                &mainWindowHeaderReadable);
    const QString bootstrapHeader = readSource(QStringLiteral("src/app/qml_ui/QmlUiBootstrap.h"),
                                               &bootstrapHeaderReadable);
    const QString bootstrapSource = readSource(QStringLiteral("src/app/qml_ui/QmlUiBootstrap.cpp"),
                                               &bootstrapSourceReadable);
    const QString frameBootstrap = readSource(
        QStringLiteral("src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp"),
        &frameBootstrapReadable);
    const QString windowRuntime = readSource(
        QStringLiteral("src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp"),
        &windowRuntimeReadable);
    const QString applicationContext = readSource(
        QStringLiteral("src/app/qml_ui/QmlApplicationContext.cpp"), &applicationContextReadable);
    const QString mediaModelHeader = readSource(
        QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.h"), &mediaModelHeaderReadable);
    const QString mediaModelSource = readSource(
        QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.cpp"), &mediaModelSourceReadable);

    const QStringList mediaMethods = {
        QStringLiteral("convertTrackTo44100Hz"), QStringLiteral("compressBackgroundVideo"),
        QStringLiteral("mediaBlankContext"), QStringLiteral("detectMediaBlankTiming"),
        QStringLiteral("restoreMediaBlankBackup"), QStringLiteral("applyMediaBlank"),
    };
    const QStringList mediaReturns = {
        QStringLiteral("void"), QStringLiteral("void"), QStringLiteral("QVariantMap"),
        QStringLiteral("QVariantMap"), QStringLiteral("void"), QStringLiteral("void"),
    };
    const QStringList mediaArguments = {
        QString(), QString(), QStringLiteral("bool isTrack"), QStringLiteral("bool isTrack"),
        QStringLiteral("bool isTrack"), QStringLiteral("bool isTrack, double beats, double bpm"),
    };

    bool ok = true;
    const auto requireReadable = [&ok](bool readable, const QString& source,
                                       const QString& path) {
        ok &= require(readable && !source.isEmpty(),
                      QStringLiteral("required source is readable and non-empty: ") + path);
    };
    requireReadable(cmakeReadable, cmake, QStringLiteral("CMakeLists.txt"));
    requireReadable(serviceHeaderReadable, serviceHeader,
                    QStringLiteral("src/app/v2/MediaToolsService.h"));
    requireReadable(serviceSourceReadable, serviceSource,
                    QStringLiteral("src/app/v2/MediaToolsService.cpp"));
    requireReadable(mainWindowHeaderReadable, mainWindowHeader,
                    QStringLiteral("src/app/mainwindow/MainWindow.h"));
    requireReadable(bootstrapHeaderReadable, bootstrapHeader,
                    QStringLiteral("src/app/qml_ui/QmlUiBootstrap.h"));
    requireReadable(bootstrapSourceReadable, bootstrapSource,
                    QStringLiteral("src/app/qml_ui/QmlUiBootstrap.cpp"));
    requireReadable(frameBootstrapReadable, frameBootstrap,
                    QStringLiteral("src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp"));
    requireReadable(windowRuntimeReadable, windowRuntime,
                    QStringLiteral("src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp"));
    requireReadable(applicationContextReadable, applicationContext,
                    QStringLiteral("src/app/qml_ui/QmlApplicationContext.cpp"));
    requireReadable(mediaModelHeaderReadable, mediaModelHeader,
                    QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.h"));
    requireReadable(mediaModelSourceReadable, mediaModelSource,
                    QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.cpp"));
    bool cmakeLexicallyComplete = true;
    lexicallyVisible(cmake, &cmakeLexicallyComplete);
    ok &= require(cmakeLexicallyComplete,
                  QStringLiteral("CMakeLists.txt has no unterminated comment or literal"));
    ok &= hasCompleteLexicalScan(serviceHeader, QStringLiteral("src/app/v2/MediaToolsService.h"));
    ok &= hasCompleteLexicalScan(serviceSource, QStringLiteral("src/app/v2/MediaToolsService.cpp"));
    ok &= hasCompleteLexicalScan(mainWindowHeader, QStringLiteral("src/app/mainwindow/MainWindow.h"));
    ok &= hasCompleteLexicalScan(bootstrapHeader, QStringLiteral("src/app/qml_ui/QmlUiBootstrap.h"));
    ok &= hasCompleteLexicalScan(bootstrapSource, QStringLiteral("src/app/qml_ui/QmlUiBootstrap.cpp"));
    ok &= hasCompleteLexicalScan(
        frameBootstrap, QStringLiteral("src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp"));
    ok &= hasCompleteLexicalScan(
        windowRuntime, QStringLiteral("src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp"));
    ok &= hasCompleteLexicalScan(applicationContext, QStringLiteral("src/app/qml_ui/QmlApplicationContext.cpp"));
    ok &= hasCompleteLexicalScan(mediaModelHeader, QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.h"));
    ok &= hasCompleteLexicalScan(mediaModelSource, QStringLiteral("src/app/qml_ui/media/QmlMediaToolsModel.cpp"));
    ok &= require(!cmake.isEmpty(), QStringLiteral("CMakeLists.txt is readable"));
    ok &= require(QFileInfo::exists(QStringLiteral(MIACODE_SOURCE_ROOT)
                                    + QStringLiteral("/src/app/v2/MediaToolsService.h")),
                  QStringLiteral("src/app/v2/MediaToolsService.h exists"));
    ok &= require(QFileInfo::exists(QStringLiteral(MIACODE_SOURCE_ROOT)
                                    + QStringLiteral("/src/app/v2/MediaToolsService.cpp")),
                  QStringLiteral("src/app/v2/MediaToolsService.cpp exists"));
    ok &= require(!mainTargetSources.isEmpty(),
                  QStringLiteral("main CMake add_executable(MiaCode) source list is readable"));
    ok &= require(hasCmakeSourceToken(mainTargetSources, QStringLiteral("src/app/v2/MediaToolsService.h"))
                      && hasCmakeSourceToken(mainTargetSources,
                                             QStringLiteral("src/app/v2/MediaToolsService.cpp")),
                  QStringLiteral("MiaCode main target owns MediaToolsService sources"));
    const QString legacySourceRelativePath =
        QStringLiteral("src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp");
    const bool legacyInCmake = hasCmakeSourceToken(mainTargetSources, legacySourceRelativePath);

    const QString serviceHeaderCode = lexicallyVisible(serviceHeader);
    const QString serviceSourceCode = lexicallyVisible(serviceSource);
    const int serviceClassStart = serviceHeaderCode.indexOf(QStringLiteral("class MediaToolsService"));
    const int serviceClassOpen = serviceClassStart < 0
        ? -1 : serviceHeaderCode.indexOf(QLatin1Char('{'), serviceClassStart);
    int serviceClassClose = -1;
    if (serviceClassOpen >= 0) {
        int depth = 0;
        for (int i = serviceClassOpen; i < serviceHeaderCode.size(); ++i) {
            if (serviceHeaderCode.at(i) == QLatin1Char('{')) {
                ++depth;
            } else if (serviceHeaderCode.at(i) == QLatin1Char('}') && --depth == 0) {
                serviceClassClose = i;
                break;
            }
        }
    }
    const QString serviceClassCode = serviceClassStart >= 0 && serviceClassClose > serviceClassStart
        ? serviceHeaderCode.mid(serviceClassStart, serviceClassClose - serviceClassStart + 1)
        : QString();
    ok &= require(!serviceClassCode.isEmpty(),
                  QStringLiteral("MediaToolsService is declared"));
    ok &= require(serviceClassOpen > serviceClassStart
                      && serviceClassCode.left(serviceClassOpen - serviceClassStart)
                             .contains(QStringLiteral("public MediaToolsEngine")),
                  QStringLiteral("MediaToolsService implements MediaToolsEngine"));
    ok &= require(hasMethodDeclaration(serviceClassCode, QStringLiteral("void"),
                                       QStringLiteral("invalidateCallbacks"), QString()),
                  QStringLiteral("MediaToolsService declares callback invalidation"));
    ok &= require(QRegularExpression(QStringLiteral(
                               R"(void\s+MediaToolsService\s*::\s*invalidateCallbacks\s*\()"))
                           .match(serviceSourceCode).hasMatch(),
                  QStringLiteral("MediaToolsService defines callback invalidation"));
    ok &= require(serviceClassCode.contains(QStringLiteral("bool hasActiveMediaOperation() const")),
                  QStringLiteral("MediaToolsService exposes a const activity query"));
    const QString mediaBeginCode = functionBlock(
        serviceSourceCode, QStringLiteral("PreviewSurface* MediaToolsService::beginMediaFileOperation"));
    const QString mediaEndCode = functionBlock(
        serviceSourceCode, QStringLiteral("bool MediaToolsService::endMediaFileOperation"));
    const int activeSet = mediaBeginCode.indexOf(QStringLiteral("activeMediaOperation_ = true"));
    const int slotRead = mediaBeginCode.indexOf(QStringLiteral("previewSurfaceSlot_"));
    const int beginCall = mediaBeginCode.indexOf(QStringLiteral("beginMediaFileOperation()"));
    ok &= require(!mediaBeginCode.isEmpty() && activeSet >= 0
                      && slotRead > activeSet && beginCall > activeSet,
                  QStringLiteral("MediaToolsService marks activity before touching PreviewSurface"));
    ok &= require(mediaBeginCode.count(QStringLiteral("activeMediaOperation_ = false")) >= 1,
                  QStringLiteral("MediaToolsService clears activity on begin failure"));
    const int firstEndClear = mediaEndCode.indexOf(QStringLiteral("activeMediaOperation_ = false"));
    ok &= require(!mediaEndCode.isEmpty()
                      && mediaEndCode.count(QStringLiteral("activeMediaOperation_ = false")) >= 2
                      && firstEndClear > mediaEndCode.indexOf(QStringLiteral("liveSurface")),
                  QStringLiteral("MediaToolsService clears activity on every end path"));
    ok &= require(mediaEndCode.contains(QStringLiteral("liveSurface->endMediaFileOperation(reloadTrack)"))
                      && mediaEndCode.lastIndexOf(QStringLiteral("activeMediaOperation_ = false"))
                             > mediaEndCode.indexOf(QStringLiteral("liveSurface->endMediaFileOperation")),
                  QStringLiteral("MediaToolsService clears activity only after PreviewSurface end returns"));
    for (int i = 0; i < mediaMethods.size(); ++i) {
        ok &= require(hasMethodDeclaration(serviceClassCode, mediaReturns.at(i), mediaMethods.at(i),
                                           mediaArguments.at(i)),
                      QStringLiteral("MediaToolsService declares exact interface: ") + mediaMethods.at(i));
        ok &= require(hasMethodDefinition(serviceSource, mediaReturns.at(i), mediaMethods.at(i),
                                           mediaArguments.at(i)),
                      QStringLiteral("MediaToolsService defines exact interface: ") + mediaMethods.at(i));
    }
    for (const QString& widgetToken : {QStringLiteral("QtWidgets"), QStringLiteral("QWidget")}) {
        ok &= require(!serviceHeaderCode.contains(widgetToken) && !serviceSourceCode.contains(widgetToken),
                      QStringLiteral("MediaToolsService has no Qt Widgets dependency: ") + widgetToken);
    }

    const QString legacyPath = QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/')
        + legacySourceRelativePath;
    const bool legacyExists = QFileInfo::exists(legacyPath);
    const QString legacySource = readSource(
        legacySourceRelativePath);
    bool legacySourceHasMediaInterface = false;
    for (const QString& method : mediaMethods) {
        legacySourceHasMediaInterface = legacySourceHasMediaInterface
            || hasQualifiedMethod(legacySource, QStringLiteral("MainWindow"), method)
            || hasQualifiedMethod(legacySource, QStringLiteral("MainWindow::DialogsSection"), method);
    }
    bool mainWindowHeaderHasMediaInterface = QRegularExpression(QStringLiteral("\\bMediaToolsEngine\\b"))
                                                  .match(mainWindowHeader)
                                                  .hasMatch();
    for (int i = 0; i < mediaMethods.size(); ++i) {
        mainWindowHeaderHasMediaInterface = mainWindowHeaderHasMediaInterface
            || hasMethodDeclaration(mainWindowHeader, mediaReturns.at(i), mediaMethods.at(i),
                                    mediaArguments.at(i));
    }

    const QString mainWindowRoot = QStringLiteral(MIACODE_SOURCE_ROOT) + QStringLiteral("/src/app/mainwindow");
    QDirIterator productionFiles(mainWindowRoot,
        QStringList{QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cpp"),
                    QStringLiteral("*.cc"), QStringLiteral("*.cxx"), QStringLiteral("*.hh"),
                    QStringLiteral("*.hxx"), QStringLiteral("*.ipp"), QStringLiteral("*.inc")},
        QDir::Files, QDirIterator::Subdirectories);
    ok &= require(QFileInfo::exists(mainWindowRoot) && QFileInfo(mainWindowRoot).isDir(),
                  QStringLiteral("MainWindow production source directory exists"));
    int scannedProductionFiles = 0;
    bool productionTreeHasMediaImplementation = false;
    bool productionTreeHasMediaEngine = false;
    while (productionFiles.hasNext()) {
        const QString path = productionFiles.next();
        ++scannedProductionFiles;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ok &= require(false, QStringLiteral("could not read MainWindow production source: ") + path);
            continue;
        }
        const QString productionSource = QString::fromUtf8(file.readAll());
        bool productionLexicallyComplete = true;
        const QString productionCode = lexicallyVisible(productionSource, &productionLexicallyComplete);
        ok &= require(productionLexicallyComplete,
                      QStringLiteral("unterminated comment or literal in: ") + path);
        for (const QString& method : mediaMethods) {
            productionTreeHasMediaImplementation = productionTreeHasMediaImplementation
                || hasQualifiedMethod(productionCode, QStringLiteral("MainWindow"), method)
                || hasQualifiedMethod(productionCode, QStringLiteral("MainWindow::DialogsSection"), method);
        }
        productionTreeHasMediaEngine = productionTreeHasMediaEngine
            || QRegularExpression(QStringLiteral("\\bMediaToolsEngine\\b"))
                   .match(productionCode)
                   .hasMatch();
    }
    ok &= require(scannedProductionFiles > 0,
                  QStringLiteral("MainWindow production tree has scannable source files"));

    const bool task6Complete = !legacyInCmake && !legacyExists
        && !mainWindowHeaderHasMediaInterface
        && !productionTreeHasMediaImplementation
        && !productionTreeHasMediaEngine;
    if (task6Complete) {
        ok &= require(!legacyExists,
                      QStringLiteral("legacy MainWindow.Dialogs.MediaTools.cpp is removed"));
        for (const QString& method : mediaMethods) {
            ok &= require(!legacySourceHasMediaInterface,
                          QStringLiteral("legacy media source has no residual interface: ") + method);
        }
        ok &= require(!mainWindowHeaderHasMediaInterface,
                      QStringLiteral("MainWindow no longer declares MediaToolsEngine/media interface"));
        ok &= require(!productionTreeHasMediaImplementation,
                      QStringLiteral("MainWindow production tree has no media implementation"));
        ok &= require(!productionTreeHasMediaEngine,
                      QStringLiteral("MainWindow production tree has no MediaToolsEngine"));
    } else {
        qInfo().noquote() << QStringLiteral(
            "Stage4 ownership guard: Task 6 removal checks pending; Task 5 lifecycle checks remain enforced.");
    }

    const QString bootstrapHeaderCode = lexicallyVisible(bootstrapHeader);
    const QString bootstrapCode = lexicallyVisible(bootstrapSource);
    const QString frameBootstrapCode = lexicallyVisible(frameBootstrap);
    const QString windowRuntimeCode = lexicallyVisible(windowRuntime);
    const QString startCode = functionBlock(
        bootstrapCode, QStringLiteral("bool QmlUiBootstrap::start"));
    const QString rootCloseConnection = commandBlockContaining(
        bootstrapCode,
        QStringLiteral("QObject::connect"),
        {QStringLiteral("rootCloseAccepted"),
         QStringLiteral("beginAcceptedRootWindowShutdown")});
    ok &= require(!rootCloseConnection.isEmpty(),
                  QStringLiteral("rootCloseAccepted connects to beginAcceptedRootWindowShutdown"));
    const QString acceptedBeginCode = functionBlock(
        bootstrapCode, QStringLiteral("void QmlUiBootstrap::beginAcceptedRootWindowShutdown"));
    ok &= require(acceptedBeginCode.contains(QStringLiteral("hasActiveMediaOperation()"))
                      && acceptedBeginCode.contains(QStringLiteral("invalidateCallbacks()"))
                      && acceptedBeginCode.contains(
                          QStringLiteral("scheduleAcceptedRootWindowShutdownRetry")),
                  QStringLiteral("accepted-close defers while MediaToolsService is active"));
    ok &= require(acceptedBeginCode.contains(QStringLiteral("QTimer::singleShot"))
                      && acceptedBeginCode.contains(
                          QStringLiteral("destroyAcceptedRootWindowResourcesAndQuit")),
                  QStringLiteral("accepted-close begin schedules the accepted destroy path"));
    const int applicationServicesMember = bootstrapHeaderCode.indexOf(
        QStringLiteral("applicationServices_"));
    const int mediaToolsServiceMember = bootstrapHeaderCode.indexOf(
        QStringLiteral("mediaToolsService_"));
    const int backendMember = bootstrapHeaderCode.indexOf(QStringLiteral("backend_"));
    ok &= require(applicationServicesMember >= 0 && mediaToolsServiceMember > applicationServicesMember
                      && backendMember > mediaToolsServiceMember,
                  QStringLiteral("Bootstrap member order keeps media service between assembly and backend"));
    ok &= require(QRegularExpression(QStringLiteral(
                               R"(std::unique_ptr\s*<\s*(?:miacode::v2::)?MediaToolsService\s*>\s+mediaToolsService_\s*;)"))
                           .match(bootstrapHeaderCode).hasMatch(),
                  QStringLiteral("QmlUiBootstrap owns a unique_ptr<MediaToolsService>"));
    const int applicationServicesCreation = startCode.indexOf(
        QStringLiteral("applicationServices_ = std::make_unique<miacode::v2::ApplicationServices"));
    const int serviceCreation = startCode.indexOf(
        QStringLiteral("mediaToolsService_ = std::make_unique<miacode::v2::MediaToolsService"));
    const int mainWindowCreation = startCode.indexOf(QStringLiteral("backend_ = std::make_unique<MainWindow"));
    const int serviceInjection = startCode.indexOf(
        QStringLiteral("setMediaToolsEngine(mediaToolsService_.get())"));
    const QString serviceCreationCode = commandBlock(
        startCode, QStringLiteral("mediaToolsService_ = std::make_unique<miacode::v2::MediaToolsService"));
    const auto requireOrdered = [&ok](const QString& source, const QStringList& tokens,
                                       const QString& message) {
        int previous = -1;
        for (const QString& token : tokens) {
            const int current = source.indexOf(token);
            if (current < 0 || current <= previous) {
                ok &= require(false, message + QStringLiteral(": ") + token);
                return;
            }
            previous = current;
        }
        ok &= require(true, message);
    };
    requireOrdered(
        startCode,
        {QStringLiteral("applicationServices_ = std::make_unique<miacode::v2::ApplicationServices"),
         QStringLiteral("mediaToolsService_ = std::make_unique<miacode::v2::MediaToolsService"),
         QStringLiteral("applicationServices_->setMediaToolsEngine(mediaToolsService_.get())"),
         QStringLiteral("backend_ = std::make_unique<MainWindow")},
        QStringLiteral("Bootstrap constructs assembly, service, injects provider, then constructs backend"));
    requireOrdered(
        serviceCreationCode,
        {QStringLiteral("applicationServices_->workspace()"),
         QStringLiteral("applicationServices_->uiRequests()"),
         QStringLiteral("applicationServices_->jobProgress()"),
         QStringLiteral("applicationServices_->previewSurfaceSlot()")},
        QStringLiteral("MediaToolsService receives all four ApplicationServices dependencies"));
    requireOrdered(
        startCode,
        {QStringLiteral("backend_ = std::make_unique<MainWindow"),
         QStringLiteral("backend_->hide()"),
         QStringLiteral("backend_->setVisible(false)")},
        QStringLiteral("Bootstrap retains the hidden backend construction"));

    const QList<QPair<QString, QString>> nonMediaSlotBindings = {
        {QStringLiteral("setExportEngine"), QStringLiteral("exportSection_.get()")},
        {QStringLiteral("setEditorPageRouter"), QStringLiteral("this")},
        {QStringLiteral("setLatencyEngine"), QStringLiteral("this")},
        {QStringLiteral("setTimelineSurface"), QStringLiteral("this")},
        {QStringLiteral("setPreviewSurface"), QStringLiteral("this")},
        {QStringLiteral("setPreferencesStore"), QStringLiteral("this")},
        {QStringLiteral("setDocumentBridge"), QStringLiteral("this")},
    };
    for (const auto& binding : nonMediaSlotBindings) {
        ok &= require(hasCallWithArgument(frameBootstrapCode, QStringLiteral("applicationServices_"),
                                          binding.first, binding.second),
                      QStringLiteral("FrameBootstrap registers non-media slot: ") + binding.first);
    }
    const QStringList nonMediaSlotSetters = {
        QStringLiteral("setExportEngine"), QStringLiteral("setEditorPageRouter"),
        QStringLiteral("setLatencyEngine"), QStringLiteral("setTimelineSurface"),
        QStringLiteral("setPreviewSurface"), QStringLiteral("setPreferencesStore"),
        QStringLiteral("setDocumentBridge"),
    };
    for (const QString& setter : nonMediaSlotSetters) {
        ok &= require(hasCallWithArgument(windowRuntimeCode, QStringLiteral("applicationServices_"),
                                          setter, QStringLiteral("nullptr")),
                      QStringLiteral("WindowRuntime clears non-media slot: ") + setter);
    }

    const QString shutdownCode = functionBlock(
        bootstrapCode, QStringLiteral("void QmlUiBootstrap::shutdownOwnedResources"));
    const auto requireShutdownOrder = [&ok, &shutdownCode](const QStringList& tokens,
                                                            const QString& message) {
        int previous = -1;
        for (const QString& token : tokens) {
            const int current = shutdownCode.indexOf(token);
            if (current < 0 || current <= previous) {
                ok &= require(false, message + QStringLiteral(": ") + token);
                return;
            }
            previous = current;
        }
        ok &= require(true, message);
    };
    requireShutdownOrder(
        {QStringLiteral("mediaToolsService_->invalidateCallbacks()"),
         QStringLiteral("releaseRootWindowResources()"),
         QStringLiteral("engine_.reset()"),
         QStringLiteral("applicationContext_.reset()"),
         QStringLiteral("applicationServices_->setMediaToolsEngine(nullptr)"),
         QStringLiteral("mediaToolsService_.reset()"),
         QStringLiteral("backend_.reset()"),
         QStringLiteral("applicationServices_.reset()")},
        QStringLiteral("Bootstrap shutdown order is invalidate, QML release, slot clear, service, backend, assembly"));

    const QString destructorCode = functionBlock(
        bootstrapCode, QStringLiteral("QmlUiBootstrap::~QmlUiBootstrap"));
    const int activeDestructorStart = destructorCode.indexOf(
        QStringLiteral("if (mediaToolsService_ != nullptr && mediaToolsService_->hasActiveMediaOperation())"));
    const int activeDestructorEnd = destructorCode.indexOf(
        QStringLiteral("shutdownOwnedResources();"), activeDestructorStart);
    const QString activeDestructorCode = activeDestructorStart >= 0 && activeDestructorEnd > activeDestructorStart
        ? destructorCode.mid(activeDestructorStart, activeDestructorEnd - activeDestructorStart)
        : QString();
    const QString acceptedDestroyCode = functionBlock(
        bootstrapCode,
        QStringLiteral("void QmlUiBootstrap::destroyAcceptedRootWindowResourcesAndQuit"));
    ok &= require(acceptedDestroyCode.contains(QStringLiteral("hasActiveMediaOperation()"))
                      && acceptedDestroyCode.contains(QStringLiteral("scheduleAcceptedRootWindowDestroyRetry"))
                      && acceptedDestroyCode.contains(QStringLiteral("invalidateCallbacks()")),
                  QStringLiteral("accepted destroy defers while MediaToolsService is active"));
    ok &= require(acceptedDestroyCode.contains(QStringLiteral("shutdownOwnedResources()")),
                  QStringLiteral("accepted destroy uses the shared owned-resource shutdown"));
    ok &= require(shutdownCode.contains(QStringLiteral("hasActiveMediaOperation()"))
                      && shutdownCode.contains(QStringLiteral("scheduleOwnedResourceShutdownRetry"))
                      && shutdownCode.contains(QStringLiteral("invalidateCallbacks()")),
                  QStringLiteral("shared shutdown defers while MediaToolsService is active"));
    ok &= require(destructorCode.contains(QStringLiteral("hasActiveMediaOperation()"))
                      && activeDestructorCode.contains(QStringLiteral("chartDropBridge_->release()"))
                      && activeDestructorCode.indexOf(QStringLiteral("chartDropBridge_->release()"))
                          < activeDestructorCode.indexOf(QStringLiteral("chartDropBridge_->setParent(nullptr)"))
                      && activeDestructorCode.contains(QStringLiteral("mediaToolsService_.release()"))
                      && !activeDestructorCode.contains(QStringLiteral("mediaToolsService_.reset()"))
                      && !activeDestructorCode.contains(QStringLiteral("backend_.reset()"))
                      && !activeDestructorCode.contains(QStringLiteral("applicationServices_.reset()")),
                  QStringLiteral("Active Bootstrap destructor releases the drop bridge before detaching and preserves borrowed services"));
    const QStringList rootFailureConditions = {
        QStringLiteral("if (engine_->rootObjects().isEmpty())"),
        QStringLiteral("if (!rootLifecycle_.registerRoot())"),
        QStringLiteral("if (!rootLifecycle_.installRootEventFilter())"),
        QStringLiteral("if (!rootLifecycle_.installDropBridge())"),
        QStringLiteral("if (!rootLifecycle_.canShowRoot())"),
    };
    for (const QString& condition : rootFailureConditions) {
        const QString failureCode = functionBlock(bootstrapCode, condition);
        ok &= require(!failureCode.isEmpty()
                          && failureCode.contains(QStringLiteral("shutdownOwnedResources()"))
                          && failureCode.contains(QStringLiteral("return false")),
                      QStringLiteral("Bootstrap root failure reaches shared cleanup: ") + condition);
    }
    ok &= require(destructorCode.contains(QStringLiteral("shutdownOwnedResources()"))
                      && acceptedDestroyCode.contains(QStringLiteral("shutdownOwnedResources()"))
                      && !rootFailureConditions.isEmpty(),
                  QStringLiteral("Bootstrap failure, accepted-close, and destructor share shutdown"));
    const QString rootReleaseCode = functionBlock(
        bootstrapCode, QStringLiteral("void QmlUiBootstrap::releaseRootWindowResources"));
    const QString rootDestroyedCode = functionBlock(
        bootstrapCode, QStringLiteral("QObject::connect(window, &QObject::destroyed"));
    ok &= require(rootDestroyedCode.contains(QStringLiteral("releaseRootWindowResources()")),
                  QStringLiteral("Root destroyed callback releases root resources"));
    ok &= require(rootReleaseCode.contains(QStringLiteral("if (!rootLifecycle_.beginRelease())"))
                      && rootReleaseCode.contains(QStringLiteral("return;"))
                      && rootReleaseCode.contains(QStringLiteral("releaseChartDropImport()"))
                      && rootReleaseCode.contains(QStringLiteral("chartDropBridge_->release()"))
                      && rootReleaseCode.contains(QStringLiteral("chartDropBridge_.reset()"))
                      && rootReleaseCode.contains(QStringLiteral("rootWindow_ = nullptr")),
                  QStringLiteral("Root resource release has an idempotent beginRelease gate"));
    const QStringList forbiddenRootReleaseTokens = {
        QStringLiteral("engine_.reset"),
        QStringLiteral("applicationContext_.reset"),
        QStringLiteral("backend_.reset"),
        QStringLiteral("applicationServices_.reset"),
        QStringLiteral("mediaToolsService_.reset"),
        QStringLiteral("invalidateCallbacks"),
        QStringLiteral("setMediaToolsEngine"),
        QStringLiteral("previewSurfaceSlot"),
        QStringLiteral("mediaToolsEngineSlot"),
    };
    for (const QString& token : forbiddenRootReleaseTokens) {
        ok &= require(!rootReleaseCode.contains(token),
                      QStringLiteral("Root resource release excludes premature lifetime operation: ")
                          + token);
    }

    const QString mediaModelHeaderCode = lexicallyVisible(mediaModelHeader);
    const QString mediaModelCode = lexicallyVisible(mediaModelSource);
    ok &= require(mediaModelHeaderCode.contains(QStringLiteral("MediaToolsEngine*& engineSlot"))
                      && mediaModelHeaderCode.contains(QStringLiteral("MediaToolsEngine** engineSlot_")),
                  QStringLiteral("QmlMediaToolsModel accepts and stores the engine slot"));
    ok &= require(mediaModelCode.contains(QStringLiteral("QmlMediaToolsModel::QmlMediaToolsModel("))
                      && mediaModelCode.contains(QStringLiteral("MediaToolsEngine*& engineSlot")),
                  QStringLiteral("QmlMediaToolsModel constructor receives MediaToolsEngine*& engineSlot"));
    ok &= require(mediaModelCode.contains(QStringLiteral("engineSlot_(&engineSlot)")),
                  QStringLiteral("QmlMediaToolsModel binds engineSlot_ to the live slot"));
    ok &= require(mediaModelHeaderCode.contains(
                               QStringLiteral("return engineSlot_ != nullptr ? *engineSlot_")),
                  QStringLiteral("QmlMediaToolsModel::engine() dereferences the live slot"));
    for (const QString& method : mediaMethods) {
        ok &= require(QRegularExpression(QStringLiteral("engine\\(\\)->\\s*")
                                          + QRegularExpression::escape(method)
                                          + QStringLiteral("\\s*\\(")).match(mediaModelCode).hasMatch(),
                      QStringLiteral("QmlMediaToolsModel forwards through engine slot: ") + method);
    }
    ok &= require(QRegularExpression(QStringLiteral(
                               R"(mediaTools_\s*\(\s*services\.uiRequests\(\)\s*,\s*services\.jobProgress\(\)\s*,\s*services\.mediaToolsEngineSlot\(\))"))
                           .match(lexicallyVisible(applicationContext)).hasMatch(),
                  QStringLiteral("QmlApplicationContext injects mediaToolsEngineSlot()"));

    for (const QString& target : {QStringLiteral("stage4_widget_residue_spec"),
                                  QStringLiteral("stage4_media_tools_ownership_spec")}) {
        const QString targetBlock = commandBlock(
            cmakeCode, QStringLiteral("miacode_add_dev_tool(") + target);
        const QString expectedSpec = target == QLatin1String("stage4_widget_residue_spec")
            ? QStringLiteral("src/tools/ui/Stage4WidgetResidueSpec.cpp")
            : QStringLiteral("src/tools/ui/Stage4MediaToolsOwnershipSpec.cpp");
        ok &= require(targetBlock.contains(QStringLiteral(" TEST"))
                          && targetBlock.contains(QStringLiteral("SOURCES"))
                          && targetBlock.contains(expectedSpec),
                      QStringLiteral("CTest target has TEST and its own spec source: ") + target);
    }
    return ok ? 0 : 1;
}
