// Drift guard for the QTranslator localization pipeline.
//
// Four invariants:
//
//   1. The message id sets in translations/miacode_{en,zh_CN,ja_JP}.ts must
//      be identical. Any key added to one catalog must be added to all three.
//
//   2. Literal id lookups in src/ (qtTrId, qsTrId) and the
//      shortcuts.json label_key values must all exist in the en.ts id set.
//      This catches typos before they reach the UI.
//
//   3. QML uses qsTrId; qsTr() and UiText.text() are forbidden.
//
//   4. Selected keys must resolve to the correct English label when the
//      English catalog is installed, confirming the pipeline is live.
//
// The repo root is injected at configure time via MIACODE_SOURCE_ROOT.

#include "preferences/PreferenceDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTranslator>
#include <QXmlStreamReader>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

#ifndef MIACODE_EN_QM_PATH
#error "MIACODE_EN_QM_PATH must be defined (generated English catalog path)"
#endif

namespace {

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// Collect all message id attributes from a Qt .ts file.
QSet<QString> idsFromTsFile(const QString& path, QTextStream& err)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err << "ui_text_locale_spec: cannot open " << path << Qt::endl;
        return {};
    }

    QSet<QString> ids;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNextStartElement() && xml.name() == QLatin1String("message")) {
            const QString id = xml.attributes().value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                ids.insert(id);
            }
        }
    }
    if (xml.hasError()) {
        err << "ui_text_locale_spec: XML error in " << path
            << ": " << xml.errorString() << Qt::endl;
    }
    return ids;
}

