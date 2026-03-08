#include "MainWindow.h"
#include "PlainCodeEditor.h"
#include "PreviewCanvas.h"
#include "PreviewIntegration.h"
#include "PreviewMediaController.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "tools/LatencyDetectorDialog.h"
#include "common/AssetPaths.h"

#include <algorithm>
#include <QAction>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontInfo>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSplitterHandle>
#include <QScreen>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QSysInfo>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStyleOptionSlider>
#include <QStringList>
#include <QStringConverter>
#include <QTimer>
#include <QTextBlock>
#include <QTextStream>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextOption>
#include <QToolBar>
#include <QThreadPool>
#include <QWidgetAction>
#include <QToolTip>
#include <QtMath>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif
#include <QVBoxLayout>
#include <QSizePolicy>

#include <cmath>

#include "../third_party/miniaudio/miniaudio.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kEmbeddedPreviewPanelMinWidth = 320;
constexpr qreal kEmbeddedPreviewPanelWidthRatio = 0.50;
constexpr int kEmbeddedPreviewPanelWidthMax = 900;
constexpr int kPreviewPanelMarginX = 8;
constexpr int kPreviewPanelMarginTop = 12;
constexpr int kPreviewPanelMarginBottom = 12;
constexpr int kPreviewCanvasControlGap = 10;
constexpr int kPreviewStatsBottomGap = 12;
constexpr int kPreviewControlStatsGap = 10;
constexpr int kPreviewControlStatsCardMinWidth = 280;

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);

        painter->save();
        const QRect fillRect = option.rect.adjusted(1, 1, -1, -1);
        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(QColor("#9EC2EF"), 1.0));
            painter->setBrush(QColor("#F1F6FF"));
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#F3F7FD"));
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }
        painter->restore();

        drawOption.state &= ~QStyle::State_Selected;
        drawOption.state &= ~QStyle::State_MouseOver;
        drawOption.backgroundBrush = Qt::NoBrush;
        drawOption.palette.setColor(QPalette::HighlightedText, QColor("#243447"));
        QStyledItemDelegate::paint(painter, drawOption, index);
    }
};

class LeftPlaceholderLineEdit : public QLineEdit {
public:
    explicit LeftPlaceholderLineEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {}

    void setLeftPlaceholderText(const QString& text)
    {
        leftPlaceholderText_ = text;
        QLineEdit::setPlaceholderText(QString());
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QLineEdit::paintEvent(event);
        if (!text().isEmpty() || leftPlaceholderText_.isEmpty()) {
            return;
        }

        QStyleOptionFrame option;
        initStyleOption(&option);
        const QRect contentsRect = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);

        QPainter painter(this);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.setFont(font());
        painter.drawText(contentsRect.adjusted(0, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, leftPlaceholderText_);
    }

private:
    QString leftPlaceholderText_;
};

QString timestampLine(const QString& title)
{
    return QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(title);
}

QString uiText(const QString& key, const QString& fallback)
{
    const QString localized = UiText::text(key);
    return localized.isEmpty() ? fallback : localized;
}

QFont editorFont()
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(13);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    static const QString embeddedConsolasFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(":/fonts/consola.ttf");
        if (fontId < 0) {
            return QString();
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty()) {
            return QString();
        }
        return families.first();
    }();

    QFont font;
    if (!embeddedConsolasFamily.isEmpty()) {
        font.setFamily(embeddedConsolasFamily);
    } else {
        font.setFamily("Consolas");
        if (!QFontInfo(font).exactMatch()) {
            font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(11);
    return font;
#endif
}

QFont uiOutputFont()
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(12);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPointSize(11);
    return font;
#endif
}

QFont uiAccentFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"PingFang SC", "Hiragino Sans GB"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + ((pointSize <= 11) ? 1 : 0));
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    QStringList familyCandidates;
    if (UiText::isChineseUi()) {
        familyCandidates << "Microsoft YaHei UI" << "Microsoft YaHei" << "PingFang SC" << "Noto Sans CJK SC";
    }
    familyCandidates << "Segoe UI Variable Text" << "Segoe UI";
    for (const QString& family : familyCandidates) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#endif
}

QFont uiMonoFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + 1);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    for (const QString& family : QStringList{"Cascadia Mono", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
#endif
}

QProcessEnvironment pythonProcessEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    return env;
}

QString decodeProcessText(const QByteArray& data)
{
    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString utf8Text = utf8Decoder.decode(data);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }
    return QString::fromLocal8Bit(data);
}

QByteArray noteMarkerSignature(const QVector<TimelineNoteMarker>& notes)
{
    QByteArray signature;
    signature.reserve(notes.size() * 48);
    for (const TimelineNoteMarker& marker : notes) {
        signature.append(QByteArray::number(marker.sourceLine));
        signature.append('|');
        signature.append(QByteArray::number(marker.lane));
        signature.append('|');
        signature.append(QByteArray::number(marker.endLane));
        signature.append('|');
        signature.append(QByteArray::number(marker.second, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.endSecond, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.slideTraceSecond, 'f', 6));
        signature.append('|');
        signature.append(marker.type.toUtf8());
        signature.append(';');
    }
    return signature;
}

QVector<float> buildWaveformPeaks(const QString& trackPath, double* durationSeconds, int peakCount = 1024)
{
    QVector<float> peaks;
    if (durationSeconds != nullptr) {
        *durationSeconds = 0.0;
    }
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath) || peakCount <= 0) {
        return peaks;
    }

    const QByteArray pathBytes = QFile::encodeName(trackPath);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (ma_decoder_init_file(pathBytes.constData(), &config, &decoder) != MA_SUCCESS) {
        return peaks;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return peaks;
    }

    const double sampleRate = 48000.0;
    if (durationSeconds != nullptr) {
        *durationSeconds = static_cast<double>(totalFrames) / sampleRate;
    }
    peaks.fill(0.0f, peakCount);
    constexpr ma_uint64 kChunkFrames = 4096;
    QVector<float> buffer(static_cast<int>(kChunkFrames), 0.0f);
    ma_uint64 frameCursor = 0;

    while (frameCursor < totalFrames) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buffer.data(), kChunkFrames, &framesRead) != MA_SUCCESS || framesRead == 0) {
            break;
        }
        for (ma_uint64 i = 0; i < framesRead; ++i) {
            const ma_uint64 absoluteFrame = frameCursor + i;
            const int binIndex = qBound(
                0,
                static_cast<int>((absoluteFrame * peakCount) / qMax<ma_uint64>(1, totalFrames)),
                peakCount - 1
            );
            peaks[binIndex] = qMax(peaks[binIndex], qAbs(buffer[static_cast<int>(i)]));
        }
        frameCursor += framesRead;
    }

    ma_decoder_uninit(&decoder);
    return peaks;
}

QIcon makePreviewPlayIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(5.0, 3.0),
        QPointF(15.5, 10.0),
        QPointF(5.0, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makePreviewStopIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(4.5, 4.5, 11.0, 11.0), 1.8, 1.8);
    return QIcon(pixmap);
}

QColor difficultyColor(int difficultyId)
{
    switch (difficultyId) {
    case 1:
        return QColor("#69A6FF");
    case 2:
        return QColor("#78C85A");
    case 3:
        return QColor("#DCC548");
    case 4:
        return QColor("#E35C50");
    case 5:
        return QColor("#7A4FD1");
    case 6:
        return QColor("#D548B6");
    case 7:
        return QColor("#E29A46");
    default:
        return QColor("#8A8F98");
    }
}

QIcon makeRecycleTrashIcon(const QColor& color)
{
    QPixmap pixmap(20, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(7.2, 4.0), QPointF(13.0, 4.0));
    painter.drawLine(QPointF(8.2, 2.8), QPointF(12.0, 2.8));
    painter.drawRoundedRect(QRectF(6.4, 4.6, 7.4, 8.4), 1.5, 1.5);
    painter.drawArc(QRectF(8.1, 6.2, 3.2, 3.2), 30 * 16, 260 * 16);
    painter.drawArc(QRectF(8.8, 7.0, 3.0, 3.0), 210 * 16, 230 * 16);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF{
        QPointF(11.6, 6.3),
        QPointF(12.9, 6.6),
        QPointF(12.0, 7.6),
    });
    painter.drawPolygon(QPolygonF{
        QPointF(8.2, 9.6),
        QPointF(7.0, 9.1),
        QPointF(7.8, 8.2),
    });
    return QIcon(pixmap);
}

QIcon makeOutlineCloseIcon(const QColor& color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(3.0, 3.0), QPointF(9.0, 9.0));
    painter.drawLine(QPointF(9.0, 3.0), QPointF(3.0, 9.0));
    return QIcon(pixmap);
}

QPixmap makeDifficultyBadgePixmap(int difficultyId)
{
    const QColor fill = difficultyColor(difficultyId);
    const qreal dpr = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    QImage image(static_cast<int>(14 * dpr), static_cast<int>(14 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawRoundedRect(QRectF(2.0 * dpr, 2.0 * dpr, 10.0 * dpr, 10.0 * dpr), 3.0 * dpr, 3.0 * dpr);
    painter.end();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QIcon makeDifficultyBadgeIcon(int difficultyId)
{
    const QPixmap pixmap = makeDifficultyBadgePixmap(difficultyId);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::On);
    return icon;
}

QIcon makePreviewCursorIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(2.0, 3.0),
        QPointF(11.5, 10.0),
        QPointF(2.0, 17.0),
    });
    painter.drawPolygon(QPolygonF{
        QPointF(12.5, 2.5),
        QPointF(18.0, 12.5),
        QPointF(15.1, 12.2),
        QPointF(14.4, 17.2),
        QPointF(12.6, 16.9),
        QPointF(13.2, 11.8),
        QPointF(10.8, 13.4),
    });
    painter.end();
    return QIcon(pixmap);
}

QIcon makePreviewPauseIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(5.0, 3.0, 3.5, 14.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(11.5, 3.0, 3.5, 14.0), 1.2, 1.2);
    return QIcon(pixmap);
}

QIcon makePreviewResumeIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(3.0, 4.0, 2.6, 12.0), 1.0, 1.0);
    painter.drawPolygon(QPolygonF{
        QPointF(7.5, 3.0),
        QPointF(16.5, 10.0),
        QPointF(7.5, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makeSettingsGearIcon(const QColor& color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(9.0, 9.0), 3.0, 3.0);
    for (int i = 0; i < 8; ++i) {
        const qreal angle = (i * 45.0) * 0.017453292519943295;
        const QPointF outer(9.0 + qCos(angle) * 7.0, 9.0 + qSin(angle) * 7.0);
        const QPointF inner(9.0 + qCos(angle) * 5.0, 9.0 + qSin(angle) * 5.0);
        painter.drawLine(inner, outer);
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorLeftRightIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(4.2, 10.0), QPointF(15.8, 10.0));
    p.drawLine(QPointF(5.9, 8.3), QPointF(4.2, 10.0));
    p.drawLine(QPointF(5.9, 11.7), QPointF(4.2, 10.0));
    p.drawLine(QPointF(14.1, 8.3), QPointF(15.8, 10.0));
    p.drawLine(QPointF(14.1, 11.7), QPointF(15.8, 10.0));
    p.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorUpDownIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(10.0, 4.2), QPointF(10.0, 15.8));
    p.drawLine(QPointF(8.3, 5.9), QPointF(10.0, 4.2));
    p.drawLine(QPointF(11.7, 5.9), QPointF(10.0, 4.2));
    p.drawLine(QPointF(8.3, 14.1), QPointF(10.0, 15.8));
    p.drawLine(QPointF(11.7, 14.1), QPointF(10.0, 15.8));
    p.end();
    return QIcon(pixmap);
}

QIcon makeTransformRotate180Icon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 35 * 16, 150 * 16);
    p.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 215 * 16, 150 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF{
        QPointF(15.9, 6.8),
        QPointF(17.6, 4.9),
        QPointF(14.9, 4.6),
    });
    p.drawPolygon(QPolygonF{
        QPointF(4.1, 13.2),
        QPointF(2.4, 15.1),
        QPointF(5.1, 15.4),
    });
    p.end();
    return QIcon(pixmap);
}

QPointF pointOnArcPrecise(const QRectF& rect, qreal deg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    return QPointF(
        cx + rx * std::cos(rad),
        cy - ry * std::sin(rad)
    );
}

QPointF tangentOnArcPrecise(const QRectF& rect, qreal deg, qreal sweepDeg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    QPointF t(
        -rx * std::sin(rad),
        -ry * std::cos(rad)
    );
    if (sweepDeg < 0.0) {
        t = -t;
    }
    const qreal len = std::hypot(t.x(), t.y());
    if (len < 1e-6) {
        return QPointF(1.0, 0.0);
    }
    return QPointF(t.x() / len, t.y() / len);
}

QPixmap makeTransformRotate45Pixmap(const QColor& color, bool clockwise)
{
    constexpr qreal logicalSize = 20.0;
    constexpr qreal dpr = 2.0;
    QPixmap pixmap(static_cast<int>(logicalSize * dpr), static_cast<int>(logicalSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arcRect(3.4, 3.4, 13.2, 13.2);
    const qreal startDeg = clockwise ? 42.0 : 138.0;
    const qreal fullSweepDeg = clockwise ? -250.0 : 250.0;
    const qreal arcSweepDeg = clockwise ? -236.0 : 236.0;

    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcRect, qRound(startDeg * 16.0), qRound(arcSweepDeg * 16.0));

    const qreal endDeg = startDeg + arcSweepDeg;
    const QPointF tip = pointOnArcPrecise(arcRect, endDeg);
    const QPointF dir = tangentOnArcPrecise(arcRect, endDeg, arcSweepDeg);
    const QPointF normal(-dir.y(), dir.x());
    constexpr qreal headLength = 5.2;
    constexpr qreal headWidth = 4.2;
    const QPointF baseCenter = tip;
    const QPointF headTip = baseCenter + dir * headLength;
    QPolygonF head;
    head << headTip
         << (baseCenter + normal * (headWidth * 0.5))
         << (baseCenter - normal * (headWidth * 0.5));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(head);
    p.end();
    return pixmap;
}

QIcon makeTransformRotateCcw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, false));
}

QIcon makeTransformRotateCw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, true));
}

QString modernScrollBarStyle()
{
    return QStringLiteral(
        "QScrollBar:vertical {"
        " background: #F4F7FB;"
        " width: 12px;"
        " margin: 2px;"
        " border: 1px solid #D1DDEA;"
        " border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        " background: #9CB5CE;"
        " min-height: 36px;"
        " border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #81A2C3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal {"
        " background: #F4F7FB;"
        " height: 12px;"
        " margin: 2px;"
        " border: 1px solid #D1DDEA;"
        " border-radius: 6px;"
        "}"
        "QScrollBar::handle:horizontal {"
        " background: #9CB5CE;"
        " min-width: 36px;"
        " border-radius: 5px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #81A2C3; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; background: transparent; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
    );
}

void styleRoundedMenu(QMenu& menu)
{
    menu.setWindowFlag(Qt::FramelessWindowHint, true);
    menu.setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu.setAttribute(Qt::WA_TranslucentBackground, true);
    menu.setStyleSheet(
        "QMenu {"
        " background: rgba(255, 255, 255, 245);"
        " border: 1px solid #D7E0EB;"
        " border-radius: 8px;"
        " padding: 7px;"
        "}"
        "QMenu::item {"
        " padding: 6px 20px 6px 12px;"
        " margin: 1px 0;"
        " border-radius: 6px;"
        " color: #203040;"
        " background: transparent;"
        "}"
        "QMenu::item:selected {"
        " background: #EEF5FF;"
        " color: #203040;"
        "}"
        "QMenu::item:disabled {"
        " color: #9AA5B4;"
        " background: transparent;"
        "}"
    );
}

bool hasRuntimeDebugArg(const QStringList& args)
{
    return args.contains("--miacode-debug")
        || args.contains("--debug-runtime")
        || args.contains("--enable-debug-output");
}

QString startupTimingLogPath()
{
    return QDir::temp().filePath("miacode_startup_timing.log");
}

QString runtimeDebugLogPath()
{
    return QDir::temp().filePath("miacode_runtime_debug.log");
}

bool startupTimingEnabled()
{
    static const bool enabled = []() {
        const QString raw = qEnvironmentVariable(
            "MIACODE_ENABLE_STARTUP_TIMING",
            qEnvironmentVariable("MAIMURI_ENABLE_STARTUP_TIMING")
        ).trimmed();
        return raw == "1" || raw.compare("true", Qt::CaseInsensitive) == 0;
    }();
    return enabled;
}

void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    if (!startupTimingEnabled()) {
        return;
    }
    QFile logFile(startupTimingLogPath());
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&logFile);
    out << "stage=" << stage
        << ", elapsed_ms=" << elapsedMs
        << ", delta_ms=" << deltaMs
        << "\n";
}

}  // namespace

void MainWindow::configureRuntimeDebugOutput()
{
    const QStringList appArgs = QCoreApplication::arguments();
    runtimeDebugOutputEnabled_ = hasRuntimeDebugArg(appArgs);
    if (runtimeDebugOutputEnabled_) {
        qputenv("MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT", "1");
        QFile::remove(runtimeDebugLogPath());
    } else {
        qunsetenv("MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT");
    }
}

void MainWindow::ensurePreviewMediaControllerInitialized()
{
    if (previewMediaController_ != nullptr) {
        return;
    }

    QElapsedTimer initTimer;
    initTimer.start();
    previewMediaController_ = new PreviewMediaController(this);

    connect(previewMediaController_, &PreviewMediaController::frameChanged, this, [this](const QImage& frame) {
        if (previewCanvas_ == nullptr) {
            return;
        }
        previewCanvas_->setMediaFrame(frame);
        previewCanvas_->update();
    });
#ifdef HAVE_QT_MULTIMEDIA
    connect(previewMediaController_, &PreviewMediaController::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (previewCanvas_ == nullptr) {
            return;
        }
        previewCanvas_->setVideoFrame(frame);
        if (!qtPreviewPlaying_) {
            previewCanvas_->update();
        }
    });
#endif
    connect(
        previewMediaController_,
        &PreviewMediaController::backgroundBrightnessChanged,
        previewCanvas_,
        &PreviewCanvas::setBackgroundBrightness
    );
    connect(previewMediaController_, &PreviewMediaController::playbackPositionChanged, this, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        applyQtPreviewPosition(second, true);
    });
    connect(previewMediaController_, &PreviewMediaController::playbackFinished, this, [this]() {
        if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
            qtPreviewPauseSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
        } else if (previewMediaController_ != nullptr) {
            qtPreviewPauseSecond_ = previewMediaController_->currentPlaybackSecond();
        }
        stopQtPreviewPlayback(true);
        statusBar()->showMessage("Qt preview reached the end of current media.");
    });

    previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
    previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    previewMediaController_->setBackgroundTrackPath(lastTrackPath_);
    previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
    previewMediaController_->setTimelineOffsetSeconds(parsedFirstSeconds());
    previewMediaController_->setChartPath(currentFilePath_);
    previewMediaController_->setPlayheadSeconds(qtPreviewPauseSecond_);

    const qint64 elapsedMs = initTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_media_controller_lazy_init", elapsedMs, elapsedMs);
}

void MainWindow::ensurePreviewSfxRuntimePrepared()
{
    if (previewSfxRuntime_ == nullptr || previewSfxRuntimePrepared_) {
        return;
    }
    QElapsedTimer initTimer;
    initTimer.start();
    previewSfxRuntime_->reloadAssets(previewAudioSettings_);
    previewSfxRuntime_->setChartPath(currentFilePath_);
    previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    previewSfxRuntimePrepared_ = true;
    const qint64 elapsedMs = initTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_sfx_runtime_prepare_on_demand", elapsedMs, elapsedMs);
}

void MainWindow::schedulePreviewSubsystemWarmup()
{
    if (previewSubsystemWarmupScheduled_) {
        return;
    }
    previewSubsystemWarmupScheduled_ = true;
    previewSubsystemWarmupPendingTasks_ = 2;

    const PreviewAudioSettings audioSettingsSnapshot = previewAudioSettings_;
    const QString chartPathSnapshot = currentFilePath_;
    const double playbackRateSnapshot = previewPlaybackRate_;
    QPointer<MainWindow> guard(this);

    QThreadPool::globalInstance()->start([guard]() {
        QElapsedTimer timer;
        timer.start();
#ifdef HAVE_QT_MULTIMEDIA
        PreviewMediaController controllerWarmup;
#endif
        const qint64 elapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, elapsedMs]() {
                if (guard.isNull()) {
                    return;
                }
                appendStartupTimingStage("mainwindow/preview_media_controller_worker_warmup", elapsedMs, elapsedMs);
                guard->previewSubsystemWarmupPendingTasks_ = qMax(0, guard->previewSubsystemWarmupPendingTasks_ - 1);
                guard->tryFinalizePreviewSubsystemWarmup();
            },
            Qt::QueuedConnection
        );
    }, -1);

    QThreadPool::globalInstance()->start([guard, audioSettingsSnapshot, chartPathSnapshot, playbackRateSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        QtPreviewSfxRuntime runtimeWarmup;
        runtimeWarmup.setChartPath(chartPathSnapshot);
        runtimeWarmup.setBackgroundTrackPlaybackRate(playbackRateSnapshot);
        runtimeWarmup.reloadAssets(audioSettingsSnapshot);
        const qint64 elapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, elapsedMs]() {
                if (guard.isNull()) {
                    return;
                }
                appendStartupTimingStage("mainwindow/preview_sfx_worker_warmup", elapsedMs, elapsedMs);
                guard->previewSubsystemWarmupPendingTasks_ = qMax(0, guard->previewSubsystemWarmupPendingTasks_ - 1);
                guard->tryFinalizePreviewSubsystemWarmup();
            },
            Qt::QueuedConnection
        );
    }, -1);
}

