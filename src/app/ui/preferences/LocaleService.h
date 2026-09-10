#pragma once

#include <QObject>
#include <QString>

class QQmlEngine;
class QTranslator;

namespace miacode {

// Owns the process-wide QTranslator install for ID-based catalogs
// (qtTrId / qsTrId). Language preference persistence stays on PreferenceDocument;
// this service only loads .qm files and asks the QML engine to retranslate.
class LocaleService final : public QObject
{
    Q_OBJECT

public:
    static LocaleService& instance();

    // Install the catalog matching PreferenceDocument::resolvedLanguageToken(). Safe to
    // call before any QML engine exists.
    void applyResolvedLanguage();

    // Persist token via PreferenceDocument, reload translators, then retranslate.
    void setLanguageToken(const QString& token);

    QString activeLanguageToken() const { return activeToken_; }

    void setQmlEngine(QQmlEngine* engine);

signals:
    void languageChanged(const QString& token);

private:
    explicit LocaleService(QObject* parent = nullptr);
    ~LocaleService() override;

    bool loadTranslatorForToken(const QString& token);
    void retranslateEngine();

    QTranslator* translator_ = nullptr;
    QQmlEngine* engine_ = nullptr;
    QString activeToken_;
};

}  // namespace miacode
