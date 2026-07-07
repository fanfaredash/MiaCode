// Drift guard for the UiText localization tables.
//
// Two invariants, both of which have silently broken before (see the 2026-07-07
// audit, docs/audit/I18N_AND_UI_COMPONENT_AUDIT_ZH.md):
//
//   1. zhMap and jaMap in UiText.cpp must have identical key sets. When they
//      drift, the *most complete* language (Simplified Chinese, the reference
//      language) falls back to the call-site English string for the keys the
//      other table added — exactly the 48-key drift the audit found.
//
//   2. Every inline `UiText::localized(QStringLiteral(en), QStringLiteral(zh))`
//      2-argument call in src/ must have a Japanese entry in the central
//      zh-keyed dictionary (UiTextJaDictionary.cpp). Without it, Japanese
//      silently falls back to English for that string. Simplified Chinese is
//      the reference language: new UI strings are authored as (en, zh) and the
//      Japanese is filled in from the Chinese here.
//
// The 3-argument form `localized(en, zh, ja)` supplies Japanese inline and is
// intentionally NOT required to have a dictionary entry. Helper forwarders
// (l10n / localizedText / trText) are also not enforced — a missing entry there
// is a safe English fallback, not a silent regression of the canonical API.
//
// The repo root is injected at configure time via MIACODE_SOURCE_ROOT.

#include "UiText.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
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
// produce, so it can be matched against dictionary keys.
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

bool hasCjk(const QString& s)
{
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x4E00 && u <= 0x9FFF) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    bool ok = true;

    // --- Invariant 1: zhMap / jaMap key-set parity. ------------------------
    const QStringList mismatches = UiText::translationKeyMismatches();
    if (!mismatches.isEmpty()) {
        ok = false;
        err << mismatches.size()
            << " zhMap/jaMap key-set mismatch(es) in UiText.cpp:" << Qt::endl;
        for (const QString& m : mismatches) {
            err << "  - " << m << Qt::endl;
        }
        err << "Fix: add the missing key to the other map. Simplified Chinese is the "
               "reference language; translate from it." << Qt::endl;
    }

    // --- Invariant 2: inline localized(en, zh) zh strings are in the dict. --
    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT);
    const QString srcDir = root + QStringLiteral("/src");

    // Match UiText::localized( QStringLiteral("EN") , QStringLiteral("ZH") )
    // where the argument list closes right after the second literal — i.e. the
    // 2-arg form. A trailing comma before ')' would indicate a 3-arg call and
    // is deliberately excluded by requiring ')' after optional whitespace.
    static const QRegularExpression re(
        QStringLiteral(
            "UiText::localized\\(\\s*"
            "QStringLiteral\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)\\s*,\\s*"
            "QStringLiteral\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)\\s*\\)"),
        QRegularExpression::DotMatchesEverythingOption);

    const QHash<QString, QString>& jaDict = UiText::japaneseByChineseText();

    QSet<QString> missing;
    int scanned = 0;
    int callsChecked = 0;
    QDirIterator it(
        srcDir,
        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString text = readFile(path);
        if (!text.contains(QStringLiteral("UiText::localized"))) {
            ++scanned;
            continue;
        }
        QRegularExpressionMatchIterator mi = re.globalMatch(text);
        while (mi.hasNext()) {
            const QRegularExpressionMatch m = mi.next();
            const QString zh = unescapeLiteral(m.captured(2));
            if (!hasCjk(zh)) {
                continue;  // en/en pair (rare) — nothing to translate.
            }
            ++callsChecked;
            if (!jaDict.contains(zh)) {
                missing.insert(zh);
            }
        }
        ++scanned;
    }

    if (scanned == 0) {
        err << "ui_text_locale_spec: scanned 0 source files under " << srcDir
            << " — is MIACODE_SOURCE_ROOT correct?" << Qt::endl;
        return 1;
    }

    if (!missing.isEmpty()) {
        ok = false;
        QStringList sorted(missing.begin(), missing.end());
        sorted.sort();
        err << sorted.size()
            << " inline UiText::localized(en, zh) string(s) missing a Japanese "
               "dictionary entry (UiTextJaDictionary.cpp):" << Qt::endl;
        for (const QString& zh : sorted) {
            QString oneLine = zh;
            oneLine.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            err << "  - " << oneLine << Qt::endl;
        }
        err << "Fix: add each zh key with its Japanese translation to "
               "japaneseByChineseText() in src/app/ui/UiTextJaDictionary.cpp." << Qt::endl;
    }

    if (!ok) {
        return 1;
    }

    out << "ui_text_locale_spec ok (" << jaDict.size() << " ja dict entries, "
        << callsChecked << " inline localized() calls checked across "
        << scanned << " files)" << Qt::endl;
    return 0;
}