void MainWindow::tryFinalizePreviewSubsystemWarmup()
{
    if (!previewSubsystemWarmupScheduled_
        || previewSubsystemWarmupFinalized_
        || previewSubsystemWarmupPendingTasks_ > 0) {
        return;
    }
    previewSubsystemWarmupFinalized_ = true;

    QElapsedTimer applyTimer;
    applyTimer.start();
    ensurePreviewSfxRuntimePrepared();
    ensurePreviewMediaControllerInitialized();
    const qint64 elapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_subsystem_warmup_apply", elapsedMs, elapsedMs);
}

void MainWindow::setupInitialWindowGeometry()
{
    QSize initialSize(1280, 800);
    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect workArea = screen->availableGeometry();
        initialSize.setWidth(qMin(initialSize.width(), qMax(960, workArea.width() - 120)));
        initialSize.setHeight(qMin(initialSize.height(), qMax(640, workArea.height() - 120)));
        resize(initialSize);
        move(workArea.center() - QPoint(width() / 2, height() / 2));
        return;
    }
    resize(initialSize);
}

void MainWindow::setupMenusAndActions(QMenu* fileMenu, QMenu* toolsMenu, QMenu* transformMenu, QMenu* helpMenu)
{
    if (fileMenu == nullptr || toolsMenu == nullptr || transformMenu == nullptr || helpMenu == nullptr) {
        return;
    }

    newAction_ = new QAction(uiText("action.new", "New"), this);
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &MainWindow::onNewFile);
    fileMenu->addAction(newAction_);

    openAction_ = new QAction(uiText("action.open", "Open..."), this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::onOpenFile);
    fileMenu->addAction(openAction_);

    saveAction_ = new QAction(uiText("action.save", "Save"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSaveFile);
    fileMenu->addAction(saveAction_);

    saveAsAction_ = new QAction(uiText("action.save_as", "Save As..."), this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onSaveFileAs);
    fileMenu->addAction(saveAsAction_);

    fileMenu->addSeparator();

    preferencesAction_ = new QAction(uiText("action.preferences", "Preferences..."), this);
    connect(preferencesAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    fileMenu->addAction(preferencesAction_);

    fileMenu->addSeparator();

    auto* quitAction = new QAction("Quit", this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    validateAction_ = new QAction(uiText("action.validate", "Validate Simai"), this);
    validateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(validateAction_, &QAction::triggered, this, &MainWindow::onValidateSimai);
    toolsMenu->addAction(validateAction_);

    stopPreviewAction_ = new QAction(uiText("action.stop_preview", "Stop Preview"), this);
    stopPreviewAction_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    stopPreviewAction_->setToolTip(QString());
    connect(stopPreviewAction_, &QAction::triggered, this, &MainWindow::onStopPreview);
    toolsMenu->addAction(stopPreviewAction_);

    pausePreviewAction_ = new QAction(uiText("action.pause_preview", "Play/Pause Preview"), this);
    pausePreviewAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
    pausePreviewAction_->setToolTip(QString());
    connect(pausePreviewAction_, &QAction::triggered, this, &MainWindow::onTogglePreviewPause);
    toolsMenu->addAction(pausePreviewAction_);

    latencyDetectorAction_ = new QAction(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection..."), this);
    connect(latencyDetectorAction_, &QAction::triggered, this, &MainWindow::onOpenLatencyDetector);
    toolsMenu->addAction(latencyDetectorAction_);

    toolsMenu->addSeparator();

    toggleJudgeMarkersAction_ = new QAction("Show Judge Markers", this);
    toggleJudgeMarkersAction_->setCheckable(true);
    toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    connect(toggleJudgeMarkersAction_, &QAction::toggled, this, &MainWindow::onToggleJudgeMarkers);
    toolsMenu->addAction(toggleJudgeMarkersAction_);
    toggleJudgeMarkersAction_->setEnabled(false);
    toggleJudgeMarkersAction_->setVisible(false);

    toggleTouchTrailAction_ = new QAction("Show Touch Trail", this);
    toggleTouchTrailAction_->setCheckable(true);
    toggleTouchTrailAction_->setChecked(showTouchTrail_);
    connect(toggleTouchTrailAction_, &QAction::toggled, this, &MainWindow::onToggleTouchTrail);
    toolsMenu->addAction(toggleTouchTrailAction_);
    toggleTouchTrailAction_->setEnabled(false);
    toggleTouchTrailAction_->setVisible(false);

    toolsMenu->addSeparator();

    previewRenderSettingsAction_ = new QAction(uiText("action.render_settings", "Render Settings..."), this);
    connect(previewRenderSettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewRenderSettings);
    toolsMenu->addAction(previewRenderSettingsAction_);

    transformMirrorLeftRightAction_ = new QAction(uiText("action.transform.mirror_lr", "Mirror Left/Right"), this);
    transformMirrorLeftRightAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    transformMirrorLeftRightAction_->setIcon(makeTransformMirrorLeftRightIcon(QColor("#2B3C4E")));
    connect(transformMirrorLeftRightAction_, &QAction::triggered, this, &MainWindow::onMirrorLeftRight);
    transformMenu->addAction(transformMirrorLeftRightAction_);

    transformMirrorUpDownAction_ = new QAction(uiText("action.transform.mirror_ud", "Mirror Up/Down"), this);
    transformMirrorUpDownAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    transformMirrorUpDownAction_->setIcon(makeTransformMirrorUpDownIcon(QColor("#2B3C4E")));
    connect(transformMirrorUpDownAction_, &QAction::triggered, this, &MainWindow::onMirrorUpDown);
    transformMenu->addAction(transformMirrorUpDownAction_);

    transformRotate180Action_ = new QAction(uiText("action.transform.rotate_180", "Rotate 180"), this);
    transformRotate180Action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    transformRotate180Action_->setIcon(makeTransformRotate180Icon(QColor("#2B3C4E")));
    connect(transformRotate180Action_, &QAction::triggered, this, &MainWindow::onRotate180);
    transformMenu->addAction(transformRotate180Action_);

    transformRotate45CounterClockwiseAction_ = new QAction(uiText("action.transform.rotate_ccw_45", "Rotate -45"), this);
    transformRotate45CounterClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    transformRotate45CounterClockwiseAction_->setIcon(makeTransformRotateCcw45Icon(QColor("#2B3C4E")));
    connect(transformRotate45CounterClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45CounterClockwise);
    transformMenu->addAction(transformRotate45CounterClockwiseAction_);

    transformRotate45ClockwiseAction_ = new QAction(uiText("action.transform.rotate_cw_45", "Rotate +45"), this);
    transformRotate45ClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    transformRotate45ClockwiseAction_->setIcon(makeTransformRotateCw45Icon(QColor("#2B3C4E")));
    connect(transformRotate45ClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45Clockwise);
    transformMenu->addAction(transformRotate45ClockwiseAction_);

    aboutAction_ = new QAction(uiText("action.about", "About"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(aboutAction_);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    QElapsedTimer startupStageTimer;
    startupStageTimer.start();
    qint64 startupLastMs = 0;
    const auto logStartupStage = [&](const QString& stageName) {
        const qint64 nowMs = startupStageTimer.elapsed();
        const qint64 deltaMs = nowMs - startupLastMs;
        startupLastMs = nowMs;
        appendStartupTimingStage(QString("mainwindow/%1").arg(stageName), nowMs, deltaMs);
    };

    configureRuntimeDebugOutput();
    logStartupStage("configure_runtime_debug_output");

    const QString legacyPreviewEnv = qEnvironmentVariable("MIACODE_ENABLE_PYGAME_PREVIEW", qEnvironmentVariable("MAIMURI_ENABLE_PYGAME_PREVIEW")).trimmed();
    legacyPygamePreviewEnabled_ =
        legacyPreviewEnv == "1" || legacyPreviewEnv.compare("true", Qt::CaseInsensitive) == 0;

    setWindowModified(false);
    updateWindowTitle();
    setupInitialWindowGeometry();

    auto* fileMenu = menuBar()->addMenu(uiText("menu.file", "&File"));
    auto* toolsMenu = menuBar()->addMenu(uiText("menu.tools", "&Tools"));
    auto* transformMenu = menuBar()->addMenu(uiText("menu.transform", "&Transform"));
    auto* helpMenu = menuBar()->addMenu(uiText("menu.help", "&Help"));

    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    setupMenusAndActions(fileMenu, toolsMenu, transformMenu, helpMenu);
    logStartupStage("menus_and_actions_ready");

    auto* editor = new PlainCodeEditor(this);
    const QFont codeFont = editorFont();
    editor->setFont(codeFont);
    editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor->setPlainText(QString());
#ifdef Q_OS_MACOS
    editor->setBlockSpacingPixels(2);
#else
    editor->setBlockSpacingPixels(1);
#endif
    editorWidget_ = editor;
    editorWidget_->setFont(codeFont);
    editorWidget_->setStyleSheet(
        "border: none;"
        "background: #FFFFFF;"
        "color: #1F1F1F;"
        "selection-background-color: #D7EBFF;"
    );
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
        if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
            vbar->setStyleSheet(modernScrollBarStyle());
        }
        if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
            hbar->setStyleSheet(modernScrollBarStyle());
        }
    }
    logStartupStage("editor_widget_ready");

    auto* central = new QWidget(this);
    central->setStyleSheet(
        "QWidget#EditorShell { background: #F5F7FA; }"
        "QFrame#EditorHeader { background: #FFFFFF; border-bottom: 1px solid #DEE4EC; }"
        "QLabel#EditorContext { color: #1F2D3D; font-weight: 700; }"
        "QLabel#EditorMeta { color: #5F6B7A; }"
        "QWidget#EditorDifficultyControls QLabel { color: #5F6B7A; }"
        "QWidget#EditorBatchTransformControls QToolButton {"
        " color: #223042;"
        " min-width: 24px;"
        " min-height: 22px;"
        " padding: 0;"
        " border: 1px solid #D2DCE8;"
        " border-radius: 5px;"
        " background: #FFFFFF;"
        "}"
        "QWidget#EditorBatchTransformControls QToolButton:hover { background: #F3F8FF; border-color: #9FC1E9; }"
        "QWidget#EditorBatchTransformControls QToolButton:pressed { background: #E7F1FD; }"
        "QWidget#EditorDifficultyControls QLineEdit {"
        " background: #FFFFFF;"
        " color: #1F1F1F;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " padding: 4px 6px;"
        " selection-background-color: #D7EBFF;"
        "}"
        "QWidget#EditorDifficultyControls QLineEdit:focus { border-color: #3B82F6; }"
    );
    central->setObjectName("EditorShell");
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 4, 0, 0);
    centralLayout->setSpacing(0);

    auto* editorHeader = new QFrame(central);
    editorHeader->setObjectName("EditorHeader");
    editorHeaderWidget_ = editorHeader;
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    editorHeaderLayout->setContentsMargins(12, 8, 12, 8);
    editorHeaderLayout->setSpacing(10);
    editorContextLabel_ = new QLabel(uiText("editor.welcome", "Welcome to MiaCode!"), editorHeader);
    editorContextLabel_->setObjectName("EditorContext");
    editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
    editorContextLabel_->setMinimumWidth(0);
    editorContextLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    editorHeaderLayout->addWidget(editorContextLabel_, 1);

    editorBatchTransformControls_ = new QWidget(editorHeader);
    editorBatchTransformControls_->setObjectName("EditorBatchTransformControls");
    auto* editorBatchLayout = new QHBoxLayout(editorBatchTransformControls_);
    editorBatchLayout->setContentsMargins(0, 0, 0, 0);
    editorBatchLayout->setSpacing(4);
    const auto makeTransformButton = [this](QAction* action) -> QToolButton* {
        auto* button = new QToolButton(editorBatchTransformControls_);
        button->setObjectName("PreviewControlButton");
        button->setDefaultAction(action);
        button->setAutoRaise(false);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(15, 15));
        button->setFixedSize(26, 24);
        return button;
    };
    transformMirrorLeftRightButton_ = makeTransformButton(transformMirrorLeftRightAction_);
    transformMirrorUpDownButton_ = makeTransformButton(transformMirrorUpDownAction_);
    transformRotate180Button_ = makeTransformButton(transformRotate180Action_);
    transformRotate45CounterClockwiseButton_ = makeTransformButton(transformRotate45CounterClockwiseAction_);
    transformRotate45ClockwiseButton_ = makeTransformButton(transformRotate45ClockwiseAction_);
    editorBatchLayout->addWidget(transformMirrorLeftRightButton_);
    editorBatchLayout->addWidget(transformMirrorUpDownButton_);
    editorBatchLayout->addWidget(transformRotate180Button_);
    editorBatchLayout->addWidget(transformRotate45CounterClockwiseButton_);
    editorBatchLayout->addWidget(transformRotate45ClockwiseButton_);
    editorBatchTransformControls_->setVisible(false);

    editorDifficultyControls_ = new QWidget(editorHeader);
    editorDifficultyControls_->setObjectName("EditorDifficultyControls");
    auto* editorDifficultyLayout = new QHBoxLayout(editorDifficultyControls_);
    editorDifficultyLayout->setContentsMargins(0, 0, 0, 0);
    editorDifficultyLayout->setSpacing(8);
    auto* difficultyLevelLabel = new QLabel("Lv", editorDifficultyControls_);
    difficultyLevelLabel->setFont(uiAccentFont(10));
    auto* difficultyLevelLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyLevelLineEdit->setLeftPlaceholderText("&lv_n=");
    difficultyLevelEdit_ = difficultyLevelLineEdit;
    difficultyLevelEdit_->setMinimumWidth(0);
    difficultyLevelEdit_->setMaximumWidth(72);
    difficultyLevelEdit_->setAlignment(Qt::AlignCenter);
    difficultyLevelEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* difficultyDesignerLabel = new QLabel(uiText("editor.des", "Des"), editorDifficultyControls_);
    difficultyDesignerLabel->setFont(uiAccentFont(10));
    auto* difficultyDesignerLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyDesignerLineEdit->setLeftPlaceholderText("&des_n=");
    difficultyDesignerEdit_ = difficultyDesignerLineEdit;
    difficultyDesignerEdit_->setMinimumWidth(0);
    difficultyDesignerEdit_->setMaximumWidth(140);
    difficultyDesignerEdit_->setAlignment(Qt::AlignCenter);
    difficultyDesignerEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorDifficultyLayout->addWidget(difficultyLevelLabel);
    editorDifficultyLayout->addWidget(difficultyLevelEdit_);
    editorDifficultyLayout->addWidget(difficultyDesignerLabel);
    editorDifficultyLayout->addWidget(difficultyDesignerEdit_, 1);
    editorDifficultyLayout->addSpacing(10);
    editorDifficultyLayout->addWidget(editorBatchTransformControls_);
    editorDifficultyControls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorDifficultyControls_->hide();
    editorHeaderLayout->addWidget(editorDifficultyControls_, 0);

    editorHeaderLayout->addStretch(1);

    editorCursorLabel_ = new QLabel("Ln 1, Col 1", editorHeader);
    editorCursorLabel_->setObjectName("EditorMeta");
    editorCursorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editorCursorLabel_->setMinimumWidth(0);
    editorCursorLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    editorHeaderLayout->addWidget(editorCursorLabel_, 0, Qt::AlignRight);
    centralLayout->addWidget(editorHeader, 0);

    editorStack_ = new QStackedWidget(central);

    metadataPage_ = new QWidget(editorStack_);
    metadataPage_->setStyleSheet(
        "QWidget { background: #FFFFFF; color: #2A3440; }"
        "QFrame#MetadataCard { background: #FFFFFF; border: 1px solid #DEE4EC; border-radius: 8px; }"
        "QLabel#SectionTitle { color: #1F2D3D; font-weight: 700; padding-left: 4px; }"
        "QLabel#MetadataFieldLabel { color: #2A3440; background: transparent; padding-left: 8px; }"
        "QLineEdit, QPlainTextEdit {"
        " background: #FFFFFF;"
        " color: #1F1F1F;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " padding: 6px 8px;"
        " selection-background-color: #D7EBFF;"
        "}"
        "QLineEdit:focus, QPlainTextEdit:focus { border-color: #3B82F6; }"
    );
    auto* metadataLayout = new QVBoxLayout(metadataPage_);
    metadataLayout->setContentsMargins(12, 8, 12, 12);
    metadataLayout->setSpacing(8);

    auto* metadataCard = new QFrame(metadataPage_);
    metadataCard_ = metadataCard;
    metadataCard->setObjectName("MetadataCard");
    auto* metadataCardLayout = new QVBoxLayout(metadataCard);
    metadataCardLayout->setContentsMargins(14, 12, 14, 14);
    metadataCardLayout->setSpacing(12);

    auto* infoTitle = new QLabel(uiText("metadata.information", "Information"), metadataPage_);
    infoTitle->setObjectName("SectionTitle");
    infoTitle->setFont(uiAccentFont(12));
    metadataCardLayout->addWidget(infoTitle);

    auto* metadataForm = new QFormLayout();
    metadataForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    metadataForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metadataForm->setHorizontalSpacing(8);
    metadataForm->setVerticalSpacing(10);
    titleEdit_ = new QLineEdit(metadataPage_);
    artistEdit_ = new QLineEdit(metadataPage_);
    firstEdit_ = new QLineEdit(metadataPage_);
    auto* designerLineEdit = new LeftPlaceholderLineEdit(metadataPage_);
    designerLineEdit->setLeftPlaceholderText("&des=");
    designerEdit_ = designerLineEdit;
    titleEdit_->setPlaceholderText("&title=");
    artistEdit_->setPlaceholderText("&artist=");
    firstEdit_->setPlaceholderText("&first=");
    designerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const auto makeMetadataFieldLabel = [this](const QString& text) {
        auto* label = new QLabel(text, metadataPage_);
        label->setObjectName("MetadataFieldLabel");
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setMinimumWidth(46);
        return label;
    };
    firstEdit_->setFixedWidth(98);
    auto* firstWrap = new QWidget(metadataPage_);
    auto* firstWrapLayout = new QHBoxLayout(firstWrap);
    firstWrapLayout->setContentsMargins(0, 0, 0, 0);
    firstWrapLayout->setSpacing(6);
    firstWrapLayout->addWidget(firstEdit_, 0, Qt::AlignLeft);
    latencyDetectorButton_ = new QToolButton(metadataPage_);
    latencyDetectorButton_->setText(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection"));
    connect(latencyDetectorButton_, &QToolButton::clicked, this, &MainWindow::onOpenLatencyDetector);
    firstWrapLayout->addWidget(latencyDetectorButton_, 0, Qt::AlignLeft);
    firstWrapLayout->addStretch(1);

    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.title", "title")), titleEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.artist", "artist")), artistEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.des", "des")), designerEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.first", "first")), firstWrap);
    metadataCardLayout->addLayout(metadataForm);

    auto* extraMetadataLabel = new QLabel(uiText("metadata.other_fields", "Other &xx Fields"), metadataPage_);
    extraMetadataLabel->setObjectName("SectionTitle");
    extraMetadataLabel->setFont(uiAccentFont(11));
    metadataCardLayout->addWidget(extraMetadataLabel);
    metadataExtraEdit_ = new QPlainTextEdit(metadataPage_);
    metadataExtraEdit_->setFont(editorFont());
    metadataExtraEdit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    metadataExtraEdit_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    metadataExtraEdit_->setPlaceholderText("&dummy=...");
    if (QScrollBar* vbar = metadataExtraEdit_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    if (QScrollBar* hbar = metadataExtraEdit_->horizontalScrollBar()) {
        hbar->setStyleSheet(modernScrollBarStyle());
    }
    metadataCardLayout->addWidget(metadataExtraEdit_, 1);
    metadataEmptyHintLabel_ = new QLabel(uiText("metadata.empty_hint", "← Click to add a chart difficulty"), metadataPage_);
    metadataEmptyHintLabel_->setFont(uiAccentFont(11));
    metadataEmptyHintLabel_->setStyleSheet("color: #6A7890; background: transparent; padding-left: 6px;");
    metadataEmptyHintLabel_->hide();
    metadataLayout->addWidget(metadataEmptyHintLabel_, 0, Qt::AlignLeft | Qt::AlignTop);
    metadataLayout->addWidget(metadataCard, 1);

    chartPage_ = new QWidget(editorStack_);
    auto* chartLayout = new QVBoxLayout(chartPage_);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);
    chartLayout->addWidget(editorWidget_, 1);

    editorStack_->addWidget(metadataPage_);
    editorStack_->addWidget(chartPage_);
    centralLayout->addWidget(editorStack_, 1);
    logStartupStage("editor_stack_ready");

    auto* outlineDock = new QDockWidget("Fields", this);
    outlineDock->setObjectName("OutlineDock");
    outlineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* outlineTitle = new QWidget(outlineDock);
    outlineTitle->setFixedHeight(0);
    outlineDock->setTitleBarWidget(outlineTitle);
    outlineList_ = new QListWidget(outlineDock);
    outlineList_->setUniformItemSizes(true);
    outlineList_->setIconSize(QSize(14, 14));
    outlineList_->setSpacing(2);
    outlineList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outlineList_->setTextElideMode(Qt::ElideRight);
    outlineList_->setFont(uiAccentFont(11));
    outlineList_->setItemDelegate(new OutlineItemDelegate(outlineList_));
    outlineList_->setStyleSheet(
        "QListWidget {"
        " background: #FFFFFF;"
        " color: #243447;"
        " border: 1px solid #E1E7EF;"
        " padding: 6px;"
        " outline: none;"
        "}"
        "QListWidget::item {"
        " min-height: 28px;"
        " padding: 4px 12px;"
        " border: 1px solid transparent;"
        " border-radius: 6px;"
        "}"
        "QListWidget::item:selected { color: #243447; }"
    );
    outlineDock->setWidget(outlineList_);
    outlineList_->setMouseTracking(true);
    outlineList_->viewport()->setMouseTracking(true);
    outlineList_->viewport()->installEventFilter(this);
    deleteDifficultyButton_ = new QToolButton(outlineList_->viewport());
    deleteDifficultyButton_->setAutoRaise(true);
    deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(QColor("#5D6876")));
    deleteDifficultyButton_->setIconSize(QSize(12, 12));
    deleteDifficultyButton_->setToolTip("Delete the current difficulty");
    deleteDifficultyButton_->setCursor(Qt::PointingHandCursor);
    deleteDifficultyButton_->setFocusPolicy(Qt::NoFocus);
    deleteDifficultyButton_->setFixedSize(18, 18);
    deleteDifficultyButton_->setStyleSheet(
        "QToolButton {"
        " border: none;"
        " border-radius: 5px;"
        " background: transparent;"
        "}"
        "QToolButton:hover {"
        " background: #E9EEF4;"
        "}"
    );
    deleteDifficultyButton_->hide();
    connect(deleteDifficultyButton_, &QToolButton::clicked, this, [this]() {
        if (hasActiveDifficulty()) {
            deleteDifficultyField(activeDifficultyId_);
        }
    });
    connect(outlineList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        updateDifficultyDeleteButton(false);
        if (current == nullptr) {
            return;
        }
        const QString kind = current->data(Qt::UserRole).toString();
        const int difficultyId = current->data(Qt::UserRole + 1).toInt();
        if (kind == "metadata") {
            activeOutlineKey_ = "metadata";
            if (switchToMetadataField() && titleEdit_ != nullptr) {
                titleEdit_->setFocus();
            }
            return;
        }
        if (kind == "add") {
            QMenu menu(this);
            menu.setFont(uiAccentFont(10));
            styleRoundedMenu(menu);
            for (int id = 1; id <= 7; ++id) {
                if (document_.difficulty(id) != nullptr) {
                    continue;
                }
                auto* action = new QWidgetAction(&menu);
                auto* button = new QToolButton(&menu);
                button->setAutoRaise(true);
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                button->setIcon(makeDifficultyBadgeIcon(id));
                button->setIconSize(QSize(14, 14));
                button->setText(SimaiDocument::difficultyName(id));
                button->setFont(uiAccentFont(10));
                button->setCursor(Qt::PointingHandCursor);
                button->setStyleSheet(
                    "QToolButton {"
                    " color: #203040;"
                    " background: transparent;"
                    " border: none;"
                    " padding: 6px 20px 6px 12px;"
                    " text-align: left;"
                    "}"
                    "QToolButton:hover {"
                    " background: #EEF5FF;"
                    " border-radius: 6px;"
                    "}"
                );
                connect(button, &QToolButton::clicked, &menu, [action, &menu]() {
                    action->trigger();
                    menu.close();
                });
                action->setDefaultWidget(button);
                menu.addAction(action);
                connect(action, &QAction::triggered, this, [this, id]() {
                    if (!maybeSaveCurrentFieldChanges()) {
                        rebuildFieldSidebar();
                        return;
                    }
                    document_.ensureDifficulty(id);
                    documentDirty_ = true;
                    activeOutlineKey_ = "chart";
                    updateDirtyState();
                    switchToDifficultyField(id);
                });
            }
            if (!menu.isEmpty()) {
                const QRect rowRect = outlineList_->visualItemRect(current);
                menu.exec(outlineList_->viewport()->mapToGlobal(rowRect.bottomRight()));
            }
            rebuildFieldSidebar();
            return;
        }
        if (SimaiDocument::isDifficultyId(difficultyId)) {
            activeOutlineKey_ = "chart";
            if (switchToDifficultyField(difficultyId) && editorWidget_ != nullptr) {
                editorWidget_->setFocus();
            }
        }
    });
    outlineList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outlineList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (outlineList_ == nullptr) {
            return;
        }
        QListWidgetItem* item = outlineList_->itemAt(pos);
        if (item == nullptr) {
            return;
        }
        const int difficultyId = item->data(Qt::UserRole + 1).toInt();
        if (!SimaiDocument::isDifficultyId(difficultyId) || document_.difficulty(difficultyId) == nullptr) {
            return;
        }
        QMenu menu(this);
        menu.setFont(uiAccentFont(10));
        styleRoundedMenu(menu);
        QAction* deleteAction = menu.addAction(
            makeOutlineCloseIcon(QColor("#5D6876")),
            QString("Delete %1").arg(SimaiDocument::difficultyName(difficultyId))
        );
        connect(deleteAction, &QAction::triggered, this, [this, difficultyId]() {
            deleteDifficultyField(difficultyId);
        });
        menu.exec(outlineList_->viewport()->mapToGlobal(pos));
    });
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    outlineDock->setMinimumWidth(210);
    outlineDock->setMaximumWidth(210);
    logStartupStage("outline_ready");

    previewPanel_ = new QWidget(this);
    previewPanel_->setObjectName("PreviewPanel");
    previewPanel_->setStyleSheet(
        "QWidget#PreviewPanel {"
        " background: #F5F7FA;"
        " border-left: 1px solid #DEE4EC;"
        "}"
        "QFrame#PreviewCanvasFrame {"
        " background: #000000;"
        " border: 1px solid #D8E0EA;"
        "}"
        "QFrame#PreviewControlCard, QFrame#PreviewStatsCard {"
        " background: #EDF2F8;"
        " border: 1px solid #D5E0EC;"
        " border-radius: 10px;"
        "}"
        "QFrame#PreviewControls {"
        " background: transparent;"
        " border: none;"
        "}"
        "QFrame#PreviewStats {"
        " background: transparent;"
        " border: none;"
        "}"
        "QLabel#PreviewStatChip {"
        " color: #213246;"
        " background: #F6F9FD;"
        " border: 1px solid #D3DEEA;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 600;"
        "}"
        "QLabel#PreviewStatChipTotal {"
        " color: #213246;"
        " background: #F0F4FA;"
        " border: 1px solid #CBD8E6;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 700;"
        "}"
        "QToolButton#PreviewControlButton {"
        " color: #223042;"
        " padding: 5px 8px;"
        " min-height: 28px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " background: transparent;"
        " font-weight: 600;"
        "}"
        "QToolButton#PreviewControlButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        "QToolButton#PreviewControlButton:pressed { background: #E8F1FB; }"
        "QSlider::groove:horizontal {"
        " height: 6px;"
        " background: #D8E0EA;"
        " border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal {"
        " background: #2E77D0;"
        " border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        " width: 12px;"
        " margin: -4px 0;"
        " border-radius: 6px;"
        " background: #FFFFFF;"
        " border: 1px solid #AFC0D6;"
        "}"
    );
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewCanvas();
    logStartupStage("preview_canvas_created");
    previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
    logStartupStage("preview_skin_async_dispatched");
    previewCanvasFrame_ = new QFrame(previewPanel_);
    previewCanvasFrame_->setObjectName("PreviewCanvasFrame");
    previewCanvasFrame_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_ = QWidget::createWindowContainer(previewCanvas_, previewCanvasFrame_);
    previewCanvasContainer_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_->hide();
    logStartupStage("preview_canvas_container_ready");

    previewControlCard_ = new QFrame(previewPanel_);
    previewControlCard_->setObjectName("PreviewControlCard");
    previewControlCard_->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewControlCard_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* previewControlCardLayout = new QVBoxLayout(previewControlCard_);
    previewControlCardLayout->setContentsMargins(8, 8, 8, 8);
    previewControlCardLayout->setSpacing(0);

    auto* previewControls = new QFrame(previewControlCard_);
    previewControls->setObjectName("PreviewControls");
    auto* previewControlsLayout = new QHBoxLayout(previewControls);
    previewControlsLayout->setContentsMargins(0, 0, 0, 0);
    previewControlsLayout->setSpacing(8);

    stopPreviewButton_ = new QToolButton(previewControls);
    stopPreviewButton_->setObjectName("PreviewControlButton");
    stopPreviewButton_->setDefaultAction(stopPreviewAction_);
    stopPreviewButton_->setIconSize(QSize(18, 18));
    stopPreviewButton_->setAutoRaise(false);
    stopPreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopPreviewButton_->setToolTip(QString());
    previewControlsLayout->addWidget(stopPreviewButton_, 0);

    pausePreviewButton_ = new QToolButton(previewControls);
    pausePreviewButton_->setObjectName("PreviewControlButton");
    pausePreviewButton_->setDefaultAction(pausePreviewAction_);
    pausePreviewButton_->setIconSize(QSize(18, 18));
    pausePreviewButton_->setAutoRaise(false);
    pausePreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pausePreviewButton_->setToolTip(QString());
    previewControlsLayout->addWidget(pausePreviewButton_, 0);

    previewSlider_ = new QSlider(Qt::Horizontal, previewControls);
    previewSlider_->setRange(0, 1000);
    previewSlider_->setSingleStep(25);
    previewSlider_->setPageStep(250);
    previewSlider_->setTracking(true);
    previewControlsLayout->addWidget(previewSlider_, 1);

    previewSpeedButton_ = new QToolButton(previewControls);
    previewSpeedButton_->setObjectName("PreviewControlButton");
    previewSpeedButton_->setPopupMode(QToolButton::InstantPopup);
    previewSpeedButton_->setText("1x");
    previewSpeedButton_->setFont(uiAccentFont(10));
    previewSpeedButton_->setFixedWidth(72);
    auto* speedMenu = new QMenu(previewSpeedButton_);
    speedMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*speedMenu);
    const QList<QPair<double, QString>> speedOptions{
        {0.25, "0.25x"},
        {0.5, "0.5x"},
        {0.75, "0.75x"},
        {1.0, "1x"},
        {1.25, "1.25x"},
        {2.0, "2x"},
    };
    for (const auto& speedOption : speedOptions) {
        const double speed = speedOption.first;
        QAction* speedAction = speedMenu->addAction(speedOption.second);
        speedAction->setCheckable(true);
        speedAction->setChecked(qFuzzyCompare(speed + 1.0, 2.0));
        connect(speedAction, &QAction::triggered, this, [this, speed, speedMenu]() {
            const QList<QAction*> actions = speedMenu->actions();
            for (QAction* action : actions) {
                action->setChecked(false);
            }
            QAction* action = qobject_cast<QAction*>(sender());
            if (action != nullptr) {
                action->setChecked(true);
            }
            applyPreviewPlaybackRate(speed);
        });
    }
    previewSpeedButton_->setMenu(speedMenu);
    previewControlsLayout->addWidget(previewSpeedButton_, 0);
    previewControlCardLayout->addWidget(previewControls, 0);

    auto* previewStatsCard = new QFrame(previewPanel_);
    previewStatsCard_ = previewStatsCard;
    previewStatsCard->setObjectName("PreviewStatsCard");
    previewStatsCard->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewStatsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* previewStatsCardLayout = new QVBoxLayout(previewStatsCard);
    previewStatsCardLayout->setContentsMargins(8, 8, 8, 8);
    previewStatsCardLayout->setSpacing(0);

    auto* previewStats = new QFrame(previewStatsCard);
    previewStats->setObjectName("PreviewStats");
    auto* previewStatsLayout = new QGridLayout(previewStats);
    previewStatsGridLayout_ = previewStatsLayout;
    previewStatsLayout->setContentsMargins(2, 2, 2, 2);
    previewStatsLayout->setHorizontalSpacing(10);
    previewStatsLayout->setVerticalSpacing(6);

    const auto addStatsChip = [previewStats, previewStatsLayout](const QString& labelText) -> QLabel* {
        auto* label = new QLabel(labelText, previewStats);
        label->setObjectName("PreviewStatChip");
        label->setFont(uiMonoFont(10, QFont::DemiBold));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setFixedHeight(30);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewStatsLayout->addWidget(label);
        return label;
    };

    previewTapStatsLabel_ = addStatsChip("Tap    0/0");
    previewHoldStatsLabel_ = addStatsChip("Hold   0/0");
    previewSlideStatsLabel_ = addStatsChip("Slide  0/0");
    previewTouchStatsLabel_ = addStatsChip("Touch  0/0");
    previewBreakStatsLabel_ = addStatsChip("Break  0/0");
    previewTotalStatsLabel_ = addStatsChip("Total  0/0");
    previewTotalStatsLabel_->setObjectName("PreviewStatChipTotal");
    previewStatsChips_.clear();
    previewStatsChips_ << previewTapStatsLabel_
                       << previewHoldStatsLabel_
                       << previewSlideStatsLabel_
                       << previewTouchStatsLabel_
                       << previewBreakStatsLabel_
                       << previewTotalStatsLabel_;
    previewStatsCardLayout->addWidget(previewStats, 0);
    previewStatsCardLayout->addStretch(1);
    updatePreviewStatsLayoutMode();
    logStartupStage("preview_controls_and_stats_ready");

    previewSfxRuntime_ = new QtPreviewSfxRuntime(this);
    logStartupStage("preview_sfx_runtime_created");
    connect(previewCanvas_, &QOpenGLWindow::frameSwapped, this, [this]() {
        if (!qtPreviewPlaying_ || legacyPygamePreviewEnabled_ || !qtPreviewAwaitingFrameSwap_) {
            return;
        }
        qtPreviewAwaitingFrameSwap_ = false;
        qtPreviewAwaitingFrameSwapSinceMs_ = -1;
        QTimer::singleShot(0, this, [this]() {
            if (!qtPreviewPlaying_ || legacyPygamePreviewEnabled_) {
                return;
            }
            onQtPreviewTick();
        });
    });
    logStartupStage("preview_runtime_connections_ready");
    logStartupStage("preview_runtime_ready");

    bottomTabs_ = new QTabWidget(central);
    timelineView_ = new TimelineView(bottomTabs_);
    timelineView_->setShowSlideTracks(true);
    connect(timelineView_, &TimelineView::noteNavigateRequested, this, [this](int line, int col) {
        jumpToLocation(line, col);
        statusBar()->showMessage(QString("Timeline jump: nearest object -> L%1 C%2").arg(line).arg(col));
    });
    bottomTabs_->addTab(timelineView_, uiText("tab.timeline", "Timeline"));

    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QPlainTextEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        scheduleTimelineRefresh();
        updateEditorEmptyState();
        updateEditorStatus();
    });
    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        updateEditorStatus();
    });
    connect(titleEdit_, &QLineEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        updateWindowTitle();
    });
    connect(artistEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(firstEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(designerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(metadataExtraEdit_, &QPlainTextEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(difficultyLevelEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(difficultyDesignerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);

    outputView_ = nullptr;

    errorList_ = new QListWidget(bottomTabs_);
    errorList_->setFont(uiOutputFont());
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    bottomTabs_->addTab(errorList_, uiText("tab.validation_errors", "Validation Errors"));
    bottomTabs_->setMinimumHeight(220);
    bottomTabs_->setMaximumHeight(280);
    logStartupStage("timeline_and_tabs_ready");

    previewLeftColumn_ = new QWidget(this);
    previewLeftColumn_->setMinimumWidth(320);
    previewLeftColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* leftColumnLayout = new QVBoxLayout(previewLeftColumn_);
    leftColumnLayout->setContentsMargins(0, 0, 0, 0);
    leftColumnLayout->setSpacing(0);
    leftColumnLayout->addWidget(central, 1);
    leftColumnLayout->addWidget(bottomTabs_, 0);

    workspaceSplitter_ = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(0);
    workspaceSplitter_->addWidget(previewLeftColumn_);
    workspaceSplitter_->addWidget(previewPanel_);
    workspaceSplitter_->setStretchFactor(0, 1);
    workspaceSplitter_->setStretchFactor(1, 0);
    if (QSplitterHandle* handle = workspaceSplitter_->handle(1); handle != nullptr) {
        handle->setEnabled(false);
        handle->hide();
    }
    setCentralWidget(workspaceSplitter_);
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    settingsPlaceholderAction_ = toolBar->addAction(
        makeSettingsGearIcon(QColor("#5D6E83")),
        uiText("action.preferences", "Preferences...")
    );
    settingsPlaceholderAction_->setToolTip(uiText("action.preferences", "Preferences..."));
    connect(settingsPlaceholderAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();

    metadataRefreshTimer_ = new QTimer(this);
    metadataRefreshTimer_->setSingleShot(true);
    metadataRefreshTimer_->setInterval(0);
    connect(metadataRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshTimelineMetadata);

    qtPreviewTimer_ = new QTimer(this);
    qtPreviewTimer_->setInterval(16);
    qtPreviewTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimer_, &QTimer::timeout, this, [this]() {
        if (!qtPreviewPlaying_) {
            return;
        }
        if (previewCanvas_ == nullptr || legacyPygamePreviewEnabled_) {
            onQtPreviewTick();
            return;
        }
        if (!qtPreviewAwaitingFrameSwap_) {
            return;
        }
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        if (qtPreviewAwaitingFrameSwapSinceMs_ >= 0 && nowMs - qtPreviewAwaitingFrameSwapSinceMs_ >= 40) {
            qtPreviewAwaitingFrameSwap_ = false;
            qtPreviewAwaitingFrameSwapSinceMs_ = -1;
            onQtPreviewTick();
            return;
        }
        previewCanvas_->update();
    });

    qtPreviewTimelineTimer_ = new QTimer(this);
    qtPreviewTimelineTimer_->setInterval(16);
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QTimer::timeout, this, &MainWindow::flushQtPreviewTimelinePosition);

    previewSeekDebounceTimer_ = new QTimer(this);
    previewSeekDebounceTimer_->setSingleShot(true);
    previewSeekDebounceTimer_->setInterval(120);
    connect(previewSeekDebounceTimer_, &QTimer::timeout, this, [this]() {
        seekPreviewToSecond(previewPendingSeekSecond_, previewPendingSeekCenterView_);
    });
    logStartupStage("timers_ready");

    if (previewSlider_ != nullptr) {
        previewSlider_->setFocusPolicy(Qt::StrongFocus);
        previewSlider_->installEventFilter(this);
        connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
            if (qtPreviewPlaying_) {
                stopQtPreviewPlayback(true);
            }
            previewSliderDragging_ = true;
            if (previewSlider_ != nullptr) {
                showPreviewSliderTimeHint(previewSlider_->value());
            }
        });
        connect(previewSlider_, &QSlider::sliderMoved, this, [this](int value) {
            if (previewSlider_ == nullptr) {
                return;
            }
            showPreviewSliderTimeHint(value);
            schedulePreviewSeek(static_cast<double>(value) / 1000.0, true);
        });
        connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
            previewSliderDragging_ = false;
            if (previewSlider_ == nullptr) {
                return;
            }
            showPreviewSliderTimeHint(previewSlider_->value());
            schedulePreviewSeek(static_cast<double>(previewSlider_->value()) / 1000.0, true);
        });
    }

    editorViewport_ = qobject_cast<PlainCodeEditor*>(editorWidget_)->viewport();
    if (editorViewport_ != nullptr) {
        editorViewport_->installEventFilter(this);
    }

    statusBar()->showMessage("PlainCodeEditor ready.");

    loadPortableState();
    logStartupStage("portable_state_loaded");
    if (runtimeDebugOutputEnabled_) {
        previewShowDebugInfo_ = true;
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPath(lastTrackPath_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
        logStartupStage("preview_sfx_set_chart_path_done");
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        logStartupStage("preview_sfx_set_playback_rate_done");
    }
    if (toggleJudgeMarkersAction_ != nullptr) {
        toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    }
    if (toggleTouchTrailAction_ != nullptr) {
        toggleTouchTrailAction_->setChecked(showTouchTrail_);
    }
    if (timelineView_ != nullptr) {
        timelineView_->setShowSlideTracks(true);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
    }
    updatePauseButtonAppearance();
    loadDocument(SimaiDocument::createEmpty());
    logStartupStage("initial_empty_document_applied");
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
    logStartupStage("initial_document_loaded");
    qtPreviewWatchdogElapsed_.start();
    if (legacyPygamePreviewEnabled_) {
        appendOutput("preview/bootstrap", "initializing resident preview session");
        bootstrapPreviewWindow();
        QTimer::singleShot(1500, this, [this]() {
            if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
                appendOutput("preview/bootstrap", "startup retry");
                bootstrapPreviewWindow();
            }
        });
    } else {
        appendOutput("preview/bootstrap", "legacy pygame preview disabled by default");
    }
    logStartupStage("preview_media_controller_lazy_init_deferred");
    QTimer::singleShot(0, this, [this]() {
        schedulePreviewSubsystemWarmup();
    });
    logStartupStage("preview_subsystem_warmup_scheduled");
    logStartupStage("constructor_done");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveBeforeContinue()) {
        savePortableState();
        stopPreviewSession();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (outlineList_ != nullptr && watched == outlineList_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QListWidgetItem* hoveredItem = outlineList_->itemAt(mouseEvent->pos());
            const bool showButton =
                hoveredItem != nullptr
                && hoveredItem == outlineList_->currentItem()
                && SimaiDocument::isDifficultyId(hoveredItem->data(Qt::UserRole + 1).toInt());
            updateDifficultyDeleteButton(showButton);
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Wheel) {
            updateDifficultyDeleteButton(false);
        } else if (event->type() == QEvent::Resize && deleteDifficultyButton_ != nullptr && deleteDifficultyButton_->isVisible()) {
            updateDifficultyDeleteButton(true);
        }
    }
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QStyleOptionSlider option;
                option.initFrom(previewSlider_);
                option.subControls = QStyle::SC_SliderHandle;
                option.orientation = previewSlider_->orientation();
                option.minimum = previewSlider_->minimum();
                option.maximum = previewSlider_->maximum();
                option.sliderPosition = previewSlider_->sliderPosition();
                option.sliderValue = previewSlider_->value();
                option.upsideDown = false;
                const QRect handleRect = previewSlider_->style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    previewSlider_
                );
                if (!handleRect.contains(mouseEvent->pos())) {
                    const int value = QStyle::sliderValueFromPosition(
                        previewSlider_->minimum(),
                        previewSlider_->maximum(),
                        mouseEvent->pos().x(),
                        qMax(1, previewSlider_->width()),
                        false
                    );
                    previewSlider_->setValue(value);
                    showPreviewSliderTimeHint(value);
                    seekPreviewToSecond(static_cast<double>(value) / 1000.0, true);
                    return true;
                }
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int deltaMs = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                deltaMs = -16;
            } else if (keyEvent->key() == Qt::Key_Right) {
                deltaMs = 16;
            }
            if (deltaMs != 0) {
                const int value = qBound(
                    previewSlider_->minimum(),
                    previewSlider_->value() + deltaMs,
                    previewSlider_->maximum()
                );
                previewSlider_->setValue(value);
                showPreviewSliderTimeHint(value);
                seekPreviewToSecond(static_cast<double>(value) / 1000.0, true);
                return true;
            }
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && (mouseEvent->modifiers() & Qt::ControlModifier)) {
            int line = 1;
            int col = 1;

            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            QTextCursor cursor = editor->cursorForPosition(mouseEvent->pos());
            editor->setTextCursor(cursor);
            line = cursor.blockNumber() + 1;
            col = cursor.positionInBlock() + 1;
            const double second = timelineSecondForCursor(line, col);
            if (second >= 0.0) {
                if (qtPreviewPlaying_) {
                    stopQtPreviewPlayback(true);
                }
                schedulePreviewSeek(second, true);
            } else {
                seekTimelineToCursor(line, col);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewWorkspaceLayout();
    updateEditorHeaderLayoutMode();
}

bool MainWindow::maybeSaveBeforeContinue()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (!documentDirty_) {
        return true;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        "Unsaved Changes",
        "Current document has unsaved changes. Save before continue?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    if (choice == QMessageBox::Save) {
        return onSaveFile();
    }
    return choice == QMessageBox::Discard;
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    if (!currentFieldDirty_) {
        return true;
    }

    const QString fieldName = hasActiveDifficulty()
        ? SimaiDocument::difficultyName(activeDifficultyId_)
        : QString("Metadata");
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        "Unsaved Field Changes",
        QString("%1 has unsaved changes. Save before switch?").arg(fieldName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    if (choice == QMessageBox::Save) {
        return applyCurrentFieldToDocument();
    }
    if (choice == QMessageBox::Discard) {
        currentFieldDirty_ = false;
        updateDirtyState();
        return true;
    }
    return false;
}

bool MainWindow::applyCurrentFieldToDocument()
{
    bool changed = false;
    bool metadataTimingChanged = false;
    if (hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = document_.ensureDifficulty(activeDifficultyId_);
        const QString newLevel = difficultyLevelEdit_ != nullptr ? difficultyLevelEdit_->text() : QString();
        const QString newDesigner = difficultyDesignerEdit_ != nullptr ? difficultyDesignerEdit_->text() : QString();
        const QString newChart = editorText();
        if (difficultyData.level != newLevel || difficultyData.designer != newDesigner || difficultyData.chart != newChart) {
            difficultyData.level = newLevel;
            difficultyData.designer = newDesigner;
            difficultyData.chart = newChart;
            changed = true;
        }
    } else {
        const QString newTitle = titleEdit_ != nullptr ? titleEdit_->text() : QString();
        const QString newArtist = artistEdit_ != nullptr ? artistEdit_->text() : QString();
        const QString newFirst = firstEdit_ != nullptr ? firstEdit_->text() : QString();
        const QString newDesigner = designerEdit_ != nullptr ? designerEdit_->text() : QString();
        const QVector<SimaiRawField> newExtraFields = SimaiDocument::parseRawFields(
            metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
            true
        );
        if (document_.title != newTitle
            || document_.artist != newArtist
            || document_.first != newFirst
            || document_.designer != newDesigner
            || document_.extraFields != newExtraFields) {
            metadataTimingChanged = (document_.first != newFirst);
            document_.title = newTitle;
            document_.artist = newArtist;
            document_.first = newFirst;
            document_.designer = newDesigner;
            document_.extraFields = newExtraFields;
            changed = true;
        }
    }

    currentFieldDirty_ = false;
    if (changed) {
        documentDirty_ = true;
    }
    updateDirtyState();
    updateWindowTitle();
    rebuildFieldSidebar();
    if (metadataTimingChanged) {
        refreshWaveformCache();
    }
    return true;
}

void MainWindow::onNewFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }
    loadDocument(SimaiDocument::createEmpty());
    clearValidationErrors();
    clearValidationDecorations();
    currentEncoding_ = TextEncoding::Utf8;
    setCurrentFilePath(QString());
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setChartPath(QString());
    }
    statusBar()->showMessage("New file.");
}

