// Drift guard for the UiText localization tables.
//
// Two invariants:
//
//   1. Built-in enMap, zhMap, and jaMap in UiText.cpp must have identical key sets.
//      English is now centralized instead of living only at call-site fallback
//      strings.
//
//   2. Literal key-based lookups in src/ and resources/ must reference keys
//      present in the tables. This catches typoed `UiText::text(<key>)`
//      lookups and shortcut registry `label_key` values.
//
// The repo root is injected at configure time via MIACODE_SOURCE_ROOT.

#include "UiText.h"

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

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
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

// Turn a raw C++ string-literal body (as it appears in source, with two-char
// escapes like backslash-n) into the runtime QString that QStringLiteral would
// produce, so it can be matched against table keys.
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

    // --- Invariant 1: built-in enMap / zhMap / jaMap key-set parity. -------
    const QStringList mismatches = UiText::translationKeyMismatches();
    if (!mismatches.isEmpty()) {
        ok = false;
        err << mismatches.size()
            << " enMap/zhMap/jaMap key-set mismatch(es) in UiText.cpp:" << Qt::endl;
        for (const QString& m : mismatches) {
            err << "  - " << m << Qt::endl;
        }
        err << "Fix: add the missing key to each built-in map. External language "
               "packs are still validated through extension manifest diagnostics." << Qt::endl;
    }

    const bool japaneseAvailable = UiText::isLanguageAvailable(QStringLiteral("ja"));
    bool hasBuiltInJapanese = false;
    for (const UiText::LanguageOption& option : UiText::availableLanguageOptions()) {
        if (option.id == QStringLiteral("ja") && option.builtIn) {
            hasBuiltInJapanese = true;
            break;
        }
    }
    if (!japaneseAvailable || !hasBuiltInJapanese) {
        ok = false;
        err << "ui_text_locale_spec: built-in Japanese language option 'ja' is missing."
            << Qt::endl;
    }

    // Skin variants use their familiar chart-type abbreviations in every UI
    // language. The persisted enum still uses "Standard" internally, so only
    // the localized display labels are asserted here.
    for (const QString& key : {
             QStringLiteral("dialog.skin_settings.chart_effect.standard"),
             QStringLiteral("dialog.render_settings.video.skin.standard"),
         }) {
        if (UiText::text(key) != QStringLiteral("SD")) {
            ok = false;
            err << "ui_text_locale_spec: " << key << " must display SD, got "
                << UiText::text(key) << Qt::endl;
        }
    }

    // --- Invariant 2: source literal keys exist in the tables. --------------
    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT);
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
                "(^|[^A-Za-z0-9_:])translated\\(\\s*QStringLiteral\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")),
            2
        },
        {
            QRegularExpression(QStringLiteral(
                "\\{\\s*(?:QDialogButtonBox|QMessageBox)::[A-Za-z0-9_]+\\s*,\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")),
            1
        },
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
        const QString text = readFile(path);
        for (const auto& pattern : keyPatterns) {
            QRegularExpressionMatchIterator mi = pattern.re.globalMatch(text);
            while (mi.hasNext()) {
                const QRegularExpressionMatch m = mi.next();
                const QString key = unescapeLiteral(m.captured(pattern.keyGroup));
                literalKeys.insert(key);
                if (!UiText::hasTranslationKey(key)) {
                    missingKeys.insert(key);
                }
            }
        }
        ++scanned;
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
                if (!UiText::hasTranslationKey(key)) {
                    missingKeys.insert(key);
                }
            }
        }
    }

    if (scanned == 0) {
        err << "ui_text_locale_spec: scanned 0 source files under " << srcDir
            << " - is MIACODE_SOURCE_ROOT correct?" << Qt::endl;
        return 1;
    }

    if (!missingKeys.isEmpty()) {
        ok = false;
        QStringList sorted(missingKeys.begin(), missingKeys.end());
        sorted.sort();
        err << sorted.size()
            << " source/resource literal UiText key(s) missing from the translation tables:"
            << Qt::endl;
        for (const QString& key : sorted) {
            err << "  - " << key << Qt::endl;
        }
        err << "Fix: add each key to the built-in translation maps, or correct the "
               "call-site typo." << Qt::endl;
    }

    if (!shortcutActionsMissingLabelKey.isEmpty()) {
        ok = false;
        shortcutActionsMissingLabelKey.sort();
        err << shortcutActionsMissingLabelKey.size()
            << " shortcut action(s) missing label_key in resources/shortcuts.json:"
            << Qt::endl;
        for (const QString& actionId : shortcutActionsMissingLabelKey) {
            err << "  - " << actionId << Qt::endl;
        }
        err << "Fix: add a label_key that exists in the built-in translation maps."
            << Qt::endl;
    }

    if (!ok) {
        return 1;
    }

    out << "ui_text_locale_spec ok (" << literalKeys.size()
        << " literal key lookup(s) checked across " << scanned
        << " source files plus shortcuts.json)" << Qt::endl;
    return 0;
}