// Unescape a C++ string-literal body into the runtime string.
QString unescapeLiteral(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('\\') && i + 1 < raw.size()) {
            const QChar next = raw.at(i + 1);
            switch (next.unicode()) {
            case 'n': out.append(QLatin1Char('\n')); ++i; continue;
            case 't': out.append(QLatin1Char('\t')); ++i; continue;
            case '"': out.append(QLatin1Char('"')); ++i; continue;
            case '\\': out.append(QLatin1Char('\\')); ++i; continue;
            default: break;
            }
        }
        out.append(c);
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    bool ok = true;

    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT);

    // Install the English catalog generated from the same TS input that the
    // application embeds, so invariant 4 also guards the release pipeline.
    QTranslator translator;
    const QString enQmPath = QStringLiteral(MIACODE_EN_QM_PATH);
    if (!translator.load(enQmPath)) {
        err << "ui_text_locale_spec: could not load " << enQmPath << Qt::endl;
        return 1;
    }
    if (!QCoreApplication::installTranslator(&translator)) {
        err << "ui_text_locale_spec: could not install English translator" << Qt::endl;
        return 1;
    }

    // --- Invariant 1: id sets in all three .ts files are identical. ----------
    const QString tsDir = root + QStringLiteral("/translations");
    const struct { const char* name; const char* file; } tsFiles[] = {
        {"en",    "miacode_en.ts"},
        {"zh_CN", "miacode_zh_CN.ts"},
        {"ja_JP", "miacode_ja_JP.ts"},
    };

    QHash<QByteArray, QSet<QString>> tsIds;
    for (const auto& ts : tsFiles) {
        const QString path = tsDir + QLatin1Char('/') + QString::fromLatin1(ts.file);
        const QSet<QString> ids = idsFromTsFile(path, err);
        if (ids.isEmpty()) {
            ok = false;
        }
        tsIds.insert(QByteArray(ts.name), ids);
    }

    const QSet<QString>& enIds = tsIds.value("en");

    for (const auto& ts : tsFiles) {
        const QByteArray name(ts.name);
        if (QByteArray(ts.name) == QByteArray("en")) {
            continue;
        }
        const QSet<QString>& other = tsIds.value(name);
        QSet<QString> onlyInEn = enIds - other;
        QSet<QString> onlyInOther = other - enIds;
        if (!onlyInEn.isEmpty() || !onlyInOther.isEmpty()) {
            ok = false;
            QStringList sorted;
            for (const QString& id : onlyInEn) {
                sorted.append(QStringLiteral("  only in en: ") + id);
            }
            for (const QString& id : onlyInOther) {
                sorted.append(QStringLiteral("  only in ") + QString::fromLatin1(ts.name) + QStringLiteral(": ") + id);
            }
            sorted.sort();
            err << "ui_text_locale_spec: id mismatch between en.ts and "
                << ts.name << ".ts (" << (onlyInEn.size() + onlyInOther.size())
                << " difference(s)):" << Qt::endl;
            for (const QString& line : sorted) {
                err << line << Qt::endl;
            }
        }
    }

    // --- Invariant 2: source literal ids must exist in the en.ts id set. ----
    const QString srcDir = root + QStringLiteral("/src");

    const struct {
        QRegularExpression re;
        int keyGroup;
    } keyPatterns[] = {
        {
            QRegularExpression(QStringLiteral(
                "UiText::text\\(\\s*(?:QStringLiteral\\(\\s*)?\"((?:[^\"\\\\]|\\\\.)*)\"")),
            1
        },
        {
            QRegularExpression(QStringLiteral(
                "(^|[^A-Za-z0-9_:])uiText\\(\\s*(?:QStringLiteral\\(\\s*)?\"((?:[^\"\\\\]|\\\\.)*)\"")),
            2
        },
        {
            QRegularExpression(QStringLiteral(
                "UiDialogs::text\\(\\s*(?:QStringLiteral\\(\\s*)?\"((?:[^\"\\\\]|\\\\.)*)\"")),
            1
        },
        {
            QRegularExpression(QStringLiteral(
                "(^|[^A-Za-z0-9_:])qtTrId\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")),
            2
        },
    };

    // Invariant 2 uses the parsed enIds set directly. The TS catalog is the
    // authoritative message-id source for both this guard and lrelease.
    auto inCatalog = [&enIds](const QString& key) -> bool {
        return enIds.contains(key);
    };

    QSet<QString> missingKeys;
    QSet<QString> literalKeys;
    int scanned = 0;
    QDirIterator it(
        srcDir,
        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.contains(QStringLiteral("/tools/"))) {
            continue;
        }
        const QString text = readFile(path);
        for (const auto& pattern : keyPatterns) {
            QRegularExpressionMatchIterator mi = pattern.re.globalMatch(text);
            while (mi.hasNext()) {
                const QRegularExpressionMatch m = mi.next();
                const QString key = unescapeLiteral(m.captured(pattern.keyGroup));
                literalKeys.insert(key);
                if (!inCatalog(key)) {
                    missingKeys.insert(key);
                }
            }
        }
        ++scanned;
    }

    // Also scan QML for qsTrId("...") literals.
    const QRegularExpression qmlTrIdRe(QStringLiteral(
        "qsTrId\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""
    ));
    QDirIterator qmlIt(root + QStringLiteral("/src/app/ui"),
                       {QStringLiteral("*.qml")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (qmlIt.hasNext()) {
        const QString path = qmlIt.next();
        const QString text = readFile(path);
        QRegularExpressionMatchIterator mi = qmlTrIdRe.globalMatch(text);
        while (mi.hasNext()) {
            const QString key = unescapeLiteral(mi.next().captured(1));
            literalKeys.insert(key);
            if (!inCatalog(key)) {
                missingKeys.insert(key);
            }
        }
    }

    const QString shortcutsPath = root + QStringLiteral("/resources/shortcuts.json");
    const QString shortcutsText = readFile(shortcutsPath);
    QStringList shortcutActionsMissingLabelKey;
    if (shortcutsText.isEmpty()) {
        ok = false;
        err << "ui_text_locale_spec: could not read " << shortcutsPath << Qt::endl;
    } else {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(shortcutsText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            ok = false;
            err << "ui_text_locale_spec: could not parse " << shortcutsPath
                << ": " << parseError.errorString() << Qt::endl;
        } else {
            const QJsonObject actions =
                doc.object().value(QStringLiteral("actions")).toObject();
            if (actions.isEmpty()) {
                ok = false;
                err << "ui_text_locale_spec: shortcuts.json has no actions object"
                    << Qt::endl;
            }
            for (auto actionIt = actions.constBegin(); actionIt != actions.constEnd(); ++actionIt) {
                const QJsonObject action = actionIt.value().toObject();
                const QString key = action.value(QStringLiteral("label_key")).toString();
                if (key.isEmpty()) {
                    shortcutActionsMissingLabelKey.append(actionIt.key());
                    continue;
                }
                literalKeys.insert(key);
                if (!inCatalog(key)) {
                    missingKeys.insert(key);
                }
            }
        }
    }

    if (scanned == 0) {
        err << "ui_text_locale_spec: scanned 0 source files under " << srcDir
            << " — is MIACODE_SOURCE_ROOT correct?" << Qt::endl;
        return 1;
    }

    if (!missingKeys.isEmpty()) {
        ok = false;
        QStringList sorted(missingKeys.begin(), missingKeys.end());
        sorted.sort();
        err << sorted.size()
            << " source/resource literal id(s) missing from the en.ts catalog:" << Qt::endl;
        for (const QString& key : sorted) {
            err << "  - " << key << Qt::endl;
        }
        err << "Fix: add each id to translations/miacode_{en,zh_CN,ja_JP}.ts, "
               "or correct the call-site typo." << Qt::endl;
    }

    if (!shortcutActionsMissingLabelKey.isEmpty()) {
        ok = false;
        shortcutActionsMissingLabelKey.sort();
        err << shortcutActionsMissingLabelKey.size()
            << " shortcut action(s) missing label_key in resources/shortcuts.json:" << Qt::endl;
        for (const QString& actionId : shortcutActionsMissingLabelKey) {
            err << "  - " << actionId << Qt::endl;
        }
        err << "Fix: add a label_key that exists in the en.ts catalog." << Qt::endl;
    }

    // --- Invariant 3: QML uses qsTrId; no UiText.text() calls; no qsTr(). --
    const QRegularExpression qmlUiTextCallRe(QStringLiteral(
        "UiText\\.text\\("));
    const QRegularExpression qmlQsTrRe(QStringLiteral(
        "(?<![A-Za-z0-9_])qsTr\\("));
    int qmlScanned = 0;
    QStringList qmlUiTextLeftovers;
    QStringList qmlTrLeftovers;
    QDirIterator qmlIt2(root + QStringLiteral("/src/app/ui"),
                        {QStringLiteral("*.qml")}, QDir::Files,
                        QDirIterator::Subdirectories);
    while (qmlIt2.hasNext()) {
        const QString path = qmlIt2.next();
        const QString relative = QDir(root).relativeFilePath(path);
        const QString text = readFile(path);
        ++qmlScanned;
        if (qmlUiTextCallRe.match(text).hasMatch()) {
            qmlUiTextLeftovers.append(relative);
        }
        if (qmlQsTrRe.match(text).hasMatch()) {
            qmlTrLeftovers.append(relative);
        }
    }
    if (qmlScanned == 0) {
        ok = false;
        err << "ui_text_locale_spec: scanned 0 QML files — "
               "is MIACODE_SOURCE_ROOT correct?" << Qt::endl;
    }
    if (!qmlUiTextLeftovers.isEmpty()) {
        ok = false;
        qmlUiTextLeftovers.sort();
        err << "ui_text_locale_spec: " << qmlUiTextLeftovers.size()
            << " QML file(s) still call UiText.text() — migrate to qsTrId(\"key\"):"
            << Qt::endl;
        for (const QString& f : qmlUiTextLeftovers) {
            err << "  " << f << Qt::endl;
        }
    }
    if (!qmlTrLeftovers.isEmpty()) {
        ok = false;
        qmlTrLeftovers.sort();
        err << "ui_text_locale_spec: " << qmlTrLeftovers.size()
            << " QML file(s) use qsTr() — use qsTrId(\"key\") instead:" << Qt::endl;
        for (const QString& f : qmlTrLeftovers) {
            err << "  " << f << Qt::endl;
        }
    }

    // --- Invariant 4: key sample resolves correctly with the en catalog. ----
    bool isBuiltInJapanese = false;
    for (const PreferenceDocument::LanguageOption& option : PreferenceDocument::availableLanguageOptions()) {
        if (option.id == QStringLiteral("ja") && option.builtIn) {
            isBuiltInJapanese = true;
            break;
        }
    }
    if (!PreferenceDocument::isLanguageAvailable(QStringLiteral("ja")) || !isBuiltInJapanese) {
        ok = false;
        err << "ui_text_locale_spec: built-in Japanese language option 'ja' is missing."
            << Qt::endl;
    }

    for (const QString& key : {
             QStringLiteral("dialog.skin_settings.chart_effect.standard"),
             QStringLiteral("dialog.render_settings.video.skin.standard"),
         }) {
        const QString resolved = qtTrId(key.toUtf8().constData());
        if (resolved != QStringLiteral("SD")) {
            ok = false;
            err << "ui_text_locale_spec: " << key << " must display \"SD\", got \""
                << resolved << "\"" << Qt::endl;
        }
    }

    // --- Invariant 5: no bare CJK UI literals in ui product sources. -----
    {
        const QRegularExpression bareCjkRe(QStringLiteral(
            "(?:QStringLiteral\\s*\\(\\s*)?\"((?:[^\"\\\\]|\\\\.)*[\\x{4e00}-\\x{9fff}](?:[^\"\\\\]|\\\\.)*)\""));
        const QSet<QString> allow = {
            QStringLiteral("[错误]"),
            QStringLiteral("[警告]"),
            QStringLiteral("汉"),
        };
        QStringList bareHits;
        QDirIterator uiIt(root + QStringLiteral("/src/app/ui"),
                          {QStringLiteral("*.qml"), QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                          QDir::Files, QDirIterator::Subdirectories);
        while (uiIt.hasNext()) {
            const QString path = uiIt.next();
            const QStringList lines = readFile(path).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                QString code = lines.at(i);
                const int comment = code.indexOf(QStringLiteral("//"));
                if (comment >= 0) {
                    code = code.left(comment);
                }
                if (code.trimmed().startsWith(QLatin1Char('*'))) {
                    continue;
                }
                if (code.contains(QStringLiteral("qsTrId("))
                    || code.contains(QStringLiteral("qtTrId("))) {
                    continue;
                }
                QRegularExpressionMatchIterator bi = bareCjkRe.globalMatch(code);
                while (bi.hasNext()) {
                    const QString lit = unescapeLiteral(bi.next().captured(1));
                    if (allow.contains(lit)) {
                        continue;
                    }
                    bareHits.append(QDir(root).relativeFilePath(path)
                        + QLatin1Char(':') + QString::number(i + 1)
                        + QStringLiteral(": ") + lit);
                }
            }
        }
        if (!bareHits.isEmpty()) {
            ok = false;
            bareHits.sort();
            err << "ui_text_locale_spec: bare CJK UI literal(s); use qsTrId/qtTrId:"
                << Qt::endl;
            for (const QString& entry : bareHits) {
                err << "    " << entry << Qt::endl;
            }
        }
    }

    if (!ok) {
        return 1;
    }

    out << "ui_text_locale_spec ok (" << literalKeys.size()
        << " literal id(s) checked across " << scanned
        << " source files + " << qmlScanned << " QML files + shortcuts.json)" << Qt::endl;
    return 0;
}