void MainWindow::onOpenFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open simai file",
        resolveInitialOpenDirectory(),
        "Simai (*.txt *.simai);;All Files (*.*)"
    );
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Open Failed", "Cannot open file:\n" + path);
        return;
    }

    const QByteArray bytes = file.readAll();
    QString text;
    TextEncoding encodingUsed = TextEncoding::Utf8;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            encodingUsed = TextEncoding::System;
        }
    }

    loadDocument(SimaiDocument::fromText(text));
    clearValidationErrors();
    clearValidationDecorations();
    currentEncoding_ = encodingUsed;
    setCurrentFilePath(path);
    statusBar()->showMessage(
        QString("Opened: %1 (%2)")
            .arg(QFileInfo(path).fileName())
            .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
    );
}

bool MainWindow::onSaveFile()
{
    if (currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(currentFilePath_);
}

bool MainWindow::onSaveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save simai file",
        currentFilePath_.isEmpty() ? QString("chart.txt") : currentFilePath_,
        "Simai (*.txt *.simai);;All Files (*.*)"
    );
    if (path.isEmpty()) {
        return false;
    }
    return saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path)
{
    if (!applyCurrentFieldToDocument()) {
        return false;
    }
    bool firstOk = false;
    (void)parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        QMessageBox::critical(this, "Save Failed", "&first must be a valid number of seconds.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Save Failed", "Cannot write file:\n" + path);
        return false;
    }
    QByteArray data;
    const QString serialized = document_.toText();
    if (currentEncoding_ == TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(serialized);
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(serialized);
    }
    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, "Save Failed", "Write failed:\n" + path);
        return false;
    }
    setCurrentFilePath(path);
    documentDirty_ = false;
    currentFieldDirty_ = false;
    updateDirtyState();
    statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    return true;
}

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    const QString original = editorText();
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(transformed);
    cursor.endEditBlock();

    markCurrentFieldDirty();
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        return applyBatchTransform(opName, transform);
    }

    const QString original = editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    int changed = 0;
    const QString transformed = transform(selected, &changed);
    if (transformed == selected) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(begin);
    cursor.setPosition(finish, QTextCursor::KeepAnchor);
    cursor.insertText(transformed);
    cursor.setPosition(begin);
    cursor.setPosition(begin + transformed.size(), QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    cursor.endEditBlock();

    markCurrentFieldDirty();
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    if (startPos == nullptr || endPos == nullptr) {
        return false;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }
    *startPos = cursor.selectionStart();
    *endPos = cursor.selectionEnd();
    return *endPos > *startPos;
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return currentCursorLineCol();
    }
    cursor.setPosition(cursor.selectionStart());
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    if (metadataExtraEdit_ == nullptr) {
        return;
    }
    QSignalBlocker blocker(metadataExtraEdit_);
    metadataExtraEdit_->setPlainText(text);
    metadataExtraEdit_->document()->clearUndoRedoStacks();
}

