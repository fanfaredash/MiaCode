#include "preferences/LocaleService.h"

#include "preferences/PreferenceDocument.h"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QTranslator>

namespace miacode {

namespace {

QString qmResourcePathForToken(const QString& token)
{
    if (token.startsWith(QStringLiteral("zh"))) {
        return QStringLiteral(":/i18n/miacode_zh_CN.qm");
    }
    if (token.startsWith(QStringLiteral("ja"))) {
        return QStringLiteral(":/i18n/miacode_ja_JP.qm");
    }
    return QStringLiteral(":/i18n/miacode_en.qm");
}

}  // namespace

LocaleService& LocaleService::instance()
{
    static LocaleService service;
    return service;
}

LocaleService::LocaleService(QObject* parent)
    : QObject(parent)
{
}

LocaleService::~LocaleService()
{
    if (translator_ != nullptr) {
        QCoreApplication::removeTranslator(translator_);
        delete translator_;
        translator_ = nullptr;
    }
}

void LocaleService::setQmlEngine(QQmlEngine* engine)
{
    engine_ = engine;
}

void LocaleService::applyResolvedLanguage()
{
    loadTranslatorForToken(PreferenceDocument::resolvedLanguageToken());
}

void LocaleService::setLanguageToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty()) {
        return;
    }
    PreferenceDocument::setPreferredLanguageToken(normalized);
    const QString resolved = PreferenceDocument::resolvedLanguageToken();
    if (!loadTranslatorForToken(resolved)) {
        return;
    }
    retranslateEngine();
    emit languageChanged(resolved);
}

bool LocaleService::loadTranslatorForToken(const QString& token)
{
    const QString path = qmResourcePathForToken(token);
    auto* next = new QTranslator(this);
    if (!next->load(path)) {
        delete next;
        return false;
    }

    if (translator_ != nullptr) {
        QCoreApplication::removeTranslator(translator_);
        delete translator_;
        translator_ = nullptr;
    }

    if (!QCoreApplication::installTranslator(next)) {
        delete next;
        return false;
    }

    translator_ = next;
    activeToken_ = token.startsWith(QStringLiteral("zh"))
        ? QStringLiteral("zh")
        : (token.startsWith(QStringLiteral("ja")) ? QStringLiteral("ja")
                                                  : QStringLiteral("en"));
    return true;
}

void LocaleService::retranslateEngine()
{
    if (engine_ != nullptr) {
        engine_->retranslate();
    }
}

}  // namespace miacode
