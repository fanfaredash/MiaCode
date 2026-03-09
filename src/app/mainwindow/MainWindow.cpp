#include "MainWindow.h"
#include "BracketScopeHighlighter.h"
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
#include <QComboBox>
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
#include <QShortcut>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSplitterHandle>
#include <QSpinBox>
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
constexpr int kEditorTextFontSizeMin = 8;
constexpr int kEditorTextFontSizeMax = 28;
constexpr double kEditorLineSpacingFactorDefault = 1.5;
const QList<double> kEditorLineSpacingFactorOptions{
    0.0, 1.0, 1.5, 2.0, 3.0, 5.0,
};

double normalizeEditorLineSpacingFactor(double factor)
{
    if (kEditorLineSpacingFactorOptions.isEmpty()) {
        return kEditorLineSpacingFactorDefault;
    }
    double best = kEditorLineSpacingFactorOptions.first();
    double bestDiff = qAbs(best - factor);
    for (double candidate : kEditorLineSpacingFactorOptions) {
        const double diff = qAbs(candidate - factor);
        if (diff < bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

QString editorLineSpacingFactorLabel(double factor)
{
    if (qFuzzyCompare(factor + 1.0, 1.0)) {
        return QStringLiteral("0x");
    }
    const QString text = QString::number(factor, 'f', qFuzzyCompare(factor, qRound(factor)) ? 0 : 1);
    return text + QStringLiteral("x");
}

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

QFont editorFont(int pointSize = -1)
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
    font.setPointSize(pointSize > 0 ? pointSize : 13);
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
    font.setPointSize(pointSize > 0 ? pointSize : 11);
    return font;
#endif
}

int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor)
{
    const int baseSpacing = qBound(1, qRound(static_cast<double>(pointSize) * 0.18), 6);
    return qMax(0, qRound(static_cast<double>(baseSpacing) * qMax(0.0, spacingFactor)));
}

void applyBlockSpacingToTextEdit(QTextEdit* editor, int blockSpacingPixels)
{
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }
    QSignalBlocker blocker(editor);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setBottomMargin(static_cast<qreal>(qMax(0, blockSpacingPixels)));
    cursor.mergeBlockFormat(fmt);
    cursor.endEditBlock();
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

#include "MainWindow.BootstrapAndMenus.cpp"
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

#include "MainWindow.DocumentFlow.cpp"
#include "MainWindow.PreviewTimelineFlow.cpp"
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

#include "MainWindow.EditorDisplay.cpp"
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

#include "MainWindow.PreferencesDialog.cpp"
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

#include "MainWindow.PreviewSessionFlow.cpp"