void MainWindow::setEditorText(const QString& text)
{
    QSignalBlocker blocker(editorWidget_);
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    editor->setPlainText(text);
    editor->document()->clearUndoRedoStacks();
}

void MainWindow::updatePauseButtonAppearance()
{
    if (pausePreviewAction_ == nullptr) {
        return;
    }
    if (qtPreviewPlaying_) {
        pausePreviewAction_->setIcon(makePreviewPauseIcon(QColor("#2B3C4E")));
        pausePreviewAction_->setText(uiText("preview.pause", "Pause"));
    } else {
        pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
        pausePreviewAction_->setText(uiText("preview.play", "Play"));
    }
    if (pausePreviewButton_ != nullptr) {
        pausePreviewButton_->setText(
            qtPreviewPlaying_
                ? uiText("preview.pause", "Pause")
                : uiText("preview.play", "Play")
        );
        pausePreviewButton_->setStyleSheet(
            qtPreviewPlaying_
                ? "QToolButton { color: #FFFFFF; padding: 5px 8px; min-height: 28px; border: 1px solid #2E77D0; border-radius: 6px; background: #2E77D0; font-weight: 600; }"
                  "QToolButton:hover { background: #3A86E8; }"
                : "QToolButton { color: #223042; padding: 5px 8px; min-height: 28px; border: 1px solid #D8E0EA; border-radius: 6px; background: transparent; font-weight: 600; }"
                  "QToolButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        );
    }
}

void MainWindow::updateDirtyState()
{
    setWindowModified(documentDirty_ || currentFieldDirty_);
}

void MainWindow::markCurrentFieldDirty()
{
    currentFieldDirty_ = true;
    updateDirtyState();
}

void MainWindow::updateEditorHeader()
{
    if (editorContextLabel_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        if (document_.difficultyIds().isEmpty()) {
            editorContextLabel_->setText(uiText("editor.welcome", "Welcome to MiaCode!"));
            editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
        } else {
            editorContextLabel_->setText(uiText("editor.metadata", "Metadata"));
            editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        }
        editorContextLabel_->setStyleSheet(QString());
        if (editorDifficultyControls_ != nullptr) {
            editorDifficultyControls_->hide();
        }
        if (editorBatchTransformControls_ != nullptr) {
            editorBatchTransformControls_->hide();
        }
        updateDifficultyDeleteButton(false);
        editorContextLabel_->setMinimumWidth(0);
        updateEditorHeaderLayoutMode();
        return;
    }
    editorContextLabel_->setText(SimaiDocument::difficultyShortName(activeDifficultyId_));
    editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
    editorContextLabel_->setStyleSheet(QString());
    editorContextLabel_->setMinimumWidth(QFontMetrics(editorContextLabel_->font()).horizontalAdvance(editorContextLabel_->text()) + 8);
    if (editorDifficultyControls_ != nullptr) {
        editorDifficultyControls_->show();
    }
    if (editorBatchTransformControls_ != nullptr) {
        editorBatchTransformControls_->show();
    }
    updateDifficultyDeleteButton(false);
    updateEditorHeaderLayoutMode();
}

void MainWindow::updateEditorHeaderLayoutMode()
{
    if (editorHeaderWidget_ == nullptr || editorCursorLabel_ == nullptr || editorContextLabel_ == nullptr) {
        return;
    }

    if (!hasActiveDifficulty()) {
        editorCursorLabel_->setVisible(false);
        return;
    }

    const int headerWidth = editorHeaderWidget_->contentsRect().width();
    const int contextWidth = editorContextLabel_->minimumWidth();
    const int controlsWidth =
        (editorDifficultyControls_ != nullptr && editorDifficultyControls_->isVisible())
        ? editorDifficultyControls_->sizeHint().width()
        : 0;
    const int cursorWidth = editorCursorLabel_->sizeHint().width();
    const int requiredWithCursor = contextWidth + controlsWidth + cursorWidth + 84;
    editorCursorLabel_->setVisible(headerWidth >= requiredWithCursor);
}

void MainWindow::updateEditorStatus()
{
    if (editorCursorLabel_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        editorCursorLabel_->clear();
        updateEditorHeaderLayoutMode();
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    editorCursorLabel_->setText(QString("Ln %1, Col %2").arg(line).arg(col));
    updateEditorHeaderLayoutMode();
}

void MainWindow::updateEditorEmptyState()
{
    if (editorEmptyStateLabel_ != nullptr) {
        editorEmptyStateLabel_->hide();
    }
}

void MainWindow::updateMetadataPageMode()
{
    if (metadataCard_ == nullptr || metadataEmptyHintLabel_ == nullptr) {
        return;
    }
    const bool hasAnyDifficulty = !document_.difficultyIds().isEmpty();
    metadataCard_->setVisible(hasAnyDifficulty);
    metadataEmptyHintLabel_->setVisible(!hasAnyDifficulty);
}

bool MainWindow::deleteDifficultyField(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = document_.difficulty(difficultyId);
    if (!SimaiDocument::isDifficultyId(difficultyId) || difficultyData == nullptr) {
        return false;
    }

    const bool deletingActiveDifficulty = (difficultyId == activeDifficultyId_);
    const QString difficultyName = SimaiDocument::difficultyName(difficultyId);
    const QString currentLevel =
        deletingActiveDifficulty && difficultyLevelEdit_ != nullptr ? difficultyLevelEdit_->text() : difficultyData->level;
    const QString currentDesigner =
        deletingActiveDifficulty && difficultyDesignerEdit_ != nullptr ? difficultyDesignerEdit_->text() : difficultyData->designer;
    const QString currentChart = deletingActiveDifficulty ? editorText() : difficultyData->chart;
    const bool emptyDifficulty = currentLevel.trimmed().isEmpty()
        && currentDesigner.trimmed().isEmpty()
        && currentChart.trimmed().isEmpty();

    if (!emptyDifficulty) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            "Delete Difficulty",
            QString("Delete %1?").arg(difficultyName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return false;
        }
    }

    stopQtPreviewPlayback(true);
    document_.removeDifficulty(difficultyId);
    documentDirty_ = true;

    if (deletingActiveDifficulty) {
        currentFieldDirty_ = false;
        const QVector<int> remainingIds = document_.difficultyIds();
        if (remainingIds.isEmpty()) {
            activeDifficultyId_ = 0;
            activeOutlineKey_ = "metadata";
            populateMetadataPage();
            if (editorStack_ != nullptr && metadataPage_ != nullptr) {
                editorStack_->setCurrentWidget(metadataPage_);
            }
            clearTimelineAndPreview();
            if (titleEdit_ != nullptr) {
                titleEdit_->setFocus();
            }
        } else {
            int fallbackId = remainingIds.constFirst();
            int bestDistance = qAbs(fallbackId - difficultyId);
            for (int id : remainingIds) {
                const int distance = qAbs(id - difficultyId);
                if (distance < bestDistance || (distance == bestDistance && id < fallbackId)) {
                    fallbackId = id;
                    bestDistance = distance;
                }
            }
            activeOutlineKey_ = "chart";
            switchToDifficultyField(fallbackId);
        }
    }

    rebuildFieldSidebar();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
    updateDirtyState();
    if (currentFilePath_.isEmpty()) {
        statusBar()->showMessage(QString("Deleted %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(currentFilePath_)) {
        statusBar()->showMessage(QString("Deleted %1. Changes are still unsaved.").arg(difficultyName));
    }
    return true;
}

void MainWindow::updateDifficultyDeleteButton(bool visible)
{
    if (deleteDifficultyButton_ == nullptr) {
        return;
    }
    if (!visible || !hasActiveDifficulty() || outlineList_ == nullptr) {
        deleteDifficultyButton_->hide();
        return;
    }
    QListWidgetItem* currentItem = outlineList_->currentItem();
    if (currentItem == nullptr || !SimaiDocument::isDifficultyId(currentItem->data(Qt::UserRole + 1).toInt())) {
        deleteDifficultyButton_->hide();
        return;
    }
    const QRect rowRect = outlineList_->visualItemRect(currentItem);
    if (!rowRect.isValid() || rowRect.isEmpty()) {
        deleteDifficultyButton_->hide();
        return;
    }
    const int x = rowRect.right() - deleteDifficultyButton_->width() - 8;
    const int y = rowRect.top() + (rowRect.height() - deleteDifficultyButton_->height()) / 2;
    deleteDifficultyButton_->move(x, y);
    deleteDifficultyButton_->raise();
    deleteDifficultyButton_->show();
}

void MainWindow::rebuildFieldSidebar()
{
    if (outlineList_ == nullptr) {
        return;
    }
    updateDifficultyDeleteButton(false);
    QSignalBlocker blocker(outlineList_);
    outlineList_->clear();
    auto* metadataItem = new QListWidgetItem(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        uiText("sidebar.metadata", "Metadata"),
        outlineList_
    );
    metadataItem->setData(Qt::UserRole, "metadata");

    QListWidgetItem* selectedItem = metadataItem;
    bool hasMissingDifficulty = false;
    const QVector<int> ids = document_.difficultyIds();
    for (int id : ids) {
        auto* difficultyItem = new QListWidgetItem(SimaiDocument::difficultyName(id), outlineList_);
        difficultyItem->setIcon(makeDifficultyBadgeIcon(id));
        difficultyItem->setData(Qt::UserRole, "difficulty_chart");
        difficultyItem->setData(Qt::UserRole + 1, id);
        if (id == activeDifficultyId_) {
            selectedItem = difficultyItem;
        }
    }
    for (int id = 1; id <= 7; ++id) {
        if (document_.difficulty(id) == nullptr) {
            hasMissingDifficulty = true;
            break;
        }
    }
    if (hasMissingDifficulty) {
        auto* addItem = new QListWidgetItem(
            style()->standardIcon(QStyle::SP_FileDialogNewFolder),
            uiText("sidebar.add_difficulty", "+ Add Difficulty"),
            outlineList_
        );
        addItem->setData(Qt::UserRole, "add");
    }
    if (!hasActiveDifficulty()) {
        selectedItem = metadataItem;
    }
    if (selectedItem != nullptr) {
        outlineList_->setCurrentItem(selectedItem);
    }
}

void MainWindow::populateMetadataPage()
{
    if (titleEdit_ == nullptr || artistEdit_ == nullptr || firstEdit_ == nullptr || designerEdit_ == nullptr) {
        return;
    }
    QSignalBlocker blockerTitle(titleEdit_);
    QSignalBlocker blockerArtist(artistEdit_);
    QSignalBlocker blockerFirst(firstEdit_);
    QSignalBlocker blockerDesigner(designerEdit_);
    titleEdit_->setText(document_.title);
    artistEdit_->setText(document_.artist);
    firstEdit_->setText(document_.first);
    designerEdit_->setText(document_.designer);
    setMetadataExtraText(SimaiDocument::serializeRawFields(document_.extraFields));
    updateMetadataPageMode();
    updateEditorHeader();
}

void MainWindow::populateDifficultyPage(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = document_.difficulty(difficultyId);
    if (difficultyData == nullptr) {
        return;
    }
    if (difficultyLevelEdit_ != nullptr) {
        QSignalBlocker blocker(difficultyLevelEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(difficultyLevelEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&lv_%1=").arg(difficultyId));
        }
        difficultyLevelEdit_->setText(difficultyData->level);
    }
    if (difficultyDesignerEdit_ != nullptr) {
        QSignalBlocker blocker(difficultyDesignerEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(difficultyDesignerEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&des_%1=").arg(difficultyId));
        }
        difficultyDesignerEdit_->setText(difficultyData->designer);
    }
    setEditorText(difficultyData->chart);
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
}

bool MainWindow::switchToMetadataField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    stopQtPreviewPlayback(true);
    activeDifficultyId_ = 0;
    activeOutlineKey_ = "metadata";
    populateMetadataPage();
    if (editorStack_ != nullptr && metadataPage_ != nullptr) {
        editorStack_->setCurrentWidget(metadataPage_);
    }
    if (bottomTabs_ != nullptr && timelineView_ != nullptr) {
        const int timelineTabIndex = bottomTabs_->indexOf(timelineView_);
        if (timelineTabIndex >= 0) {
            bottomTabs_->setTabVisible(timelineTabIndex, false);
        }
        if (errorList_ != nullptr) {
            const int errorTabIndex = bottomTabs_->indexOf(errorList_);
            if (errorTabIndex >= 0) {
                bottomTabs_->setCurrentIndex(errorTabIndex);
            }
        }
    }
    updateMetadataPageMode();
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    return true;
}

bool MainWindow::switchToDifficultyField(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId) || document_.difficulty(difficultyId) == nullptr) {
        return false;
    }
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    stopQtPreviewPlayback(true);
    activeDifficultyId_ = difficultyId;
    if (activeOutlineKey_.isEmpty() || activeOutlineKey_ == "metadata") {
        activeOutlineKey_ = "chart";
    }
    populateDifficultyPage(difficultyId);
    if (editorStack_ != nullptr && chartPage_ != nullptr) {
        editorStack_->setCurrentWidget(chartPage_);
    }
    if (bottomTabs_ != nullptr && timelineView_ != nullptr) {
        const int timelineTabIndex = bottomTabs_->indexOf(timelineView_);
        if (timelineTabIndex >= 0) {
            bottomTabs_->setTabVisible(timelineTabIndex, true);
        }
    }
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
    scheduleTimelineRefresh();
    return true;
}

void MainWindow::activateInitialField()
{
    const QVector<int> ids = document_.difficultyIds();
    if (!ids.isEmpty()) {
        activeOutlineKey_ = "chart";
        const QVector<int> preferredOrder{5, 6, 4, 7, 3, 2, 1};
        int targetId = ids.constFirst();
        for (int id : preferredOrder) {
            if (ids.contains(id)) {
                targetId = id;
                break;
            }
        }
        switchToDifficultyField(targetId);
    } else {
        activeOutlineKey_ = "metadata";
        switchToMetadataField();
        clearTimelineAndPreview();
    }
}

void MainWindow::loadDocument(const SimaiDocument& document)
{
    document_ = document;
    documentDirty_ = false;
    currentFieldDirty_ = false;
    activeDifficultyId_ = 0;
    rebuildFieldSidebar();
    activateInitialField();
    updateMetadataPageMode();
    updateDirtyState();
    updateWindowTitle();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
}

void MainWindow::clearTimelineAndPreview()
{
    timelineCursorNotes_.clear();
    lastPreviewNoteMarkerSignature_.clear();
    clearPreviewObjectStats();
    previewTrackDurationSeconds_ = 0.0;
    qtPreviewTimelineDirty_ = false;
    qtPreviewPendingTimelineSecond_ = 0.0;
    qtPreviewPendingTimelineCenterView_ = true;
    qtPreviewLastTimelineSecond_ = -1.0;
    qtPreviewTimelineStartSecond_ = 0.0;
    qtPreviewTimelineCenterNextTick_ = true;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->clearTimeline();
    }
    stopQtPreviewPlayback(false);
    if (timelineView_ != nullptr) {
        timelineView_->clear();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->reset();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->reset();
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
}

void MainWindow::refreshWaveformCache()
{
    const double firstSeconds = parsedFirstSeconds();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackOffsetSeconds(firstSeconds);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setTimelineOffsetSeconds(firstSeconds);
    }
    if (timelineView_ == nullptr) {
        return;
    }
    double audioDurationSeconds = 0.0;
    const QVector<float> waveform = buildWaveformPeaks(lastTrackPath_, &audioDurationSeconds);
    previewTrackDurationSeconds_ = qMax(0.0, audioDurationSeconds);
    timelineView_->setWaveformData(waveform, -firstSeconds, audioDurationSeconds);
    updatePreviewSliderRange();
}

bool MainWindow::hasActiveDifficulty() const
{
    return activeDifficultyId_ > 0 && document_.difficulty(activeDifficultyId_) != nullptr;
}

int MainWindow::activeDifficultyId() const
{
    return activeDifficultyId_;
}

QString MainWindow::activeChartText() const
{
    if (!hasActiveDifficulty()) {
        return QString();
    }
    if (editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_) {
        return editorText();
    }
    const SimaiDifficultyData* difficultyData = document_.difficulty(activeDifficultyId_);
    return difficultyData != nullptr ? difficultyData->chart : QString();
}

double MainWindow::parsedFirstSeconds(bool* ok) const
{
    QString rawValue = document_.first;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && firstEdit_ != nullptr) {
        rawValue = firstEdit_->text();
    }
    bool localOk = false;
    const double value = rawValue.trimmed().isEmpty() ? 0.0 : rawValue.trimmed().toDouble(&localOk);
    if (ok != nullptr) {
        *ok = rawValue.trimmed().isEmpty() ? true : localOk;
    }
    if (rawValue.trimmed().isEmpty()) {
        return 0.0;
    }
    return localOk ? value : 0.0;
}

