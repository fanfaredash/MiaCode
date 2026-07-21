#include "tools/video_export/FontLibrary.h"

#include <QApplication>
#include <QComboBox>
#include <QSizePolicy>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyNarrowFontComboRemainsVisible(QTextStream& err)
{
    QComboBox narrowCombo;
    narrowCombo.addItem(QStringLiteral("A deliberately long font family name"));
    miacode::video_export::configureFontComboWidth(
        &narrowCombo, miacode::video_export::FontComboWidthMode::NarrowInspector);

    QComboBox standardCombo;
    standardCombo.addItem(QStringLiteral("A deliberately long font family name"));
    miacode::video_export::configureFontComboWidth(
        &standardCombo, miacode::video_export::FontComboWidthMode::StandardForm);

    return require(
        narrowCombo.sizePolicy().horizontalPolicy() == QSizePolicy::Expanding,
        QStringLiteral("narrow font combo must remain horizontally expanding"), err)
        && require(
            narrowCombo.minimumContentsLength() == 8,
            QStringLiteral("narrow font combo must use the compact eight-character budget"), err)
        && require(
            standardCombo.minimumContentsLength() > narrowCombo.minimumContentsLength(),
            QStringLiteral("standard font combo must reserve more text width than a narrow inspector"), err);
}

}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTextStream err(stderr);

    return verifyNarrowFontComboRemainsVisible(err) ? 0 : 1;
}