double MainWindow::parsedWholeBpm(bool* ok) const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool localOk = false;
        const double value = field.value.trimmed().toDouble(&localOk);
        if (ok != nullptr) {
            *ok = localOk && value > 0.0;
        }
        return (localOk && value > 0.0) ? value : 0.0;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return 0.0;
}

QString MainWindow::parsedLatencyMeterId() const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("meter"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString value = field.value.trimmed();
        if (value == QLatin1String("4/4")
            || value == QLatin1String("3/4")
            || value == QLatin1String("6/8")
            || value == QLatin1String("7/4")
            || value == QLatin1String("auto")) {
            return value;
        }
        return QStringLiteral("auto");
    }
    return QStringLiteral("auto");
}

void MainWindow::applyLatencyDetectorOffset(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    const QString serialized = QString::number(normalized, 'f', 3);
    document_.first = serialized;
    if (firstEdit_ != nullptr) {
        QSignalBlocker blocker(firstEdit_);
        firstEdit_->setText(serialized);
    }
    documentDirty_ = true;
    updateDirtyState();
    refreshWaveformCache();
}

void MainWindow::applyLatencyDetectorBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    const QString serializedBpm = QString::number(bpm, 'f', 3);
    bool foundWholeBpm = false;
    for (SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        field.value = serializedBpm;
        foundWholeBpm = true;
        break;
    }
    if (!foundWholeBpm) {
        fields.append(SimaiRawField{QStringLiteral("wholebpm"), serializedBpm});
    }
    document_.extraFields = fields;
    setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    documentDirty_ = true;
    updateDirtyState();
}

void MainWindow::applyLatencyDetectorMeter(const QString& meterId)
{
    QString normalized = meterId.trimmed();
    if (normalized != QLatin1String("4/4")
        && normalized != QLatin1String("3/4")
        && normalized != QLatin1String("6/8")
        && normalized != QLatin1String("7/4")
        && normalized != QLatin1String("auto")) {
        normalized = QStringLiteral("auto");
    }

    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    bool found = false;
    for (SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("meter"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        field.value = normalized;
        found = true;
        break;
    }
    if (!found) {
        fields.append(SimaiRawField{QStringLiteral("meter"), normalized});
    }
    document_.extraFields = fields;
    setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    documentDirty_ = true;
    updateDirtyState();
}

void MainWindow::setCurrentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool pathChanged = normalizedPath != currentFilePath_;
    if (pathChanged) {
        stopQtPreviewPlayback(false);
        if (latencyDetectorDialog_ != nullptr) {
            latencyDetectorDialog_->close();
            latencyDetectorDialog_.clear();
        }
    }
    currentFilePath_ = normalizedPath;
    if (!currentFilePath_.isEmpty()) {
        setLastOpenDirectory(currentFilePath_);

        const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
        if (QFileInfo::exists(siblingTrack)) {
            // Keep preview audio in sync with the currently opened chart directory.
            lastTrackPath_ = QDir::cleanPath(siblingTrack);
        } else {
            lastTrackPath_.clear();
        }
    } else {
        lastTrackPath_.clear();
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();
    if (pathChanged) {
        loadProjectRenderState();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setChartPath(currentFilePath_);
        previewMediaController_->setBackgroundTrackPath(lastTrackPath_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    applyPreviewAudioSettingsToRuntime();
    refreshWaveformCache();
    if (legacyPygamePreviewEnabled_ && pathChanged && !currentFilePath_.isEmpty()) {
        stopPreviewSession();
        if (ensurePreviewSessionStarted()) {
            sendPreviewPrepareCommand();
        }
    }
}

void MainWindow::updateWindowTitle()
{
    QString titleText = document_.title;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && titleEdit_ != nullptr) {
        titleText = titleEdit_->text();
    }
    if (titleText.trimmed().isEmpty()) {
        titleText = currentFilePath_.isEmpty()
            ? QString("Untitled.simai")
            : QFileInfo(currentFilePath_).fileName();
    }
    const QFontMetrics metrics(font());
    const QString elided = metrics.elidedText(titleText, Qt::ElideRight, 420);
    setWindowTitle(QString("MiaCode - %1[*]").arg(elided));
}

void MainWindow::updateCurrentFileLabel()
{
    if (currentFileLabel_ == nullptr) {
        return;
    }
    if (currentFilePath_.isEmpty()) {
        currentFileLabel_->setText("(unsaved)");
    } else {
        currentFileLabel_->setText(QDir::toNativeSeparators(currentFilePath_));
    }
}

QString MainWindow::editorText() const
{
    return qobject_cast<PlainCodeEditor*>(editorWidget_)->toPlainText();
}

void MainWindow::clearValidationErrors()
{
    errorList_->clear();
}

void MainWindow::clearValidationDecorations()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    editor->setExtraSelections({});
}

void MainWindow::addValidationError(int line, int col, const QString& message)
{
    auto* item = new QListWidgetItem(QString("L%1 C%2  %3").arg(line).arg(col).arg(message), errorList_);
    item->setData(Qt::UserRole, line);
    item->setData(Qt::UserRole + 1, col);
}

void MainWindow::addValidationDecoration(int line, int col, const QString& message)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qMax(0, col - 1));
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection sel;
    sel.cursor = cursor;
    sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    sel.format.setUnderlineColor(QColor("#E74C3C"));
    sel.format.setToolTip(message);

    auto selections = editor->extraSelections();
    selections.append(sel);
    editor->setExtraSelections(selections);
}

void MainWindow::jumpToLocation(int line, int col)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + col - 1);
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    editor->setFocus();
}

QString MainWindow::resolvePreviewSessionScriptPath() const
{
    const QString envPath = qEnvironmentVariable("MIACODE_PREVIEW_SESSION_SCRIPT", qEnvironmentVariable("MAIMURI_PREVIEW_SESSION_SCRIPT"));
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
    }
    return QString();
}

void MainWindow::scheduleTimelineRefresh()
{
    if (metadataRefreshTimer_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        return;
    }
    metadataRefreshTimer_->stop();
    metadataRefreshTimer_->start();
}

void MainWindow::refreshTimelineMetadata()
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(activeChartText());
    QVector<TimelineBeatMarker> beatMarkers = nativeResult.beatMarkers;
    QVector<TimelineNoteMarker> noteMarkers = nativeResult.noteMarkers;
    timelineCursorNotes_.clear();
    timelineCursorNotes_.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        TimelineCursorNote cursorNote;
        cursorNote.line = qMax(1, marker.sourceLine);
        cursorNote.col = qMax(1, marker.sourceCol);
        cursorNote.lane = marker.lane;
        cursorNote.second = marker.second;
        timelineCursorNotes_.append(cursorNote);
    }

    std::sort(timelineCursorNotes_.begin(), timelineCursorNotes_.end(), [](const TimelineCursorNote& a, const TimelineCursorNote& b) {
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return a.second < b.second;
    });

    double durationSeconds = nativeResult.durationSeconds;
    if (durationSeconds <= 0.0) {
        for (const TimelineNoteMarker& marker : noteMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
            if (marker.endSecond > marker.second) {
                durationSeconds = qMax(durationSeconds, marker.endSecond);
            }
        }
        for (const TimelineBeatMarker& marker : beatMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
        }
    }

    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->configureTimeline(noteMarkers);
    }
    refreshPreviewObjectStatsTotals(noteMarkers);

    timelineView_->setTimelineData(beatMarkers, noteMarkers, durationSeconds);
    updatePreviewSliderRange();
    if (previewCanvas_ != nullptr) {
        const QByteArray newSignature = noteMarkerSignature(noteMarkers);
        if (newSignature != lastPreviewNoteMarkerSignature_) {
            previewCanvas_->setNoteMarkers(noteMarkers);
            lastPreviewNoteMarkerSignature_ = newSignature;
        }
    }
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    if (timelineCursorNotes_.isEmpty()) {
        return -1.0;
    }

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        if (note.line > line || (note.line == line && note.col >= col)) {
            return note.second;
        }
    }
    return timelineCursorNotes_.constLast().second;
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    if (second < 0.0) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    timelineView_->setCursorSeconds(second);
    timelineView_->setPlayheadSeconds(second, true);
    statusBar()->showMessage(QString("Timeline seek: L%1 C%2 -> %3s").arg(line).arg(col).arg(second, 0, 'f', 3));
}

double MainWindow::previewDurationSeconds() const
{
    double duration = 0.0;
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
    }
    if (previewTrackDurationSeconds_ > 0.0) {
        const double firstSeconds = parsedFirstSeconds();
        const double audioStartSecond = qMax(0.0, -firstSeconds);
        duration = qMax(duration, audioStartSecond + previewTrackDurationSeconds_ + 3.0);
    }
    return qMax(0.0, duration);
}

void MainWindow::updatePreviewSliderRange()
{
    if (previewSlider_ == nullptr) {
        return;
    }
    const int maximum = qMax(1, qRound(previewDurationSeconds() * 1000.0));
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setMaximum(maximum);
}

void MainWindow::updatePreviewSliderPosition(double second)
{
    if (previewSlider_ == nullptr || previewSliderDragging_) {
        return;
    }
    const int value = qBound(0, qRound(second * 1000.0), previewSlider_->maximum());
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setValue(value);
}

void MainWindow::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    previewStatsNoteMarkers_ = noteMarkers;
    updatePreviewObjectStats(qtPreviewPauseSecond_);
}

void MainWindow::clearPreviewObjectStats()
{
    previewStatsNoteMarkers_.clear();
    updatePreviewObjectStats(0.0);
}

int MainWindow::updatePreviewStatsLayoutMode(int hostWidth)
{
    if (previewStatsCard_ == nullptr || previewStatsGridLayout_ == nullptr || previewStatsChips_.isEmpty()) {
        return 0;
    }

    const int itemCount = previewStatsChips_.size();
    const QWidget* gridHost = previewStatsGridLayout_->parentWidget();
    const int horizontalSpacing = qMax(0, previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = previewStatsGridLayout_->contentsMargins();
    const int resolvedHostWidth =
        (hostWidth >= 0)
        ? hostWidth
        : ((gridHost != nullptr) ? gridHost->contentsRect().width() : previewStatsCard_->contentsRect().width());
    constexpr int kWideLayoutCols = 3;
    constexpr int kNarrowLayoutCols = 2;
    constexpr int kWideLayoutMinChipWidth = 118;
    const int wideLayoutThreshold = kWideLayoutCols * kWideLayoutMinChipWidth + qMax(0, kWideLayoutCols - 1) * horizontalSpacing;
    const bool useWideLayout = resolvedHostWidth >= wideLayoutThreshold;
    const int cols = qMin(itemCount, useWideLayout ? kWideLayoutCols : kNarrowLayoutCols);
    const int rows = qMax(1, (itemCount + cols - 1) / cols);
    const bool structureChanged = (rows != previewStatsLayoutRows_) || (cols != previewStatsLayoutCols_);
    previewStatsLayoutRows_ = rows;
    previewStatsLayoutCols_ = cols;

    const int chipHeight = qMax(
        30,
        !previewStatsChips_.isEmpty() && previewStatsChips_.constFirst() != nullptr
            ? previewStatsChips_.constFirst()->sizeHint().height()
            : 30
    );
    const int cardHeight = 16 + gridMargins.top() + gridMargins.bottom() + rows * chipHeight + qMax(0, rows - 1) * verticalSpacing;
    previewStatsCard_->setMinimumHeight(cardHeight);

    if (structureChanged) {
        while (QLayoutItem* item = previewStatsGridLayout_->takeAt(0)) {
            delete item;
        }
        for (int col = 0; col < 6; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 0);
            previewStatsGridLayout_->setColumnMinimumWidth(col, 0);
        }
        for (int row = 0; row < 6; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 0);
        }

        for (int i = 0; i < itemCount; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            previewStatsGridLayout_->addWidget(previewStatsChips_.at(i), row, col);
        }
        for (int col = 0; col < cols; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 1);
        }
        for (int row = 0; row < rows; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 1);
        }
    }

    // Keep chip widths column-driven and independent from text metrics.
    const int totalSpacing = horizontalSpacing * qMax(0, cols - 1);
    const int availableWidth = qMax(0, resolvedHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    const int columnWidth = (cols > 0) ? (availableWidth / cols) : 0;
    for (QLabel* chip : previewStatsChips_) {
        if (chip == nullptr) {
            continue;
        }
        chip->setFixedWidth(qMax(0, columnWidth));
    }

    return cardHeight;
}

void MainWindow::updatePreviewWorkspaceLayout()
{
    if (workspaceSplitter_ == nullptr || previewPanel_ == nullptr || previewControlCard_ == nullptr) {
        return;
    }

    const QRect splitterRect = workspaceSplitter_->contentsRect();
    if (splitterRect.width() <= 0 || splitterRect.height() <= 0) {
        updatePreviewPanelLayout();
        return;
    }

    const int handleWidth = qMax(0, workspaceSplitter_->handleWidth());
    const int availableWidth = qMax(0, splitterRect.width() - handleWidth);
    const int availableHeight = qMax(0, splitterRect.height());
    const int leftMinWidth = (previewLeftColumn_ != nullptr) ? previewLeftColumn_->minimumWidth() : 320;
    const int minimumRightWidth =
        (availableWidth >= leftMinWidth + kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        ? (kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        : qMin(availableWidth, kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2);
    const int rightMaxWidth =
        (availableWidth >= leftMinWidth + minimumRightWidth)
        ? qMin(kEmbeddedPreviewPanelWidthMax, availableWidth - leftMinWidth)
        : availableWidth;
    const int preferredRightMaxWidth = qMax(minimumRightWidth, rightMaxWidth);
    int preferredRightWidth = qRound(availableWidth * kEmbeddedPreviewPanelWidthRatio);
    preferredRightWidth = qMin(preferredRightWidth, preferredRightMaxWidth);
    preferredRightWidth = qMax(preferredRightWidth, qMin(kEmbeddedPreviewPanelMinWidth, preferredRightMaxWidth));

    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/workspace-base",
            QString("splitter_rect=%1x%2 handle=%3 available=%4x%5 left_min=%6 right_min=%7 right_max=%8 preferred_ratio=%.2f preferred_right=%9 control_h=%10")
                .arg(splitterRect.width())
                .arg(splitterRect.height())
                .arg(handleWidth)
                .arg(availableWidth)
                .arg(availableHeight)
                .arg(leftMinWidth)
                .arg(minimumRightWidth)
                .arg(rightMaxWidth)
                .arg(preferredRightWidth)
                .arg(controlHeight)
                .replace("%.2f", QString::number(kEmbeddedPreviewPanelWidthRatio, 'f', 2))
        );
    }
    int targetRightWidth = preferredRightWidth;
    for (int i = 0; i < 3; ++i) {
        const int panelContentWidth = qMax(0, targetRightWidth - kPreviewPanelMarginX * 2);
        const int statsHostWidth = qMax(0, panelContentWidth - 16);
        const int minimumStatsHeight = updatePreviewStatsLayoutMode(statsHostWidth);
        const int availablePreviewHeight = qMax(
            0,
            availableHeight
                - kPreviewPanelMarginTop
                - kPreviewCanvasControlGap
                - controlHeight
                - kPreviewControlStatsGap
                - minimumStatsHeight
                - kPreviewStatsBottomGap
        );
        const int heightLimitedWidth = qMax(0, availablePreviewHeight + kPreviewPanelMarginX * 2);
        const int nextRightWidth = qMin(targetRightWidth, qMax(minimumRightWidth, heightLimitedWidth));
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-calc/workspace-iter",
                QString("iter=%1 target_right=%2 panel_content_w=%3 stats_host_w=%4 stats_min_h=%5 available_preview_h=%6 height_limited_w=%7 next_right=%8")
                    .arg(i)
                    .arg(targetRightWidth)
                    .arg(panelContentWidth)
                    .arg(statsHostWidth)
                    .arg(minimumStatsHeight)
                    .arg(availablePreviewHeight)
                    .arg(heightLimitedWidth)
                    .arg(nextRightWidth)
            );
        }
        if (nextRightWidth == targetRightWidth) {
            break;
        }
        targetRightWidth = nextRightWidth;
    }

    targetRightWidth = qBound(minimumRightWidth, targetRightWidth, rightMaxWidth);
    const int targetLeftWidth =
        (availableWidth >= leftMinWidth + targetRightWidth)
        ? qMax(leftMinWidth, availableWidth - targetRightWidth)
        : qMax(0, availableWidth - targetRightWidth);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/workspace-final",
            QString("target_left=%1 target_right=%2 total_available_w=%3")
                .arg(targetLeftWidth)
                .arg(targetRightWidth)
                .arg(availableWidth)
        );
    }
    const QList<int> currentSizes = workspaceSplitter_->sizes();
    if (currentSizes.size() == 2
        && (qAbs(currentSizes.at(0) - targetLeftWidth) > 1 || qAbs(currentSizes.at(1) - targetRightWidth) > 1)) {
        workspaceSplitter_->setSizes({targetLeftWidth, targetRightWidth});
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-calc/workspace-apply",
                QString("applied_sizes=[%1,%2] previous=[%3,%4]")
                    .arg(targetLeftWidth)
                    .arg(targetRightWidth)
                    .arg(currentSizes.value(0))
                    .arg(currentSizes.value(1))
            );
        }
    }

    updatePreviewPanelLayout();
}

void MainWindow::updatePreviewPanelLayout()
{
    if (previewPanel_ == nullptr
        || previewCanvasFrame_ == nullptr
        || previewCanvasContainer_ == nullptr
        || previewControlCard_ == nullptr
        || previewStatsCard_ == nullptr) {
        return;
    }

    const QRect panelRect = previewPanel_->contentsRect();
    if (panelRect.width() <= 0 || panelRect.height() <= 0) {
        return;
    }

    const int contentX = panelRect.x() + kPreviewPanelMarginX;
    const int contentY = panelRect.y() + kPreviewPanelMarginTop;
    const int contentWidth = qMax(0, panelRect.width() - kPreviewPanelMarginX * 2);
    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    const int statsHostWidth = qMax(0, contentWidth - 16);
    const int minimumStatsHeight = updatePreviewStatsLayoutMode(statsHostWidth);
    const int availablePreviewHeight = qMax(
        0,
        panelRect.height()
            - kPreviewPanelMarginTop
            - kPreviewCanvasControlGap
            - controlHeight
            - kPreviewControlStatsGap
            - minimumStatsHeight
            - kPreviewStatsBottomGap
    );
    const int previewSide = qMax(1, qMin(contentWidth, availablePreviewHeight));
    const int controlY = contentY + previewSide + kPreviewCanvasControlGap;
    const int statsAreaY = controlY + controlHeight + kPreviewControlStatsGap;
    const int statsAreaHeight = qMax(0, panelRect.height() - (statsAreaY - panelRect.y()) - kPreviewStatsBottomGap);
    const int statsHeight = qMin(minimumStatsHeight, statsAreaHeight);
    const int statsY = statsAreaY + qMax(0, (statsAreaHeight - statsHeight) / 2);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/panel",
            QString("panel=%1x%2 content=(x=%3,y=%4,w=%5) control_h=%6 stats_host_w=%7 stats_min_h=%8 available_preview_h=%9 preview_side=%10 control_y=%11 stats_area_y=%12 stats_area_h=%13 stats_y=%14 stats_h=%15")
                .arg(panelRect.width())
                .arg(panelRect.height())
                .arg(contentX)
                .arg(contentY)
                .arg(contentWidth)
                .arg(controlHeight)
                .arg(statsHostWidth)
                .arg(minimumStatsHeight)
                .arg(availablePreviewHeight)
                .arg(previewSide)
                .arg(controlY)
                .arg(statsAreaY)
                .arg(statsAreaHeight)
                .arg(statsY)
                .arg(statsHeight)
        );
    }

    previewCanvasFrame_->setGeometry(contentX, contentY, previewSide, previewSide);
    previewCanvasContainer_->setGeometry(previewCanvasFrame_->rect().adjusted(1, 1, -1, -1));
    previewControlCard_->setGeometry(contentX, controlY, contentWidth, controlHeight);
    previewStatsCard_->setGeometry(contentX, statsY, contentWidth, statsHeight);
    updatePreviewStatsLayoutMode(statsHostWidth);

    if (!previewLayoutInitialized_) {
        previewCanvasContainer_->show();
        previewLayoutInitialized_ = true;
    }
}

void MainWindow::updatePreviewObjectStats(double second)
{
    if (previewTapStatsLabel_ == nullptr
        || previewHoldStatsLabel_ == nullptr
        || previewSlideStatsLabel_ == nullptr
        || previewTouchStatsLabel_ == nullptr
        || previewBreakStatsLabel_ == nullptr
        || previewTotalStatsLabel_ == nullptr) {
        return;
    }

    int tapTotal = 0;
    int tapPlayed = 0;
    int holdTotal = 0;
    int holdPlayed = 0;
    int slideTotal = 0;
    int slidePlayed = 0;
    int touchTotal = 0;
    int touchPlayed = 0;
    int breakTotal = 0;
    int breakPlayed = 0;
    int helperTapNonBreakTotal = 0;
    int helperTapNonBreakPlayed = 0;
    int helperTapBreakTotal = 0;
    int helperTapBreakPlayed = 0;
    int baseTotalCount = 0;
    int baseTotalPlayed = 0;
    int totalCount = 0;
    int totalPlayed = 0;

    for (const TimelineNoteMarker& marker : previewStatsNoteMarkers_) {
        const QString type = marker.type.toLower();
        const bool played = marker.second <= (second + 1e-6);
        const bool isTap = (type == "tap");
        const bool isHold = (type == "hold" || type == "touch_hold");
        const bool isSlide = (type == "slide" || type == "wifi");
        const bool isTouch = (type == "touch");
        const bool isBreak = marker.isBreak || marker.headBreak || marker.trackBreak;
        if (played) {
            ++baseTotalPlayed;
        }
        ++baseTotalCount;
        if (isTap) {
            // Legacy display semantics: "Tap" excludes break taps, and break
            // is shown as a dedicated bucket.
            if (!marker.isBreak) {
                ++tapTotal;
                if (played) {
                    ++tapPlayed;
                }
            }
        }
        if (isHold) {
            ++holdTotal;
            if (played) {
                ++holdPlayed;
            }
        }
        if (isSlide) {
            ++slideTotal;
            if (played) {
                ++slidePlayed;
            }
        }
        if (isTouch) {
            ++touchTotal;
            if (played) {
                ++touchPlayed;
            }
        }
        if (isBreak) {
            ++breakTotal;
            if (played) {
                ++breakPlayed;
            }
        }

        // Legacy parser internally has helper slide-head taps ("*_") that are
        // filtered from timeline lanes. Re-add them for preview counters so
        // Tap/Total matches legacy display numbers.
        if (isSlide && marker.hasHeadStar) {
            const double helperMoment = marker.slideTraceSecond > marker.second
                ? marker.slideTraceSecond
                : marker.second;
            const bool helperPlayed = helperMoment <= (second + 1e-6);
            if (marker.headBreak) {
                ++helperTapBreakTotal;
                if (helperPlayed) {
                    ++helperTapBreakPlayed;
                }
            } else {
                ++helperTapNonBreakTotal;
                if (helperPlayed) {
                    ++helperTapNonBreakPlayed;
                }
            }
        }
    }

    tapTotal += helperTapNonBreakTotal;
    tapPlayed += helperTapNonBreakPlayed;
    breakTotal += helperTapBreakTotal;
    breakPlayed += helperTapBreakPlayed;

    totalCount = baseTotalCount + helperTapNonBreakTotal + helperTapBreakTotal;
    totalPlayed = baseTotalPlayed + helperTapNonBreakPlayed + helperTapBreakPlayed;

    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    previewTapStatsLabel_->setText(fmt("Tap", tapPlayed, tapTotal));
    previewHoldStatsLabel_->setText(fmt("Hold", holdPlayed, holdTotal));
    previewSlideStatsLabel_->setText(fmt("Slide", slidePlayed, slideTotal));
    previewTouchStatsLabel_->setText(fmt("Touch", touchPlayed, touchTotal));
    previewBreakStatsLabel_->setText(fmt("Break", breakPlayed, breakTotal));
    previewTotalStatsLabel_->setText(fmt("Total", totalPlayed, totalCount));
}

QString MainWindow::formatPreviewTimestamp(double second) const
{
    const int totalCentiseconds = qMax(0, qRound(second * 100.0));
    const int minutes = totalCentiseconds / 6000;
    const int secondsPart = (totalCentiseconds / 100) % 60;
    const int centiseconds = totalCentiseconds % 100;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondsPart, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void MainWindow::showPreviewSliderTimeHint(int sliderValue)
{
    if (previewSlider_ == nullptr) {
        return;
    }
    const double second = static_cast<double>(sliderValue) / 1000.0;
    QStyleOptionSlider option;
    option.initFrom(previewSlider_);
    option.subControls = QStyle::SC_SliderHandle;
    option.orientation = previewSlider_->orientation();
    option.minimum = previewSlider_->minimum();
    option.maximum = previewSlider_->maximum();
    option.sliderPosition = sliderValue;
    option.sliderValue = sliderValue;
    option.upsideDown = false;
    const QRect handleRect = previewSlider_->style()->subControlRect(
        QStyle::CC_Slider,
        &option,
        QStyle::SC_SliderHandle,
        previewSlider_
    );
    const QPoint global = previewSlider_->mapToGlobal(handleRect.center() + QPoint(0, -18));
    QToolTip::showText(global, formatPreviewTimestamp(second), previewSlider_, previewSlider_->rect(), 600);
}

void MainWindow::schedulePreviewSeek(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = centerView;
    updatePreviewSliderPosition(clampedSecond);
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->start();
    } else {
        seekPreviewToSecond(clampedSecond, centerView);
    }
}

void MainWindow::seekPreviewToSecond(double second, bool centerView)
{
    ensurePreviewMediaControllerInitialized();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    qtPreviewStartSecond_ = clampedSecond;
    qtPreviewPauseSecond_ = clampedSecond;
    qtPreviewTimelineStartSecond_ = clampedSecond;
    qtPreviewTimelineElapsed_.restart();
    qtPreviewPendingTimelineSecond_ = clampedSecond;
    qtPreviewPendingTimelineCenterView_ = centerView;
    qtPreviewTimelineDirty_ = true;
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(clampedSecond);
    }
    applyQtPreviewPosition(clampedSecond, centerView);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedSecond);
}

void MainWindow::applyPreviewPlaybackRate(double rate)
{
    ensurePreviewMediaControllerInitialized();
    const double clampedRate = qMax(0.25, rate);
    if (qFuzzyCompare(previewPlaybackRate_ + 1.0, clampedRate + 1.0)) {
        return;
    }
    previewPlaybackRate_ = clampedRate;
    if (previewSpeedButton_ != nullptr) {
        QString rateText = QString::number(previewPlaybackRate_, 'f', 2);
        while (rateText.endsWith('0')) {
            rateText.chop(1);
        }
        if (rateText.endsWith('.')) {
            rateText.chop(1);
        }
        previewSpeedButton_->setText(QString("%1x").arg(rateText));
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(qtPreviewPauseSecond_, true);
    }
}

void MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    ensurePreviewMediaControllerInitialized();
    ensurePreviewSfxRuntimePrepared();
    const double startSecond = qBound(0.0, second, previewDurationSeconds());
    qtPreviewStartSecond_ = startSecond;
    qtPreviewPauseSecond_ = startSecond;
    qtPreviewLastTimelineSecond_ = startSecond;
    qtPreviewPendingTimelineSecond_ = startSecond;
    qtPreviewPendingTimelineCenterView_ = true;
    qtPreviewTimelineDirty_ = false;
    qtPreviewTimelineStartSecond_ = startSecond;
    qtPreviewTimelineCenterNextTick_ = true;
    qtPreviewTimelineElapsed_.restart();
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
        timelineView_->setPlayheadSeconds(startSecond, true);
    }
    if (previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            previewCanvas_->resetProfilingSession();
        }
        previewCanvas_->setPlayheadSeconds(startSecond);
    }
    if (!resumeFromPause && previewMediaController_ != nullptr) {
        previewMediaController_->resetProfilingSession();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
        previewMediaController_->setPlayheadSeconds(startSecond);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewSfxRuntime_->startBackgroundTrack(startSecond);
        if (previewSfxRuntime_->hasBackgroundTrack()
            && previewSfxRuntime_->isBackgroundTrackRunning()) {
            qtPreviewStartSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
            qtPreviewPauseSecond_ = qtPreviewStartSecond_;
            qtPreviewLastTimelineSecond_ = qtPreviewStartSecond_;
            qtPreviewPendingTimelineSecond_ = qtPreviewStartSecond_;
            qtPreviewPendingTimelineCenterView_ = true;
            qtPreviewTimelineDirty_ = false;
            qtPreviewTimelineStartSecond_ = qtPreviewStartSecond_;
            qtPreviewTimelineCenterNextTick_ = true;
            qtPreviewTimelineElapsed_.restart();
            qtPreviewPendingAudioCalibration_ = true;
            if (timelineView_ != nullptr) {
                timelineView_->setPlayheadSeconds(qtPreviewStartSecond_, true);
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
            if (previewMediaController_ != nullptr) {
                previewMediaController_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
        } else {
            qtPreviewPendingAudioCalibration_ = false;
        }
        previewSfxRuntime_->resetCursor(qtPreviewStartSecond_, !resumeFromPause);
        if (!resumeFromPause) {
            previewSfxRuntime_->drainEvents(qtPreviewStartSecond_);
        }
        previewSfxRuntime_->syncTouchholdVoices(qtPreviewStartSecond_);
    } else {
        qtPreviewPendingAudioCalibration_ = false;
    }
    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->startPlayback(qtPreviewStartSecond_);
    }

    qtPreviewElapsed_.restart();
    qtPreviewPlaying_ = true;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    if (previewCanvas_ != nullptr) {
        qtPreviewAwaitingFrameSwap_ = true;
        qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
        previewCanvas_->update();
    }
    if (qtPreviewTimer_ != nullptr && !qtPreviewTimer_->isActive()) {
        qtPreviewTimer_->start();
    }
    if (qtPreviewTimelineTimer_ != nullptr && !qtPreviewTimelineTimer_->isActive()) {
        qtPreviewTimelineTimer_->start();
    }
    updatePreviewSliderPosition(startSecond);
    updatePauseButtonAppearance();
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    const bool wasPlaying = qtPreviewPlaying_;
    if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        qtPreviewPauseSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
        previewSfxRuntime_->pauseBackgroundTrack();
    } else if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        qtPreviewPauseSecond_ = previewMediaController_->currentPlaybackSecond();
    }
    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->pausePlayback();
    }
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (!keepPosition) {
        qtPreviewPauseSecond_ = 0.0;
    }
    if (wasPlaying) {
        qtPreviewPendingTimelineSecond_ = qtPreviewPauseSecond_;
        qtPreviewPendingTimelineCenterView_ = false;
        qtPreviewTimelineDirty_ = true;
    }
    qtPreviewPlaying_ = false;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    qtPreviewPendingAudioCalibration_ = false;
    flushQtPreviewTimelinePosition();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->stopAll();
    }
    if (runtimeDebugOutputEnabled_ && wasPlaying && previewCanvas_ != nullptr) {
        const QString summaryPath = previewCanvas_->writeProfilingSummaryToFile();
        if (!summaryPath.isEmpty() && previewMediaController_ != nullptr) {
            QFile file(summaryPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
                QTextStream stream(&file);
                stream << previewMediaController_->profilingSummaryLines();
            }
        }
    }
    updatePreviewSliderPosition(qtPreviewPauseSecond_);
    updatePreviewObjectStats(qtPreviewPauseSecond_);
    updatePauseButtonAppearance();
}

void MainWindow::applyQtPreviewPosition(double second, bool centerView)
{
    qtPreviewPauseSecond_ = second;
    if (!qtPreviewPlaying_
        && timelineView_ != nullptr
        && (qtPreviewLastTimelineSecond_ < 0.0 || qAbs(second - qtPreviewLastTimelineSecond_) >= (1.0 / 30.0))) {
        qtPreviewPendingTimelineSecond_ = second;
        qtPreviewPendingTimelineCenterView_ = qtPreviewPendingTimelineCenterView_ || centerView;
        qtPreviewTimelineDirty_ = true;
        flushQtPreviewTimelinePosition();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
    updatePreviewSliderPosition(second);
    updatePreviewObjectStats(second);
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    if (timelineView_ == nullptr) {
        return;
    }
    if (qtPreviewPlaying_) {
        const double second = qMax(
            0.0,
            qtPreviewTimelineStartSecond_ + ((qtPreviewTimelineElapsed_.elapsed() / 1000.0) * previewPlaybackRate_)
        );
        timelineView_->setPlayheadSeconds(second, qtPreviewTimelineCenterNextTick_);
        qtPreviewLastTimelineSecond_ = second;
        qtPreviewTimelineCenterNextTick_ = false;
        return;
    }
    if (!qtPreviewTimelineDirty_) {
        return;
    }
    timelineView_->setPlayheadSeconds(qtPreviewPendingTimelineSecond_, qtPreviewPendingTimelineCenterView_);
    qtPreviewLastTimelineSecond_ = qtPreviewPendingTimelineSecond_;
    qtPreviewPendingTimelineCenterView_ = false;
    qtPreviewTimelineDirty_ = false;
}

void MainWindow::onQtPreviewTick()
{
    if (!qtPreviewPlaying_) {
        return;
    }
    if (qtPreviewPendingAudioCalibration_ && previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        const double calibratedSecond = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
        qtPreviewStartSecond_ = calibratedSecond;
        qtPreviewPauseSecond_ = calibratedSecond;
        qtPreviewLastTimelineSecond_ = -1.0;
        qtPreviewElapsed_.restart();
        qtPreviewTimelineStartSecond_ = calibratedSecond;
        qtPreviewTimelineCenterNextTick_ = true;
        qtPreviewTimelineElapsed_.restart();
        if (previewCanvas_ != nullptr) {
            previewCanvas_->noteTickForProfiling();
        }
        applyQtPreviewPosition(calibratedSecond, true);
        if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
            previewMediaController_->setPlayheadSeconds(calibratedSecond);
        }
        previewSfxRuntime_->drainEvents(calibratedSecond);
        previewSfxRuntime_->syncTouchholdVoices(calibratedSecond);
        qtPreviewPendingAudioCalibration_ = false;
        if (previewCanvas_ != nullptr) {
            qtPreviewAwaitingFrameSwap_ = true;
            qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
        }
        return;
    }
    const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
    double second = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    if (previewMediaController_ != nullptr) {
        previewMediaController_->syncPlayback(second);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->syncBackgroundTrack(second);
    }
    const double duration = previewDurationSeconds();
    if (duration > 0.0 && second > duration) {
        second = duration;
        applyQtPreviewPosition(second, true);
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->drainEvents(second);
        }
        stopQtPreviewPlayback(true);
        statusBar()->showMessage("Qt preview reached the end of current timeline.");
        return;
    }

    if (previewCanvas_ != nullptr) {
        previewCanvas_->noteTickForProfiling();
    }
    applyQtPreviewPosition(second, true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->drainEvents(second);
    }
    if (previewCanvas_ != nullptr) {
        qtPreviewAwaitingFrameSwap_ = true;
        qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
    }
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    if (timelineCursorNotes_.isEmpty()) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    const TimelineCursorNote* best = nullptr;
    bool foundSameLane = false;
    const bool preferLane = lane >= 1;
    const double target = qMax(0.0, second);

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        const bool sameLane = preferLane && note.lane == lane;
        if (preferLane) {
            if (sameLane && !foundSameLane) {
                best = &note;
                foundSameLane = true;
                continue;
            }
            if (!sameLane && foundSameLane) {
                continue;
            }
            if (!sameLane && !foundSameLane && best != nullptr) {
                // Still in all-note fallback mode.
            }
        }

        if (best == nullptr) {
            best = &note;
            continue;
        }

        const double bestDelta = qAbs(best->second - target);
        const double noteDelta = qAbs(note.second - target);
        if (noteDelta + 1e-9 < bestDelta) {
            best = &note;
            continue;
        }
        if (qAbs(noteDelta - bestDelta) <= 1e-9) {
            if (note.line < best->line || (note.line == best->line && note.col < best->col)) {
                best = &note;
            }
        }
    }

    if (best == nullptr) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    jumpToLocation(best->line, best->col);
    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(target, 0, 'f', 3)
            .arg(best->line)
            .arg(best->col)
    );
}

QString MainWindow::resolveDefaultTrackPath() const
{
    const QString envTrack = qEnvironmentVariable("MIACODE_TRACK_PATH", qEnvironmentVariable("MAIMURI_TRACK_PATH"));
    if (!envTrack.isEmpty() && QFileInfo::exists(envTrack)) {
        return envTrack;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
        if (QFileInfo::exists(siblingTrack)) {
            return siblingTrack;
        }
    }
    if (!lastTrackPath_.isEmpty() && QFileInfo::exists(lastTrackPath_)) {
        return lastTrackPath_;
    }
    return QString();
}

QString MainWindow::resolveLatencyDetectorTrackPath() const
{
    if (currentFilePath_.isEmpty()) {
        return QString();
    }
    const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
    if (QFileInfo::exists(siblingTrack)) {
        return QDir::cleanPath(siblingTrack);
    }
    return QString();
}

void MainWindow::updateLatencyDetectorAvailability()
{
    const bool enabled = !resolveLatencyDetectorTrackPath().isEmpty();
    if (latencyDetectorAction_ != nullptr) {
        latencyDetectorAction_->setEnabled(enabled);
    }
    if (latencyDetectorButton_ != nullptr) {
        latencyDetectorButton_->setEnabled(enabled);
    }
}

QString MainWindow::resolvePreviewSkinDir() const
{
    const QString assetSkinDir = miacode::assets::assetPath("skin");
    if (QFileInfo::exists(QDir(assetSkinDir).filePath("tap.png"))) {
        return assetSkinDir;
    }
    return QString();
}

QString MainWindow::resolveProjectRenderStateFilePath() const
{
    if (currentFilePath_.isEmpty()) {
        return QString();
    }
    const QDir projectDir(QFileInfo(currentFilePath_).absolutePath());
    return projectDir.filePath(".miacode_render_settings.json");
}

QString MainWindow::resolveInitialOpenDirectory() const
{
    if (!lastOpenDir_.isEmpty() && QDir(lastOpenDir_).exists()) {
        return lastOpenDir_;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString currentDir = QFileInfo(currentFilePath_).absolutePath();
        if (QDir(currentDir).exists()) {
            return currentDir;
        }
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QDir(appDir).exists()) {
        return appDir;
    }
    return QDir::currentPath();
}

void MainWindow::loadPortableState()
{
    lastOpenDir_.clear();
    softwarePreviewAudioSettings_ = PreviewAudioSettings();
    previewAudioSettings_ = softwarePreviewAudioSettings_;
    showSlideTracks_ = true;
    showJudgeMarkers_ = false;
    showTouchTrail_ = false;
    previewBackgroundBrightness_ = 0.2;
    previewShowDebugInfo_ = false;

    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value("app").toObject();
    const QJsonObject preview = app.value("preview").toObject();

    const QString dir = app.value("last_open_dir").toString();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        lastOpenDir_ = QDir::cleanPath(dir);
    }
    const QString trackPath = app.value("last_track_path").toString();
    if (!trackPath.isEmpty() && QFileInfo::exists(trackPath)) {
        lastTrackPath_ = QDir::cleanPath(trackPath);
    }
    showSlideTracks_ = true;
    if (preview.value("show_judge_markers").isBool()) {
        showJudgeMarkers_ = preview.value("show_judge_markers").toBool(false);
    }
    if (preview.value("show_touch_trail").isBool()) {
        showTouchTrail_ = preview.value("show_touch_trail").toBool(false);
    }
    if (preview.value("background_brightness").isDouble()) {
        previewBackgroundBrightness_ = qBound(0.0, preview.value("background_brightness").toDouble(0.2), 1.0);
    }
    if (preview.value("show_debug_info").isBool()) {
        previewShowDebugInfo_ = preview.value("show_debug_info").toBool(false);
    }
    if (preview.value("audio").isObject()) {
        softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview.value("audio").toObject());
    } else {
        softwarePreviewAudioSettings_.bgmVolume = preview.value("bgm_volume").toDouble(softwarePreviewAudioSettings_.bgmVolume);
        const double legacyAnswer = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.answerVolume);
        const double legacySlide = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.slideVolume);
        const double legacyBreak = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.breakVolume);
        const double legacyEx = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.exVolume);
        const double legacyTouch = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.touchVolume);
        const double legacyTouchhold = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.touchholdVolume);
        softwarePreviewAudioSettings_.answerVolume = preview.value("answer_volume").toDouble(legacyAnswer);
        softwarePreviewAudioSettings_.slideVolume = preview.value("slide_volume").toDouble(legacySlide);
        softwarePreviewAudioSettings_.breakVolume = preview.value("break_volume").toDouble(legacyBreak);
        softwarePreviewAudioSettings_.exVolume = preview.value("ex_volume").toDouble(legacyEx);
        softwarePreviewAudioSettings_.touchVolume = preview.value("touch_volume").toDouble(legacyTouch);
        softwarePreviewAudioSettings_.touchholdVolume = preview.value("touchhold_volume").toDouble(legacyTouchhold);
        softwarePreviewAudioSettings_.normalize();
    }
    previewAudioSettings_ = softwarePreviewAudioSettings_;
}

void MainWindow::savePortableState() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value("app").toObject();
    QJsonObject preview = app.value("preview").toObject();

    app.insert("last_open_dir", lastOpenDir_);
    app.insert("last_track_path", lastTrackPath_);
    app.insert("show_slide_tracks", true);

    preview.insert("show_judge_markers", showJudgeMarkers_);
    preview.insert("show_touch_trail", showTouchTrail_);
    preview.insert("background_brightness", previewBackgroundBrightness_);
    preview.insert("show_debug_info", previewShowDebugInfo_);
    preview.insert("audio", softwarePreviewAudioSettings_.toJson());

    app.insert("preview", preview);
    root.insert("app", app);
    UiText::savePreferencesObject(root);
}

void MainWindow::loadProjectRenderState()
{
    previewAudioSettings_ = softwarePreviewAudioSettings_;
    previewAudioSettings_.normalize();

    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();
    if (root.value("audio").isObject()) {
        previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("audio").toObject());
    } else if (root.value("preview_audio").isObject()) {
        previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("preview_audio").toObject());
    }
    previewAudioSettings_.normalize();
}

void MainWindow::saveProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QJsonObject root;
    root.insert("audio", previewAudioSettings_.toJson());
    root.insert("schema", "miacode_render_settings_v1");
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return;
    }
    file.commit();
}

void MainWindow::removeProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
}

void MainWindow::applyPreviewAudioSettingsToRuntime()
{
    previewAudioSettings_.normalize();
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->applyLevels(previewAudioSettings_);
    }
}

void MainWindow::setLastOpenDirectory(const QString& pathOrDir)
{
    if (pathOrDir.isEmpty()) {
        return;
    }

    QString dirCandidate;
    const QFileInfo info(pathOrDir);
    if (info.isDir()) {
        dirCandidate = info.absoluteFilePath();
    } else {
        dirCandidate = info.absolutePath();
    }
    dirCandidate = QDir::cleanPath(dirCandidate);
    if (!QDir(dirCandidate).exists()) {
        return;
    }
    if (lastOpenDir_ == dirCandidate) {
        return;
    }
    lastOpenDir_ = dirCandidate;
    savePortableState();
}

QString MainWindow::transformChartText(const QString& input, ChartTransformOp op, int* changedCount) const
{
    auto isDigitLane = [](QChar ch) {
        return ch >= QChar('1') && ch <= QChar('8');
    };
    auto isMirrorOp = [](ChartTransformOp current) {
        return current == ChartTransformOp::MirrorLeftRight || current == ChartTransformOp::MirrorUpDown;
    };
    auto mapLaneGeneral = [op](int lane) -> int {
        static const int mapMirrorLR[8] = {8, 7, 6, 5, 4, 3, 2, 1};
        static const int mapMirrorUD[8] = {4, 3, 2, 1, 8, 7, 6, 5};
        if (lane < 1 || lane > 8) {
            return lane;
        }
        switch (op) {
        case ChartTransformOp::MirrorLeftRight:
            return mapMirrorLR[lane - 1];
        case ChartTransformOp::MirrorUpDown:
            return mapMirrorUD[lane - 1];
        case ChartTransformOp::Rotate180:
            return ((lane - 1 + 4) % 8) + 1;
        case ChartTransformOp::Rotate45CounterClockwise:
            return ((lane - 1 + 7) % 8) + 1;
        case ChartTransformOp::Rotate45Clockwise:
            return ((lane - 1 + 1) % 8) + 1;
        }
        return lane;
    };
    auto mapLaneTouchDE = [op, mapLaneGeneral](int lane) -> int {
        static const int mapMirrorLR[8] = {1, 8, 7, 6, 5, 4, 3, 2};
        static const int mapMirrorUD[8] = {5, 4, 3, 2, 1, 8, 7, 6};
        if (lane < 1 || lane > 8) {
            return lane;
        }
        if (op == ChartTransformOp::MirrorLeftRight) {
            return mapMirrorLR[lane - 1];
        }
        if (op == ChartTransformOp::MirrorUpDown) {
            return mapMirrorUD[lane - 1];
        }
        return mapLaneGeneral(lane);
    };
    auto laneChar = [](int lane) -> QChar {
        return QChar('0' + qBound(1, lane, 8));
    };
    auto isClockwiseArc = [](int startLane, QChar arcType) -> bool {
        const bool groupA = (startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8);
        return groupA ? (arcType == QChar('>')) : (arcType == QChar('<'));
    };
    auto arcTypeFromDirection = [](int startLane, bool clockwise) -> QChar {
        const bool groupA = (startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8);
        if (clockwise) {
            return groupA ? QChar('>') : QChar('<');
        }
        return groupA ? QChar('<') : QChar('>');
    };

    int changed = 0;

    const auto transformToken = [&](const QString& token) -> QString {
        if (token.isEmpty()) {
            return token;
        }

        const auto transformTouchToken = [&](const QString& in) -> QString {
            if (in.isEmpty()) {
                return in;
            }
            QString out = in;
            const QChar head = in.at(0).toUpper();
            if (head == QChar('C')) {
                return out;
            }
            if (in.size() >= 2 && (head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E')) && isDigitLane(in.at(1))) {
                const int lane = in.at(1).digitValue();
                const int mapped = (head == QChar('D') || head == QChar('E')) ? mapLaneTouchDE(lane) : mapLaneGeneral(lane);
                const QChar mappedChar = laneChar(mapped);
                if (mappedChar != in.at(1)) {
                    out[1] = mappedChar;
                    ++changed;
                }
            }
            return out;
        };

        const auto transformSlideToken = [&](const QString& in) -> QString {
            if (in.isEmpty() || !isDigitLane(in.at(0))) {
                return in;
            }

            const int bracketPos = in.indexOf('[');
            const QString core = bracketPos >= 0 ? in.left(bracketPos) : in;
            const QString suffix = bracketPos >= 0 ? in.mid(bracketPos) : QString();
            if (core.isEmpty()) {
                return in;
            }
            auto hasSlideOperatorAhead = [](const QString& text) -> bool {
                static const QString ops = QStringLiteral("-^v<>Vpqszw");
                for (QChar c : text) {
                    if (ops.contains(c)) {
                        return true;
                    }
                }
                return false;
            };
            if (!hasSlideOperatorAhead(core.mid(1))) {
                return in;
            }

            QString out;
            out.reserve(core.size());
            int i = 0;
            const int startOriginal = core.at(0).digitValue();
            int segmentStartOriginal = startOriginal;
            int segmentStartMapped = mapLaneGeneral(startOriginal);
            const QChar mappedStart = laneChar(segmentStartMapped);
            out.append(mappedStart);
            if (mappedStart != core.at(0)) {
                ++changed;
            }
            i = 1;

            while (i < core.size()) {
                QString opToken;
                int opLength = 0;
                const QChar c = core.at(i);
                if (i + 1 < core.size()
                    && ((c == QChar('p') && core.at(i + 1) == QChar('p'))
                        || (c == QChar('q') && core.at(i + 1) == QChar('q')))) {
                    opToken = QString(c) + core.at(i + 1);
                    opLength = 2;
                } else if (QStringLiteral("-^vVpqsz<>w").contains(c)) {
                    opToken = QString(c);
                    opLength = 1;
                }

                if (opLength == 0) {
                    if (isDigitLane(c)) {
                        const QChar mapped = laneChar(mapLaneGeneral(c.digitValue()));
                        out.append(mapped);
                        if (mapped != c) {
                            ++changed;
                        }
                    } else {
                        out.append(c);
                    }
                    ++i;
                    continue;
                }

                i += opLength;
                QString transformedOp = opToken;

                if (opToken == "<" || opToken == ">") {
                    const bool clockwiseOriginal = isClockwiseArc(segmentStartOriginal, opToken.at(0));
                    const bool clockwiseTarget = isMirrorOp(op) ? !clockwiseOriginal : clockwiseOriginal;
                    transformedOp = QString(arcTypeFromDirection(segmentStartMapped, clockwiseTarget));
                } else if (isMirrorOp(op)) {
                    if (opToken == "p") transformedOp = "q";
                    else if (opToken == "q") transformedOp = "p";
                    else if (opToken == "s") transformedOp = "z";
                    else if (opToken == "z") transformedOp = "s";
                    else if (opToken == "pp") transformedOp = "qq";
                    else if (opToken == "qq") transformedOp = "pp";
                }

                out.append(transformedOp);
                if (transformedOp != opToken) {
                    ++changed;
                }

                if (opToken == "V") {
                    if (i + 1 >= core.size() || !isDigitLane(core.at(i)) || !isDigitLane(core.at(i + 1))) {
                        out.append(core.mid(i));
                        break;
                    }
                    const int midOriginal = core.at(i).digitValue();
                    const int endOriginal = core.at(i + 1).digitValue();
                    const QChar midMapped = laneChar(mapLaneGeneral(midOriginal));
                    const QChar endMapped = laneChar(mapLaneGeneral(endOriginal));
                    out.append(midMapped);
                    out.append(endMapped);
                    if (midMapped != core.at(i)) {
                        ++changed;
                    }
                    if (endMapped != core.at(i + 1)) {
                        ++changed;
                    }
                    segmentStartOriginal = endOriginal;
                    segmentStartMapped = endMapped.digitValue();
                    i += 2;
                } else {
                    if (i >= core.size() || !isDigitLane(core.at(i))) {
                        out.append(core.mid(i));
                        break;
                    }
                    const int endOriginal = core.at(i).digitValue();
                    const QChar endMapped = laneChar(mapLaneGeneral(endOriginal));
                    out.append(endMapped);
                    if (endMapped != core.at(i)) {
                        ++changed;
                    }
                    segmentStartOriginal = endOriginal;
                    segmentStartMapped = endMapped.digitValue();
                    ++i;
                }
            }

            return out + suffix;
        };

        if (token.at(0).toUpper() == QChar('C')) {
            return transformTouchToken(token);
        }
        if (token.size() >= 2) {
            const QChar head = token.at(0).toUpper();
            if ((head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E')) && isDigitLane(token.at(1))) {
                return transformTouchToken(token);
            }
        }
        if (!isDigitLane(token.at(0))) {
            return token;
        }

        bool allDigits = true;
        for (QChar ch : token) {
            if (!isDigitLane(ch)) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            QString out = token;
            for (int i = 0; i < out.size(); ++i) {
                const QChar mapped = laneChar(mapLaneGeneral(out.at(i).digitValue()));
                if (mapped != out.at(i)) {
                    out[i] = mapped;
                    ++changed;
                }
            }
            return out;
        }

        bool hasSlideOps = false;
        for (QChar ch : token) {
            if (QStringLiteral("-^v<>Vpqszw").contains(ch)) {
                hasSlideOps = true;
                break;
            }
        }
        if (hasSlideOps) {
            return transformSlideToken(token);
        }

        QString out = token;
        const QChar mapped = laneChar(mapLaneGeneral(token.at(0).digitValue()));
        if (mapped != token.at(0)) {
            out[0] = mapped;
            ++changed;
        }
        return out;
    };

    QString transformed;
    transformed.reserve(input.size());
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                transformed.append(transformToken(token));
                token.clear();
            }
        };

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken();
                transformed.append(line.mid(i));
                i = line.size();
                break;
            }
            if (ch.isSpace() || ch == QChar('/') || ch == QChar('`') || ch == QChar(',')) {
                flushToken();
                transformed.append(ch);
                continue;
            }
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(')', i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                flushToken();
                const int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('H') && line.mid(i, 3) == QStringLiteral("HS*")) {
                flushToken();
                const int close = line.indexOf('>', i + 3);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            token.append(ch);
        }

        flushToken();
        if (lineIndex + 1 < lines.size()) {
            transformed.append('\n');
        }
    }

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return transformed;
}

void MainWindow::onMirrorLeftRight()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Mirror Left/Right", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorLeftRight, changedCount);
    });
}

void MainWindow::onMirrorUpDown()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Mirror Up/Down", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorUpDown, changedCount);
    });
}

void MainWindow::onRotate180()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate 180", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate180, changedCount);
    });
}

void MainWindow::onRotate45CounterClockwise()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate -45", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45CounterClockwise, changedCount);
    });
}

void MainWindow::onRotate45Clockwise()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate +45", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45Clockwise, changedCount);
    });
}

void MainWindow::bootstrapPreviewWindow()
{
    const QString scriptPath = resolvePreviewSessionScriptPath();
    appendOutput("preview/bootstrap", scriptPath.isEmpty() ? "script=(not found)" : ("script=" + scriptPath));
    if (!ensurePreviewSessionStarted()) {
        appendOutput("preview/bootstrap", "failed to start resident preview session");
        return;
    }
    appendOutput(
        "preview/bootstrap",
        QString("resident preview session started, pid=%1").arg(previewProcess_ != nullptr ? previewProcess_->processId() : -1)
    );
    previewArrangeRetryCount_ = 0;
    QTimer::singleShot(80, this, &MainWindow::arrangeWithPreviewWindow);
}

void MainWindow::arrangeWithPreviewWindow()
{
#ifdef Q_OS_WIN
    QScreen* screen = this->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        appendOutput("preview/layout", "no available screen");
        return;
    }

    const QRect workArea = screen->availableGeometry();
    const PreviewIntegration::SideBySideLayout layout = PreviewIntegration::computeSideBySideLayout(workArea);
    QRect editorFrameRect = layout.editorRect;
    const int frameLeftInset = qMax(0, geometry().left() - frameGeometry().left());
    const int frameTopInset = qMax(0, geometry().top() - frameGeometry().top());
    const int frameRightInset = qMax(0, frameGeometry().right() - geometry().right());
    const int frameBottomInset = qMax(0, frameGeometry().bottom() - geometry().bottom());
    const int frameExtraW = frameLeftInset + frameRightInset;
    const int frameExtraH = frameTopInset + frameBottomInset;

    // Strict frame-level alignment: editor frame starts exactly at layout.editorRect
    // so preview/editor never overlap.
    editorFrameRect.setTop(workArea.top());
    editorFrameRect.setBottom(workArea.bottom());
    if (editorFrameRect.left() < workArea.left()) {
        editorFrameRect.moveLeft(workArea.left());
    }
    if (editorFrameRect.right() > workArea.right()) {
        editorFrameRect.moveRight(workArea.right());
    }

    QRect editorRect(
        editorFrameRect.left() + frameLeftInset,
        editorFrameRect.top() + frameTopInset,
        qMax(320, editorFrameRect.width() - frameExtraW),
        qMax(320, editorFrameRect.height() - frameExtraH)
    );
    setGeometry(editorRect);

    QString detail;
    const qint64 pid = (previewProcess_ != nullptr && previewProcess_->state() == QProcess::Running)
        ? previewProcess_->processId()
        : -1;
    if (!PreviewIntegration::placePreviewWindow(pid, layout.previewRect, &detail)) {
        if (previewArrangeRetryCount_ < 30) {
            ++previewArrangeRetryCount_;
            QTimer::singleShot(120, this, &MainWindow::arrangeWithPreviewWindow);
        } else {
            appendOutput("preview/layout", "preview window placement failed: " + detail);
        }
        return;
    }

    if (previewArrangeRetryCount_ > 0) {
        appendOutput("preview/layout", QString("arranged after retry=%1 (%2)").arg(previewArrangeRetryCount_).arg(detail));
    } else {
        appendOutput("preview/layout", "arranged (" + detail + ")");
    }
    previewArrangeRetryCount_ = 0;
#endif
}

void MainWindow::onStopPreview()
{
    stopQtPreviewPlayback(false);
    seekPreviewToSecond(0.0, true);
    statusBar()->showMessage("Qt preview stopped.");
}

void MainWindow::onPreviewFromStart()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!saveBeforePreviewStart()) {
        return;
    }
    refreshTimelineMetadata();
    if (!legacyPygamePreviewEnabled_) {
        if (timelineView_ != nullptr) {
            timelineView_->setCursorSeconds(0.0);
        }
        startQtPreviewPlayback(0.0, false);
        updatePauseButtonAppearance();
        statusBar()->showMessage("Qt preview playback started from 0.00s.");
        return;
    }
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadSeconds(0.0, true);
    }
    startPreviewProcess("start", -1, -1);
}

void MainWindow::onPreviewFromCursor()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!saveBeforePreviewStart()) {
        return;
    }
    const auto [line, col] = currentSelectionOrCursorLineCol();
    refreshTimelineMetadata();
    seekTimelineToCursor(line, col);
    const double second = timelineSecondForCursor(line, col);
    if (!legacyPygamePreviewEnabled_) {
        startQtPreviewPlayback(second, false);
        updatePauseButtonAppearance();
        statusBar()->showMessage(QString("Qt preview playback started from %1s.").arg(second, 0, 'f', 2));
        return;
    }
    startPreviewProcess("cursor", line, col);
}

void MainWindow::onTogglePreviewPause()
{
    if (!legacyPygamePreviewEnabled_) {
        if (qtPreviewPlaying_) {
            stopQtPreviewPlayback(true);
            updatePauseButtonAppearance();
            statusBar()->showMessage(QString("Qt preview paused at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
        } else {
            if (!hasActiveDifficulty()) {
                statusBar()->showMessage("Select a difficulty field first.");
                return;
            }
            if (!saveBeforePreviewStart()) {
                return;
            }
            startQtPreviewPlayback(qtPreviewPauseSecond_, true);
            updatePauseButtonAppearance();
            statusBar()->showMessage(QString("Qt preview resumed at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
        }
        return;
    }
    if (!ensurePreviewSessionStarted()) {
        return;
    }
    QJsonObject cmd{
        {"cmd", "pause_toggle"},
    };
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send pause command");
        return;
    }
    previewProcess_->waitForBytesWritten(1000);
}

void MainWindow::onToggleJudgeMarkers(bool checked)
{
    showJudgeMarkers_ = checked;
    savePortableState();
    sendPreviewConfigCommand();
    statusBar()->showMessage(
        showJudgeMarkers_
            ? uiText("status.judge_marker_enabled", "Judge markers enabled.")
            : uiText("status.judge_marker_disabled", "Judge markers hidden.")
    );
}

void MainWindow::onToggleTouchTrail(bool checked)
{
    showTouchTrail_ = checked;
    savePortableState();
    sendPreviewConfigCommand();
    statusBar()->showMessage(
        showTouchTrail_
            ? uiText("status.touch_trail_enabled", "Touch trail enabled.")
            : uiText("status.touch_trail_disabled", "Touch trail hidden.")
    );
}

void MainWindow::onPreviewAudioSettings()
{
    onPreviewRenderSettings();
}

void MainWindow::onPreviewDisplaySettings()
{
    onPreviewRenderSettings();
}

void MainWindow::onPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(uiText("dialog.preferences.title", "Preferences"));
    dialog.setModal(true);
    dialog.setMinimumWidth(460);
    dialog.setStyleSheet(
        "QDialog { background: #F8FAFD; }"
        "QGroupBox { background: #FFFFFF; border: 1px solid #DCE5F0; border-radius: 10px; margin-top: 12px; padding-top: 10px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"
        "QLabel { color: #203040; }"
        "QToolButton#PreferenceMenuButton {"
        " min-height: 30px;"
        " min-width: 180px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " padding: 4px 10px;"
        " background: #FFFFFF;"
        " color: #223042;"
        " font-weight: 600;"
        " text-align: left;"
        "}"
        "QToolButton#PreferenceMenuButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        "QToolButton#PreferenceMenuButton:pressed { background: #E8F1FB; border-color: #9FC1E9; }"
        "QPushButton { min-width: 92px; min-height: 30px; border: 1px solid #BFD0E3; border-radius: 6px; background: #FFFFFF; color: #223042; }"
        "QPushButton:hover { background: #F3F8FF; border-color: #9FC1E9; }"
    );

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* interfaceGroup = new QGroupBox(uiText("dialog.preferences.interface_group", "Interface"), &dialog);
    auto* interfaceLayout = new QFormLayout(interfaceGroup);
    interfaceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    interfaceLayout->setHorizontalSpacing(12);
    interfaceLayout->setVerticalSpacing(8);

    const UiText::LanguagePreference currentPreference = UiText::preferredLanguage();
    UiText::LanguagePreference selectedPreference = currentPreference;
    const auto languageLabel = [](UiText::LanguagePreference preference) -> QString {
        switch (preference) {
        case UiText::LanguagePreference::English:
            return uiText("dialog.preferences.language.english", "English");
        case UiText::LanguagePreference::Chinese:
            return uiText("dialog.preferences.language.chinese", "Simplified Chinese");
        case UiText::LanguagePreference::System:
        default:
            return uiText("dialog.preferences.language.system", "Follow System");
        }
    };

    auto* languageLabelWidget = new QLabel(uiText("dialog.preferences.language", "Language"), interfaceGroup);
    auto* languageButton = new QToolButton(interfaceGroup);
    languageButton->setObjectName("PreferenceMenuButton");
    languageButton->setFont(uiAccentFont(10, QFont::DemiBold));
    languageButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    languageButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    languageButton->setText(languageLabel(selectedPreference));
    auto* languageMenu = new QMenu(languageButton);
    languageMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*languageMenu);
    const QList<UiText::LanguagePreference> languageOptions{
        UiText::LanguagePreference::System,
        UiText::LanguagePreference::English,
        UiText::LanguagePreference::Chinese,
    };
    for (UiText::LanguagePreference preference : languageOptions) {
        QAction* action = languageMenu->addAction(languageLabel(preference));
        action->setCheckable(true);
        action->setChecked(preference == selectedPreference);
        connect(action, &QAction::triggered, &dialog, [&, preference, languageMenu, languageButton]() {
            selectedPreference = preference;
            for (QAction* candidate : languageMenu->actions()) {
                candidate->setChecked(candidate->text() == languageLabel(selectedPreference));
            }
            languageButton->setText(languageLabel(selectedPreference));
        });
    }
    int languageButtonWidth = 0;
    const QFontMetrics languageMetrics(languageButton->font());
    for (UiText::LanguagePreference preference : languageOptions) {
        languageButtonWidth = qMax(languageButtonWidth, languageMetrics.horizontalAdvance(languageLabel(preference)));
    }
    languageButton->setFixedWidth(languageButtonWidth + 28);
    connect(languageButton, &QToolButton::clicked, &dialog, [languageButton, languageLabelWidget, languageMenu]() {
        const int estimatedItemHeight = qMax(32, languageButton->sizeHint().height() + 2);
        const QPoint labelCenterGlobal = languageLabelWidget->mapToGlobal(QPoint(languageLabelWidget->width(), languageLabelWidget->height() / 2));
        const QPoint buttonTopLeftGlobal = languageButton->mapToGlobal(QPoint(0, 0));
        const QPoint popupPos(buttonTopLeftGlobal.x(), labelCenterGlobal.y() - estimatedItemHeight / 2 - 7);
        languageMenu->popup(popupPos);
    });
    interfaceLayout->addRow(languageLabelWidget, languageButton);
    rootLayout->addWidget(interfaceGroup);

    auto* editorGroup = new QGroupBox(uiText("dialog.preferences.editor_group", "Editor"), &dialog);
    auto* editorLayout = new QVBoxLayout(editorGroup);
    editorLayout->setContentsMargins(12, 10, 12, 12);
    auto* editorPlaceholder = new QLabel(
        "Work in progress",
        editorGroup
    );
    editorPlaceholder->setWordWrap(true);
    editorLayout->addWidget(editorPlaceholder);
    rootLayout->addWidget(editorGroup);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (selectedPreference == currentPreference) {
        return;
    }

    UiText::setPreferredLanguage(selectedPreference);
    statusBar()->showMessage(uiText("status.preferences_saved", "Preferences saved. Restart to apply."));
    QMessageBox::information(
        this,
        uiText("dialog.preferences.restart_title", "Restart Required"),
        uiText("dialog.preferences.restart_message", "Language preference saved. Restart MiaCode to apply menu, font, and UI text updates.")
    );
}

void MainWindow::onAbout()
{
    QString buildType = "Release";
#ifndef NDEBUG
    buildType = "Debug";
#endif
    const QString platform = QString("%1 / %2 / %3")
        .arg(QSysInfo::productType())
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(QSysInfo::buildAbi());

    QDialog dialog(this);
    dialog.setWindowTitle(uiText("action.about", "About"));
    dialog.setModal(true);
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(
        "QDialog { background: #F8FAFD; }"
        "QFrame#AboutCard { background: #FFFFFF; border: 1px solid #DCE5F0; border-radius: 10px; }"
        "QLabel#AboutTitle { color: #1B2A3B; font-size: 26px; font-weight: 700; }"
        "QLabel#AboutVersion { color: #2B4D78; background: #EAF2FC; border: 1px solid #C7DBF5; border-radius: 10px; padding: 2px 8px; }"
        "QLabel#AboutKey { color: #5B697A; }"
        "QLabel#AboutValue { color: #1D2C3E; font-weight: 600; }"
        "QPushButton { min-width: 92px; min-height: 30px; border: 1px solid #BFD0E3; border-radius: 6px; background: #FFFFFF; color: #223042; }"
        "QPushButton:hover { background: #F3F8FF; border-color: #9FC1E9; }"
    );

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    auto* versionLabel = new QLabel(QString("v%1").arg(QCoreApplication::applicationVersion()), card);
    versionLabel->setObjectName("AboutVersion");
    titleRow->addWidget(titleLabel, 0, Qt::AlignVCenter);
    titleRow->addSpacing(8);
    titleRow->addWidget(versionLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    cardLayout->addLayout(titleRow);

    auto* infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    auto addRow = [card, infoGrid](int row, const QString& key, const QString& value) {
        auto* k = new QLabel(key, card);
        k->setObjectName("AboutKey");
        auto* v = new QLabel(value, card);
        v->setObjectName("AboutValue");
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoGrid->addWidget(k, row, 0);
        infoGrid->addWidget(v, row, 1);
    };
    addRow(0, uiText("about.platform", "Release Platform"), platform);
    addRow(1, uiText("about.build_type", "Build Type"), buildType);
    cardLayout->addLayout(infoGrid);
    rootLayout->addWidget(card);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::onOpenLatencyDetector()
{
    const QString trackPath = resolveLatencyDetectorTrackPath();
    bool wholeBpmOk = false;
    const double wholeBpm = parsedWholeBpm(&wholeBpmOk);
    const QString meterId = parsedLatencyMeterId();
    if (trackPath.isEmpty()) {
        statusBar()->showMessage(UiText::isChineseUi()
            ? QStringLiteral("当前谱面目录缺少 track.mp3，无法打开BPM&偏移检测。")
            : QStringLiteral("track.mp3 was not found next to the current chart."));
        updateLatencyDetectorAvailability();
        return;
    }

    if (latencyDetectorDialog_ != nullptr) {
        if (latencyDetectorDialog_->trackPath() == trackPath) {
            latencyDetectorDialog_->setOffsetSeconds(parsedFirstSeconds());
            latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
            latencyDetectorDialog_->setMeterId(meterId);
            latencyDetectorDialog_->raise();
            latencyDetectorDialog_->activateWindow();
            return;
        }
        latencyDetectorDialog_->close();
        latencyDetectorDialog_.clear();
    }

    latencyDetectorDialog_ = new LatencyDetectorDialog(trackPath, currentFilePath_, previewAudioSettings_, this);
    latencyDetectorDialog_->setOffsetSeconds(parsedFirstSeconds());
    latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
    latencyDetectorDialog_->setMeterId(meterId);
    connect(latencyDetectorDialog_, &LatencyDetectorDialog::offsetChanged, this, [this](double seconds) {
        applyLatencyDetectorOffset(seconds);
    });
    connect(latencyDetectorDialog_, &LatencyDetectorDialog::bpmChanged, this, [this](double bpm) {
        applyLatencyDetectorBpm(bpm);
    });
    connect(latencyDetectorDialog_, &LatencyDetectorDialog::meterIdChanged, this, [this](const QString& value) {
        applyLatencyDetectorMeter(value);
    });
    connect(latencyDetectorDialog_, &QObject::destroyed, this, [this]() {
        latencyDetectorDialog_.clear();
    });
    latencyDetectorDialog_->show();
    latencyDetectorDialog_->raise();
    latencyDetectorDialog_->activateWindow();
}

void MainWindow::onPreviewRenderSettings()
{
    previewAudioSettings_.normalize();
    if (legacyPygamePreviewEnabled_) {
        ensurePreviewSessionStarted();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(uiText("dialog.render_settings.title", "Render Settings"));
    dialog.setModal(true);
    dialog.resize(560, 520);
    dialog.setMinimumWidth(520);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* audioGroup = new QGroupBox(uiText("dialog.render_settings.audio_group", "Audio"), &dialog);
    auto* audioFormLayout = new QFormLayout(audioGroup);
    audioFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    audioFormLayout->setHorizontalSpacing(10);
    audioFormLayout->setVerticalSpacing(8);

    const auto addAudioRow = [&](const QString& labelText, int valuePercent, QSlider** sliderOut, QLabel** labelOut) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 100);
        slider->setValue(valuePercent);
        auto* label = new QLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
    };

    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.bgm", "BGM Volume"), previewAudioSettings_.bgmPercent(), &bgmSlider, &bgmLabel);
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.answer", "Answer Volume"), previewAudioSettings_.answerPercent(), &answerSlider, &answerLabel);
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.slide", "Slide Volume"), previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel);
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.break", "Break Volume"), previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel);
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.ex", "EX Volume"), previewAudioSettings_.exPercent(), &exSlider, &exLabel);
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.touch", "Touch Volume"), previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel);
    QSlider* touchholdSlider = nullptr;
    QLabel* touchholdLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.touchhold", "TouchHold Volume"), previewAudioSettings_.touchholdPercent(), &touchholdSlider, &touchholdLabel);

    auto* restoreButton = new QPushButton(uiText("dialog.render_settings.button.restore_project_default", "Restore Project Audio to Software Default"), audioGroup);
    audioFormLayout->addRow(QString(), restoreButton);

    auto* brightnessRow = new QWidget(&dialog);
    auto* brightnessRowLayout = new QHBoxLayout(brightnessRow);
    brightnessRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* brightnessSlider = new QSlider(Qt::Horizontal, brightnessRow);
    brightnessSlider->setRange(0, 100);
    brightnessSlider->setValue(qRound(previewBackgroundBrightness_ * 100.0));
    auto* brightnessLabel = new QLabel(QString::number(brightnessSlider->value()) + "%", brightnessRow);
    brightnessLabel->setMinimumWidth(44);
    brightnessRowLayout->addWidget(brightnessSlider, 1);
    brightnessRowLayout->addWidget(brightnessLabel);

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness", "Background / PV Brightness"), brightnessRow);

    auto* debugCheck = new QCheckBox(uiText("dialog.render_settings.video.debug", "Show preview debug info"), videoGroup);
    debugCheck->setChecked(previewShowDebugInfo_);
    videoFormLayout->addRow(QString(), debugCheck);

    rootLayout->addWidget(audioGroup, 0);
    rootLayout->addWidget(videoGroup, 0);
    rootLayout->addStretch(1);

    auto* buttonBox = new QDialogButtonBox(&dialog);
    buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    connect(bgmSlider, &QSlider::valueChanged, &dialog, [this, bgmLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBgmPercent(value);
        bgmLabel->setText(QString::number(previewAudioSettings_.bgmPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply(QString());
    });
    connect(answerSlider, &QSlider::valueChanged, &dialog, [this, answerLabel, queueAudioApply](int value) {
        previewAudioSettings_.setAnswerPercent(value);
        answerLabel->setText(QString::number(previewAudioSettings_.answerPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("answer");
    });
    connect(slideSlider, &QSlider::valueChanged, &dialog, [this, slideLabel, queueAudioApply](int value) {
        previewAudioSettings_.setSlidePercent(value);
        slideLabel->setText(QString::number(previewAudioSettings_.slidePercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("slide");
    });
    connect(breakSlider, &QSlider::valueChanged, &dialog, [this, breakLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBreakPercent(value);
        breakLabel->setText(QString::number(previewAudioSettings_.breakPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("break");
    });
    connect(exSlider, &QSlider::valueChanged, &dialog, [this, exLabel, queueAudioApply](int value) {
        previewAudioSettings_.setExPercent(value);
        exLabel->setText(QString::number(previewAudioSettings_.exPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("ex");
    });
    connect(touchSlider, &QSlider::valueChanged, &dialog, [this, touchLabel, queueAudioApply](int value) {
        previewAudioSettings_.setTouchPercent(value);
        touchLabel->setText(QString::number(previewAudioSettings_.touchPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("touch");
    });
    connect(touchholdSlider, &QSlider::valueChanged, &dialog, [this, touchholdLabel, queueAudioApply](int value) {
        previewAudioSettings_.setTouchholdPercent(value);
        touchholdLabel->setText(QString::number(previewAudioSettings_.touchholdPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("touchhold");
    });

    connect(restoreButton, &QPushButton::clicked, &dialog, [this, bgmSlider, answerSlider, slideSlider, breakSlider, exSlider, touchSlider, touchholdSlider]() {
        removeProjectRenderState();
        previewAudioSettings_ = softwarePreviewAudioSettings_;
        previewAudioSettings_.normalize();
        {
            QSignalBlocker b1(bgmSlider), b2(answerSlider), b3(slideSlider), b4(breakSlider), b5(exSlider), b6(touchSlider), b7(touchholdSlider);
            bgmSlider->setValue(previewAudioSettings_.bgmPercent());
            answerSlider->setValue(previewAudioSettings_.answerPercent());
            slideSlider->setValue(previewAudioSettings_.slidePercent());
            breakSlider->setValue(previewAudioSettings_.breakPercent());
            exSlider->setValue(previewAudioSettings_.exPercent());
            touchSlider->setValue(previewAudioSettings_.touchPercent());
            touchholdSlider->setValue(previewAudioSettings_.touchholdPercent());
        }
        applyPreviewAudioSettingsToRuntime();
        sendPreviewConfigCommand();
        statusBar()->showMessage(uiText("status.audio_restored_default", "Project audio restored to software defaults."));
    });

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, bgmSlider, answerSlider, slideSlider, breakSlider, exSlider, touchSlider, touchholdSlider, &pendingAudition]() {
        if (bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || touchholdSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        if (!pendingAudition.isEmpty()) {
            ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        sendPreviewConfigCommand(handledLocally ? QString() : pendingAudition);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        if (!pendingAudition.isEmpty()) {
            ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        sendPreviewConfigCommand(handledLocally ? QString() : pendingAudition);
        pendingAudition.clear();
    });

    connect(brightnessSlider, &QSlider::valueChanged, &dialog, [this, brightnessLabel](int value) {
        previewBackgroundBrightness_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        brightnessLabel->setText(QString::number(value) + "%");
        if (previewMediaController_ != nullptr) {
            previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
        } else if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
        }
        savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowDebugInfo_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        }
        savePortableState();
    });

    dialog.exec();
}

bool MainWindow::ensurePreviewSessionStarted()
{
    if (previewProcess_ != nullptr && previewProcess_->state() == QProcess::Running) {
        return true;
    }

    const QString scriptPath = resolvePreviewSessionScriptPath();
    if (scriptPath.isEmpty()) {
        appendOutput("preview/session-start", "script path not found");
        QMessageBox::warning(
            this,
            "Preview Session",
            "Preview session script is not configured.\n"
            "Set MIACODE_PREVIEW_SESSION_SCRIPT to enable legacy preview."
        );
        return false;
    }

    if (previewProcess_ != nullptr) {
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
    }

    previewProcess_ = new QProcess(this);
    previewProcess_->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    previewProcess_->setProcessEnvironment(pythonProcessEnvironment());
    previewProcess_->setProgram("python");
    previewProcess_->setArguments(QStringList{scriptPath});
    appendOutput(
        "preview/session-start",
        QString("program=python script=%1 cwd=%2")
            .arg(scriptPath, previewProcess_->workingDirectory())
    );
#ifdef Q_OS_WIN
    previewProcess_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    previewStdoutBuffer_.clear();
    previewStderrBuffer_.clear();
    connect(previewProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        onPreviewProcessFinished(exitCode);
    });
    connect(previewProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStdoutBuffer_ += decodeProcessText(previewProcess_->readAllStandardOutput());
        int lineBreak = previewStdoutBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStdoutBuffer_.left(lineBreak).trimmed();
            previewStdoutBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                if (line.contains("[session] ready")) {
                    previewArrangeRetryCount_ = 0;
                    QTimer::singleShot(40, this, &MainWindow::arrangeWithPreviewWindow);
                    sendPreviewConfigCommand();
                }
                if (!handlePreviewSessionLine(line)) {
                    appendOutput("preview/session", line);
                }
            }
            lineBreak = previewStdoutBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::readyReadStandardError, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStderrBuffer_ += decodeProcessText(previewProcess_->readAllStandardError());
        int lineBreak = previewStderrBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStderrBuffer_.left(lineBreak).trimmed();
            previewStderrBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                appendOutput("preview/session-stderr", line);
            }
            lineBreak = previewStderrBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendOutput(
            "preview/session-error",
            QString("process error code=%1 message=%2")
                .arg(static_cast<int>(error))
                .arg(previewProcess_ != nullptr ? previewProcess_->errorString() : QString("n/a"))
        );
    });

    previewProcess_->start();
    if (!previewProcess_->waitForStarted(3000)) {
        appendOutput(
            "preview/session-start-failed",
            QString("failed to start python preview session. error=%1")
                .arg(previewProcess_->errorString())
        );
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
        return false;
    }

    appendOutput("preview/session-start", QString("started pid=%1").arg(previewProcess_->processId()));
    statusBar()->showMessage("Preview session started.");
    return true;
}

void MainWindow::stopPreviewSession()
{
    if (previewProcess_ == nullptr) {
        return;
    }
    QProcess* process = previewProcess_;
    previewProcess_ = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::Running) {
        const QJsonObject quitCmd{{"cmd", "quit"}};
        QByteArray payload = QJsonDocument(quitCmd).toJson(QJsonDocument::Compact);
        payload.append('\n');
        process->write(payload);
        process->waitForBytesWritten(500);
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

bool MainWindow::sendPreviewCommand(const QString& mode, int cursorLine, int cursorCol, const QString& trackPath)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "preview"},
        {"mode", mode},
        {"track", trackPath},
        {"chart", activeChartText()},
        {"chart_name", document_.title.trimmed().isEmpty()
                ? (currentFilePath_.isEmpty() ? QString("Untitled") : QFileInfo(currentFilePath_).fileName())
                : document_.title},
        {"chart_path", currentFilePath_},
        {"volume", previewAudioSettings_.bgmVolume},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"render_profile", "studio"},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    if (mode == "cursor") {
        cmd.insert("cursor_line", cursorLine);
        cmd.insert("cursor_col", cursorCol);
    }

    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview command to session");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewPrepareCommand()
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    if (currentFilePath_.isEmpty()) {
        return false;
    }
    QJsonObject cmd{
        {"cmd", "prepare"},
        {"chart_path", currentFilePath_},
        {"render_profile", "studio"},
        {"background_brightness", previewBackgroundBrightness_},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview prepare command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewConfigCommand(const QString& audition)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "config"},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"background_brightness", previewBackgroundBrightness_},
    };
    if (!audition.isEmpty()) {
        cmd.insert("audition", audition);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview config command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::handlePreviewSessionLine(const QString& line)
{
    QString jsonText = line.trimmed();
    const int jsonStart = jsonText.indexOf('{');
    const int jsonEnd = jsonText.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart) {
        jsonText = jsonText.mid(jsonStart, jsonEnd - jsonStart + 1);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject payload = doc.object();
            if (payload.value("event").toString() == "playhead_limit") {
                if (timelineView_ != nullptr) {
                    const QJsonValue secondValue = payload.value("second");
                    if (secondValue.isDouble()) {
                        timelineView_->setPlayheadUpperLimitSeconds(secondValue.toDouble());
                    } else {
                        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
                    }
                }
                return true;
            }
        }
    }

    double second = 0.0;
    const PreviewIntegration::PlayheadParseResult parsed = PreviewIntegration::parsePlayheadEvent(line, &second);
    if (parsed == PreviewIntegration::PlayheadParseResult::NotPlayheadEvent) {
        return false;
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && timelineView_ != nullptr) {
        timelineView_->setPlayheadSeconds(second, true);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(second);
    }
    return true;
}

void MainWindow::startPreviewProcess(const QString& mode, int cursorLine, int cursorCol)
{
    if (!runValidateSimai()) {
        appendOutput("preview/blocked", "validation failed; preview canceled");
        statusBar()->showMessage("Preview canceled: fix validation errors first.");
        return;
    }

    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
    }
    QString trackPath = resolveDefaultTrackPath();
    if (trackPath.isEmpty()) {
        trackPath = QFileDialog::getOpenFileName(
            this,
            "Select Preview Track",
            QString(),
            "Audio (*.mp3 *.wav *.ogg);;All Files (*.*)"
        );
    }
    if (trackPath.isEmpty()) {
        return;
    }
    lastTrackPath_ = trackPath;

    if (!ensurePreviewSessionStarted()) {
        return;
    }
    if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
        appendOutput("preview/session", "retrying by restarting session process");
        stopPreviewSession();
        if (!ensurePreviewSessionStarted()) {
            return;
        }
        if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
            appendOutput("preview/session", "failed to send preview command after restart");
            return;
        }
    }

    statusBar()->showMessage(
        QString("Preview(%1) sent to resident session: %2").arg(mode).arg(QFileInfo(trackPath).fileName())
    );
    QTimer::singleShot(80, this, &MainWindow::arrangeWithPreviewWindow);
}

void MainWindow::onPreviewProcessFinished(int exitCode)
{
    if (previewProcess_ == nullptr) {
        return;
    }
    const QString restOut = decodeProcessText(previewProcess_->readAllStandardOutput()).trimmed();
    const QString restErr = decodeProcessText(previewProcess_->readAllStandardError()).trimmed();
    if (!restOut.isEmpty()) {
        appendOutput("preview/session", restOut);
    }
    if (!restErr.isEmpty()) {
        appendOutput("preview/session-stderr", restErr);
    }
    appendOutput("preview/session-exit", QString("exit_code=%1").arg(exitCode));
    statusBar()->showMessage("Preview session exited.");
    previewProcess_->deleteLater();
    previewProcess_ = nullptr;
}

void MainWindow::appendOutput(const QString& title, const QString& payload)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
    QFile logFile(runtimeDebugLogPath());
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << timestampLine(title) << "\n";
        out << payload << "\n\n";
    }
    if (outputView_ == nullptr) {
        return;
    }
    outputView_->appendPlainText(timestampLine(title));
    outputView_->appendPlainText(payload);
    outputView_->appendPlainText(QString());
}

void MainWindow::onErrorItemActivated(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const int line = item->data(Qt::UserRole).toInt();
    const int col = item->data(Qt::UserRole + 1).toInt();
    jumpToLocation(line, col);
}

bool MainWindow::runValidateSimai()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return false;
    }
    clearValidationErrors();
    clearValidationDecorations();

    const SimaiNativeParseResult nativeResult = SimaiNativeParser::validateSyntax(activeChartText());
    QString payload;
    payload += QString("note_count=%1\nsyntax_error_count=%2")
        .arg(nativeResult.noteMarkers.size())
        .arg(nativeResult.errors.size());
    appendOutput("validate", payload);

    for (const SimaiNativeMessage& err : nativeResult.errors) {
        addValidationError(err.line, err.col, err.message);
        addValidationDecoration(err.line, err.col, err.message);
    }
    if (!nativeResult.errors.isEmpty()) {
        onErrorItemActivated(errorList_->item(0));
    }

    if (nativeResult.errors.isEmpty()) {
        statusBar()->showMessage("Validate Simai passed.");
        return true;
    } else {
        statusBar()->showMessage(QString("Validate Simai failed: %1 syntax error(s).").arg(nativeResult.errors.size()));
    }
    return false;
}

bool MainWindow::saveBeforePreviewStart()
{
    if (documentDirty_ || currentFieldDirty_) {
        return onSaveFile();
    }
    return maybeSaveCurrentFieldChanges();
}

void MainWindow::onValidateSimai()
{
    (void)runValidateSimai();
}
