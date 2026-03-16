#include "MainWindow.h"
#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "PlainCodeEditor.h"
#include "PreviewCanvas.h"
#include "PreviewIntegration.h"
#include "PreviewMediaController.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "simai/transform/ChartBatchTransform.h"
#include "tools/latency/LatencyDetectorDialog.h"
#include "tools/video_export/VideoExportDialog.h"
#include "tools/video_export/VideoExportController.h"
#include "common/AssetPaths.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"

#include <algorithm>
#include <QAction>
#include <QAbstractItemView>
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
#include <QEventLoop>
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
#include <QHideEvent>
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
#include <QMoveEvent>
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
#include <QWindowStateChangeEvent>
#include <QtMath>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif
#include <QVBoxLayout>
#include <QShowEvent>
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
constexpr qreal kPreviewSeekInitialStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekInitialStepSeconds);
constexpr qreal kPreviewSeekMaxStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekMaxStepSeconds);
constexpr qreal kPreviewSeekLinearAccelerationSecondsPerMs =
    static_cast<qreal>(miacode::preview_interaction::kSeekLinearAccelerationSecondsPerMs);
constexpr double kEditorLineSpacingFactorDefault = 1.5;
constexpr int kEditorFindBarMinWidth = 300;
constexpr int kEditorFindBarMaxWidth = 500;
constexpr int kEditorFindBarHorizontalMargin = 14;
constexpr int kEditorFindBarTopMargin = 10;
constexpr int kEditorFindBarOverlayGap = 8;
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

#ifdef Q_OS_WIN
QString sanitizeNativeDebugText(QString text)
{
    text.replace('\r', QStringLiteral("\\r"));
    text.replace('\n', QStringLiteral("\\n"));
    if (text.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    constexpr int kMaxLength = 96;
    if (text.size() > kMaxLength) {
        text = text.left(kMaxLength) + QStringLiteral("...");
    }
    return text;
}

QString describeNativeWindowHandle(HWND hwnd)
{
    if (hwnd == nullptr) {
        return QStringLiteral("hwnd=0x0");
    }

    wchar_t classNameBuf[256] = {};
    const int classNameLen = GetClassNameW(hwnd, classNameBuf, 256);
    const QString className = classNameLen > 0
        ? sanitizeNativeDebugText(QString::fromWCharArray(classNameBuf, classNameLen))
        : QStringLiteral("(none)");

    wchar_t titleBuf[512] = {};
    const int titleLen = GetWindowTextW(hwnd, titleBuf, 512);
    const QString title = titleLen > 0
        ? sanitizeNativeDebugText(QString::fromWCharArray(titleBuf, titleLen))
        : QStringLiteral("(empty)");

    RECT rect{};
    const BOOL hasRect = GetWindowRect(hwnd, &rect);
    const int rectX = hasRect ? rect.left : -1;
    const int rectY = hasRect ? rect.top : -1;
    const int rectW = hasRect ? (rect.right - rect.left) : -1;
    const int rectH = hasRect ? (rect.bottom - rect.top) : -1;

    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);
    const HWND lastPopup = GetLastActivePopup(hwnd);

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasPlacement = GetWindowPlacement(hwnd, &placement);
    const int showCmd = hasPlacement ? static_cast<int>(placement.showCmd) : -1;
    const int normalX = hasPlacement ? placement.rcNormalPosition.left : -1;
    const int normalY = hasPlacement ? placement.rcNormalPosition.top : -1;
    const int normalW = hasPlacement
        ? (placement.rcNormalPosition.right - placement.rcNormalPosition.left)
        : -1;
    const int normalH = hasPlacement
        ? (placement.rcNormalPosition.bottom - placement.rcNormalPosition.top)
        : -1;

    const auto style = static_cast<qulonglong>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto exStyle = static_cast<qulonglong>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    return QString(
               "hwnd=0x%1 class=%2 title=%3 vis=%4 ena=%5 iconic=%6 zoomed=%7 "
               "owner=0x%8 root=0x%9 root_owner=0x%10 popup=0x%11 pid=%12 tid=%13 "
               "rect=[%14,%15 %16x%17] show=%18 normal=[%19,%20 %21x%22] style=0x%23 ex=0x%24"
           )
        .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
        .arg(className)
        .arg(title)
        .arg(IsWindowVisible(hwnd) ? 1 : 0)
        .arg(IsWindowEnabled(hwnd) ? 1 : 0)
        .arg(IsIconic(hwnd) ? 1 : 0)
        .arg(IsZoomed(hwnd) ? 1 : 0)
        .arg(reinterpret_cast<quintptr>(owner), 0, 16)
        .arg(reinterpret_cast<quintptr>(root), 0, 16)
        .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
        .arg(reinterpret_cast<quintptr>(lastPopup), 0, 16)
        .arg(pid)
        .arg(tid)
        .arg(rectX)
        .arg(rectY)
        .arg(rectW)
        .arg(rectH)
        .arg(showCmd)
        .arg(normalX)
        .arg(normalY)
        .arg(normalW)
        .arg(normalH)
        .arg(style, 0, 16)
        .arg(exStyle, 0, 16);
}

bool tryRestoreOwnedNativeFileDialog(HWND ownerHwnd, QString* detailOut)
{
    if (ownerHwnd == nullptr) {
        return false;
    }

    const HWND foregroundHwnd = GetForegroundWindow();
    if (foregroundHwnd == nullptr || foregroundHwnd == ownerHwnd) {
        return false;
    }

    wchar_t classNameBuf[64] = {};
    const int classNameLen = GetClassNameW(foregroundHwnd, classNameBuf, 64);
    if (classNameLen <= 0 || QString::fromWCharArray(classNameBuf, classNameLen) != QStringLiteral("#32770")) {
        return false;
    }

    const HWND owner = GetWindow(foregroundHwnd, GW_OWNER);
    const HWND rootOwner = GetAncestor(foregroundHwnd, GA_ROOTOWNER);
    if (owner != ownerHwnd && rootOwner != ownerHwnd) {
        return false;
    }
    if (!IsZoomed(foregroundHwnd)) {
        return false;
    }

    WINDOWPLACEMENT before{};
    before.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasBefore = GetWindowPlacement(foregroundHwnd, &before);
    ShowWindow(foregroundHwnd, SW_RESTORE);
    WINDOWPLACEMENT after{};
    after.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasAfter = GetWindowPlacement(foregroundHwnd, &after);

    if (detailOut != nullptr) {
        *detailOut = QString("restore hwnd=0x%1 owner=0x%2 root_owner=0x%3 before_show=%4 after_show=%5")
                         .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                         .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                         .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
                         .arg(hasBefore ? static_cast<int>(before.showCmd) : -1)
                         .arg(hasAfter ? static_cast<int>(after.showCmd) : -1);
    }
    return true;
}
#endif

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
        const UiTheme::Colors& c = UiTheme::colors();
        const QColor selectedBorder = c.dark ? QColor("#6B8BB8") : QColor("#9EC2EF");
        const QColor selectedFill = c.dark ? QColor("#314158") : QColor("#F1F6FF");
        const QColor hoverFill = c.dark ? QColor("#2A3442") : QColor("#F3F7FD");

        painter->save();
        const QRect fillRect = option.rect.adjusted(1, 1, -1, -1);
        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(selectedBorder, 1.0));
            painter->setBrush(selectedFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }
        painter->restore();

        drawOption.state &= ~QStyle::State_Selected;
        drawOption.state &= ~QStyle::State_MouseOver;
        drawOption.backgroundBrush = Qt::NoBrush;
        drawOption.palette.setColor(QPalette::HighlightedText, c.textPrimary);
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
        signature.append('|');
        signature.append(marker.isFirework ? '1' : '0');
        signature.append(';');
    }
    return signature;
}

int difficultyIdFromCliToken(const QString& rawToken)
{
    const QString token = rawToken.trimmed().toUpper();
    if (token.isEmpty()) {
        return 0;
    }
    bool numericOk = false;
    const int numericId = token.toInt(&numericOk);
    if (numericOk && SimaiDocument::isDifficultyId(numericId)) {
        return numericId;
    }
    for (int id = 1; id <= 7; ++id) {
        if (SimaiDocument::difficultyShortName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
        if (SimaiDocument::difficultyName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
    }
    return 0;
}

QString resolveChartPathFromCliInput(const QString& inputPath)
{
    const QString cleaned = QDir::cleanPath(inputPath.trimmed());
    if (cleaned.isEmpty()) {
        return QString();
    }

    const QFileInfo info(cleaned);
    if (info.isFile()) {
        return info.absoluteFilePath();
    }
    if (!info.isDir()) {
        return QString();
    }

    const QDir dir(info.absoluteFilePath());
    const QStringList preferredNames{
        QStringLiteral("maidata.txt"),
        QStringLiteral("maidata.simai"),
        QStringLiteral("chart.txt"),
        QStringLiteral("chart.simai"),
    };
    for (const QString& name : preferredNames) {
        const QString candidate = dir.filePath(name);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    QStringList filters;
    filters << QStringLiteral("*.simai") << QStringLiteral("*.txt");
    const QStringList files = dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Name);
    if (!files.isEmpty()) {
        return QDir::cleanPath(dir.filePath(files.constFirst()));
    }
    return QString();
}

QString readTextFileWithFallbackEncoding(const QString& path, bool* usedSystemEncoding)
{
    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }

    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString utf8Text = utf8Decoder.decode(bytes);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }

    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = true;
    }
    QStringDecoder systemDecoder(QStringConverter::System);
    return systemDecoder.decode(bytes);
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
    return UiTheme::scrollBarStyleSheet();
}

void styleRoundedMenu(QMenu& menu)
{
    UiTheme::styleRoundedMenu(menu);
}

}  // namespace

void MainWindow::applyUiTheme()
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        UiTheme::applyApplicationTheme(*app);
    }

    if (editorWidget_ != nullptr) {
        editorWidget_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
            if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
                vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
            if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
                hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
        }
    }
    if (editorFindBar_ != nullptr) {
        editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    }
    if (metadataPage_ != nullptr) {
        metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (metadataEmptyHintLabel_ != nullptr) {
        metadataEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (metadataExtraEdit_ != nullptr) {
        if (QScrollBar* vbar = metadataExtraEdit_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = metadataExtraEdit_->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    if (outlineList_ != nullptr) {
        outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    }
    if (deleteDifficultyButton_ != nullptr) {
        deleteDifficultyButton_->setStyleSheet(UiTheme::deleteDifficultyButtonStyleSheet());
        deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().iconSecondary));
    }
    if (previewPanel_ != nullptr) {
        previewPanel_->setStyleSheet(UiTheme::previewPanelStyleSheet());
    }
    if (timelineView_ != nullptr) {
        timelineView_->refreshTheme();
    }
    if (chartBracketHighlighter_ != nullptr) {
        chartBracketHighlighter_->rehighlight();
    }
    if (metadataBracketHighlighter_ != nullptr) {
        metadataBracketHighlighter_->rehighlight();
    }
    if (QWidget* editorShell = findChild<QWidget*>(QStringLiteral("EditorShell")); editorShell != nullptr) {
        editorShell->setStyleSheet(UiTheme::editorShellStyleSheet());
    }
    const QList<QMenu*> menus = findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        if (menu != nullptr) {
            UiTheme::styleRoundedMenu(*menu);
        }
    }

    const QColor iconColor = UiTheme::colors().iconPrimary;
    const QColor secondaryIconColor = UiTheme::colors().iconSecondary;
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setIcon(makePreviewStopIcon(iconColor));
    }
    if (settingsPlaceholderAction_ != nullptr) {
        settingsPlaceholderAction_->setIcon(makeSettingsGearIcon(secondaryIconColor));
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (syntaxCheckButton_ != nullptr) {
        syntaxCheckButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (exportVideoButton_ != nullptr) {
        exportVideoButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    updatePauseButtonAppearance();
    update();
}

namespace {

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
        &PreviewCanvas::setBackgroundBrightnessOuter
    );
    connect(previewMediaController_, &PreviewMediaController::playbackPositionChanged, this, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        syncPausedPreviewMediaTimestamps(second);
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
    previewMediaController_->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
    }
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

QString MainWindow::formatWindowStateFlags(Qt::WindowStates states) const
{
    QStringList flags;
    if (states.testFlag(Qt::WindowMinimized)) {
        flags.append("minimized");
    }
    if (states.testFlag(Qt::WindowMaximized)) {
        flags.append("maximized");
    }
    if (states.testFlag(Qt::WindowFullScreen)) {
        flags.append("fullscreen");
    }
    if (states.testFlag(Qt::WindowActive)) {
        flags.append("active");
    }
    if (flags.isEmpty()) {
        flags.append("normal");
    }
    return flags.join('|');
}

void MainWindow::logWindowGeometryDebug(const QString& tag, const QString& detail)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }

    const QRect clientRect = geometry();
    const QRect frameRect = frameGeometry();
    const Qt::WindowStates states = windowState();

    QString payload = QString(
        "seq=%1 tag=%2 geom=[%3,%4 %5x%6] frame=[%7,%8 %9x%10] state=%11 "
        "active=%12 visible=%13 minimized=%14 maximized=%15 fullscreen=%16 "
        "suspend_depth=%17 arrange_gen=%18 arrange_retry=%19"
    )
        .arg(++windowEventDebugSequence_)
        .arg(tag)
        .arg(clientRect.left())
        .arg(clientRect.top())
        .arg(clientRect.width())
        .arg(clientRect.height())
        .arg(frameRect.left())
        .arg(frameRect.top())
        .arg(frameRect.width())
        .arg(frameRect.height())
        .arg(formatWindowStateFlags(states))
        .arg(isActiveWindow() ? 1 : 0)
        .arg(isVisible() ? 1 : 0)
        .arg(isMinimized() ? 1 : 0)
        .arg(isMaximized() ? 1 : 0)
        .arg(isFullScreen() ? 1 : 0)
        .arg(0)
        .arg(previewArrangeGeneration_)
        .arg(previewArrangeRetryCount_);

    if (!detail.isEmpty()) {
        payload += " detail=" + detail;
    }

#ifdef Q_OS_WIN
    const HWND selfHwnd = reinterpret_cast<HWND>(winId());
    const HWND foregroundHwnd = GetForegroundWindow();
    const HWND foregroundOwner = foregroundHwnd != nullptr ? GetWindow(foregroundHwnd, GW_OWNER) : nullptr;
    const HWND foregroundRootOwner = foregroundHwnd != nullptr ? GetAncestor(foregroundHwnd, GA_ROOTOWNER) : nullptr;
    payload += QString(" self=0x%1 fg=0x%2 fg_owner=0x%3 fg_root_owner=0x%4 zoomed=%5 iconic=%6")
                   .arg(reinterpret_cast<quintptr>(selfHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundOwner), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundRootOwner), 0, 16)
                   .arg(selfHwnd != nullptr ? (IsZoomed(selfHwnd) ? 1 : 0) : -1)
                   .arg(selfHwnd != nullptr ? (IsIconic(selfHwnd) ? 1 : 0) : -1);
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    if (selfHwnd != nullptr && GetWindowPlacement(selfHwnd, &placement)) {
        payload += QString(" wp_show=%1 wp_normal=[%2,%3 %4x%5]")
                       .arg(static_cast<int>(placement.showCmd))
                       .arg(placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.top)
                       .arg(placement.rcNormalPosition.right - placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top);
    } else {
        payload += " wp_show=-1";
    }
#endif

    appendOutput("window/event", payload);
}

void MainWindow::logTopLevelWindowSnapshot(const QString& tag)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }

    QStringList lines;
    const auto topLevels = QApplication::topLevelWidgets();
    lines.reserve(topLevels.size() + 1);
    lines.append(QString("tag=%1 count=%2").arg(tag).arg(topLevels.size()));
    int index = 0;
    for (QWidget* window : topLevels) {
        if (window == nullptr) {
            continue;
        }
        const QRect geom = window->geometry();
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const HWND owner = hwnd != nullptr ? GetWindow(hwnd, GW_OWNER) : nullptr;
        const HWND rootOwner = hwnd != nullptr ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr;
        const QString nativeDetail = QString(" wid=0x%1 owner=0x%2 root_owner=0x%3 zoomed=%4 iconic=%5")
                                         .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
                                         .arg(hwnd != nullptr ? (IsZoomed(hwnd) ? 1 : 0) : -1)
                                         .arg(hwnd != nullptr ? (IsIconic(hwnd) ? 1 : 0) : -1);
#else
        const QString nativeDetail;
#endif
        lines.append(
            QString("[%1] class=%2 title=%3 vis=%4 active=%5 modal=%6 state=%7 geom=[%8,%9 %10x%11]%12")
                .arg(index++)
                .arg(window->metaObject() != nullptr ? window->metaObject()->className() : "unknown")
                .arg(window->windowTitle().isEmpty() ? "(empty)" : window->windowTitle())
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->isActiveWindow() ? 1 : 0)
                .arg(window->isModal() ? 1 : 0)
                .arg(formatWindowStateFlags(window->windowState()))
                .arg(geom.left())
                .arg(geom.top())
                .arg(geom.width())
                .arg(geom.height())
                .arg(nativeDetail)
        );
    }
    appendOutput("window/top_levels", lines.join('\n'));
}

void MainWindow::logNativeWindowDebug(const QString& tag, WId dialogWId)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
#ifdef Q_OS_WIN
    const HWND selfHwnd = reinterpret_cast<HWND>(winId());
    const HWND foregroundHwnd = GetForegroundWindow();
    const HWND activeHwnd = GetActiveWindow();
    const HWND focusHwnd = GetFocus();

    QString payload = QString("tag=%1 self={%2} fg={%3} active={%4} focus={%5}")
                          .arg(tag)
                          .arg(describeNativeWindowHandle(selfHwnd))
                          .arg(describeNativeWindowHandle(foregroundHwnd))
                          .arg(describeNativeWindowHandle(activeHwnd))
                          .arg(describeNativeWindowHandle(focusHwnd));

    if (dialogWId != 0) {
        const HWND dialogHwnd = reinterpret_cast<HWND>(dialogWId);
        payload += QString(" dialog={%1}").arg(describeNativeWindowHandle(dialogHwnd));
    }

    GUITHREADINFO guiInfo{};
    guiInfo.cbSize = sizeof(GUITHREADINFO);
    if (GetGUIThreadInfo(0, &guiInfo)) {
        payload += QString(
                       " gui_active=0x%1 gui_focus=0x%2 gui_capture=0x%3 "
                       "gui_menu_owner=0x%4 gui_move_size=0x%5 gui_caret=0x%6 gui_flags=0x%7"
                   )
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndActive), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndFocus), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndCapture), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndMenuOwner), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndMoveSize), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndCaret), 0, 16)
                       .arg(static_cast<qulonglong>(guiInfo.flags), 0, 16);
    } else {
        payload += QString(" gui_info_err=%1").arg(GetLastError());
    }

    appendOutput("window/native", payload);
#else
    Q_UNUSED(tag);
    Q_UNUSED(dialogWId);
#endif
}

#include "sections/frame/MainWindow.BootstrapAndMenus.cpp"
void MainWindow::closeEvent(QCloseEvent* event)
{
    logWindowGeometryDebug("close_event_enter");
    if (maybeSaveBeforeContinue()) {
        savePortableState();
        stopPreviewSession();
        event->accept();
        logWindowGeometryDebug("close_event_accept");
    } else {
        event->ignore();
        logWindowGeometryDebug("close_event_ignore");
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
    const bool previewKeyScope =
        watched == previewSlider_
        || watched == previewCanvas_
        || watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewPanel_;
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
        }
    }
    if (previewKeyScope) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space
                && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                onTogglePreviewPause();
                return true;
            }
            if (previewSlider_ == nullptr) {
                return QMainWindow::eventFilter(watched, event);
            }
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (!keyEvent->isAutoRepeat() || previewSeekHeldArrowKey_ != keyEvent->key()) {
                    previewSeekHeldArrowKey_ = keyEvent->key();
                    previewSeekHeldArrowElapsed_.restart();
                } else if (!previewSeekHeldArrowElapsed_.isValid()) {
                    previewSeekHeldArrowElapsed_.restart();
                }

                const qreal heldMs = previewSeekHeldArrowElapsed_.isValid()
                    ? static_cast<qreal>(previewSeekHeldArrowElapsed_.elapsed())
                    : 0.0;
                const qreal acceleratedStep = qMin(
                    kPreviewSeekMaxStepSeconds,
                    kPreviewSeekInitialStepSeconds + (heldMs * kPreviewSeekLinearAccelerationSecondsPerMs)
                );
                const int deltaMs = direction * qRound(acceleratedStep * 1000.0);
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
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier) {
                return true;
            }
            if (previewSlider_ == nullptr) {
                return QMainWindow::eventFilter(watched, event);
            }
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                previewSeekHeldArrowKey_ = 0;
                previewSeekHeldArrowElapsed_.invalidate();
                return true;
            }
        }
    }
    if (watched == editorFindEdit_ || watched == editorReplaceEdit_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const bool ctrlOnly = (keyEvent->modifiers() & Qt::ControlModifier)
                && !(keyEvent->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
            if ((keyEvent->matches(QKeySequence::Find))
                || (ctrlOnly && keyEvent->key() == Qt::Key_F)) {
                onToggleFindReplace();
                return true;
            }
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const bool ctrlLeftClick = mouseEvent->button() == Qt::LeftButton
            && (mouseEvent->modifiers() & Qt::ControlModifier);
        if (ctrlLeftClick) {
            editorCtrlLeftJumpPending_ = true;
            editorCtrlLeftJumpDragged_ = false;
            editorCtrlLeftJumpPressPos_ = mouseEvent->pos();
        } else if (mouseEvent->button() == Qt::LeftButton) {
            editorCtrlLeftJumpPending_ = false;
            editorCtrlLeftJumpDragged_ = false;
        }
        if (mouseEvent->button() == Qt::LeftButton && !qtPreviewPlaying_ && !ctrlLeftClick) {
            QTimer::singleShot(0, this, [this]() {
                syncTimelineToEditorCursor(true);
            });
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseMove && editorCtrlLeftJumpPending_) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)
            && (mouseEvent->pos() - editorCtrlLeftJumpPressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            editorCtrlLeftJumpDragged_ = true;
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && editorCtrlLeftJumpPending_) {
            const bool shouldJump = !editorCtrlLeftJumpDragged_
                && (mouseEvent->modifiers() & Qt::ControlModifier);
            const QPoint releasePos = mouseEvent->pos();
            editorCtrlLeftJumpPending_ = false;
            editorCtrlLeftJumpDragged_ = false;
            if (shouldJump) {
                QTimer::singleShot(0, this, [this, releasePos]() {
                    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
                    if (editor == nullptr) {
                        return;
                    }
                    const QTextCursor cursor = editor->cursorForPosition(releasePos);
                    const int line = cursor.blockNumber() + 1;
                    const int col = cursor.positionInBlock() + 1;
                    const double second = timelineSecondForCursor(line, col);
                    if (second >= 0.0) {
                        if (qtPreviewPlaying_) {
                            stopQtPreviewPlayback(true);
                        }
                        schedulePreviewSeek(second, true);
                    } else {
                        seekTimelineToCursor(line, col);
                    }
                });
            }
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::FocusIn && !qtPreviewPlaying_) {
        QTimer::singleShot(0, this, [this]() {
            syncTimelineToEditorCursor(true);
        });
    }
    return QMainWindow::eventFilter(watched, event);
}

QTextEdit* MainWindow::activeFindTarget() const
{
    auto* chartEditor = qobject_cast<QTextEdit*>(editorWidget_);
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr) {
        if (chartEditor != nullptr && (focus == chartEditor || chartEditor->isAncestorOf(focus))) {
            return chartEditor;
        }
        if (metadataExtraEdit_ != nullptr && (focus == metadataExtraEdit_ || metadataExtraEdit_->isAncestorOf(focus))) {
            return metadataExtraEdit_;
        }
    }

    if (editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_ && chartEditor != nullptr) {
        return chartEditor;
    }
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && metadataExtraEdit_ != nullptr) {
        return metadataExtraEdit_;
    }
    return chartEditor != nullptr ? chartEditor : metadataExtraEdit_;
}

bool MainWindow::runFindInEditor(bool backward)
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr) {
        return false;
    }
    const QString pattern = editorFindEdit_->text();
    if (pattern.isEmpty()) {
        return false;
    }

    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (target->find(pattern, flags)) {
        return true;
    }

    QTextCursor resetCursor = target->textCursor();
    resetCursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    target->setTextCursor(resetCursor);
    return target->find(pattern, flags);
}

void MainWindow::updateEditorFindBarGeometry()
{
    if (editorFindBar_ == nullptr || editorStack_ == nullptr) {
        return;
    }
    const int availableWidth = qMax(0, editorStack_->width() - (kEditorFindBarHorizontalMargin * 2));
    if (availableWidth <= 0) {
        return;
    }
    int width = qMin(kEditorFindBarMaxWidth, availableWidth);
    if (availableWidth >= kEditorFindBarMinWidth) {
        width = qMax(kEditorFindBarMinWidth, width);
    }
    const int x = qMax(kEditorFindBarHorizontalMargin, editorStack_->width() - kEditorFindBarHorizontalMargin - width);
    const int y = kEditorFindBarTopMargin;
    const int height = editorFindBar_->sizeHint().height();
    editorFindBar_->setGeometry(x, y, width, height);
    editorFindBar_->raise();
}

void MainWindow::applyFindOverlayInset()
{
    const int topInset =
        (editorFindBar_ != nullptr && editorFindBar_->isVisible())
        ? editorFindBar_->height() + kEditorFindBarOverlayGap
        : 0;
    if (auto* plainEditor = qobject_cast<PlainCodeEditor*>(editorWidget_); plainEditor != nullptr) {
        plainEditor->setTopOverlayInsetPixels(topInset);
    }
}

void MainWindow::hideFindReplaceBar()
{
    if (editorFindBar_ == nullptr || !editorFindBar_->isVisible()) {
        return;
    }
    editorFindBar_->hide();
    applyFindOverlayInset();
    if (QTextEdit* target = activeFindTarget(); target != nullptr) {
        target->setFocus();
    }
}

void MainWindow::onToggleFindReplace()
{
    if (editorFindBar_ == nullptr) {
        return;
    }
    if (editorFindBar_->isVisible()) {
        hideFindReplaceBar();
        return;
    }

    updateEditorFindBarGeometry();
    editorFindBar_->show();
    editorFindBar_->raise();
    applyFindOverlayInset();
    QTextEdit* target = activeFindTarget();
    if (target != nullptr && editorFindEdit_ != nullptr && editorFindEdit_->text().isEmpty()) {
        const QTextCursor cursor = target->textCursor();
        const QString selected = cursor.selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            editorFindEdit_->setText(selected);
        }
    }
    if (editorFindEdit_ != nullptr) {
        editorFindEdit_->setFocus();
        editorFindEdit_->selectAll();
    }
}

void MainWindow::onFindNext()
{
    runFindInEditor(false);
}

void MainWindow::onFindPrevious()
{
    runFindInEditor(true);
}

void MainWindow::onReplaceOne()
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr || editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = editorFindEdit_->text();
    if (findText.isEmpty()) {
        return;
    }

    QTextCursor cursor = target->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
        cursor.insertText(editorReplaceEdit_->text());
        target->setTextCursor(cursor);
    }
    runFindInEditor(false);
}

void MainWindow::onReplaceAll()
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr || editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = editorFindEdit_->text();
    if (findText.isEmpty()) {
        return;
    }

    QTextDocument* doc = target->document();
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();
    const QString replaceText = editorReplaceEdit_->text();
    int replacedCount = 0;
    QTextCursor searchCursor = doc->find(findText, 0);
    while (true) {
        if (searchCursor.isNull()) {
            break;
        }
        searchCursor.insertText(replaceText);
        ++replacedCount;
        searchCursor = doc->find(findText, searchCursor);
    }
    editCursor.endEditBlock();
    statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已替换 %1 处。").arg(replacedCount)
            : QStringLiteral("Replaced %1 occurrence(s).").arg(replacedCount)
    );
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewWorkspaceLayout();
    updateEditorHeaderLayoutMode();
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
    logWindowGeometryDebug(
        "resize_event",
        QString("old=%1x%2 new=%3x%4")
            .arg(event->oldSize().width())
            .arg(event->oldSize().height())
            .arg(event->size().width())
            .arg(event->size().height())
    );
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    logWindowGeometryDebug(
        "move_event",
        QString("old=(%1,%2) new=(%3,%4)")
            .arg(event->oldPos().x())
            .arg(event->oldPos().y())
            .arg(event->pos().x())
            .arg(event->pos().y())
    );
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    logWindowGeometryDebug("show_event");
}

void MainWindow::hideEvent(QHideEvent* event)
{
    QMainWindow::hideEvent(event);
    logWindowGeometryDebug("hide_event");
}

void MainWindow::changeEvent(QEvent* event)
{
    const QEvent::Type type = event != nullptr ? event->type() : QEvent::None;
    QMainWindow::changeEvent(event);
    if (type == QEvent::WindowStateChange) {
        auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        logWindowGeometryDebug(
            "window_state_change",
            QString("old_state=%1 new_state=%2")
                .arg(formatWindowStateFlags(stateEvent != nullptr ? stateEvent->oldState() : Qt::WindowNoState))
                .arg(formatWindowStateFlags(windowState()))
        );
    } else if (type == QEvent::ActivationChange) {
        logWindowGeometryDebug("activation_change", QString("is_active=%1").arg(isActiveWindow() ? 1 : 0));
    } else if (type == QEvent::ZOrderChange) {
        logWindowGeometryDebug("zorder_change");
    }
}

#include "sections/document/MainWindow.DocumentFlow.cpp"
#include "sections/timeline/MainWindow.PreviewTimelineFlow.cpp"
#include "sections/validation/MainWindow.ValidationFlow.cpp"
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

#include "sections/editor/MainWindow.EditorDisplay.cpp"
void MainWindow::loadProjectRenderState()
{
    previewAudioSettings_ = softwarePreviewAudioSettings_;
    previewAudioSettings_.normalize();
    projectLastOpenedDifficultyId_ = 0;

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
    const QJsonObject render = root.value("render").toObject();
    if (!render.isEmpty()) {
        const double legacyBrightness = qBound(
            0.0,
            render.value("background_brightness").toDouble(previewBackgroundBrightnessOuter_),
            1.0
        );
        if (render.value("background_brightness_outer").isDouble()) {
            previewBackgroundBrightnessOuter_ =
                qBound(0.0, render.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
        } else {
            previewBackgroundBrightnessOuter_ = legacyBrightness;
        }
        if (render.value("background_brightness_inner").isDouble()) {
            previewBackgroundBrightnessInner_ =
                qBound(0.0, render.value("background_brightness_inner").toDouble(previewBackgroundBrightnessOuter_), 1.0);
        } else {
            previewBackgroundBrightnessInner_ = previewBackgroundBrightnessOuter_;
        }
        if (render.value("layout_square_scale").isDouble()) {
            previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
                render.value("layout_square_scale").toDouble(previewLayoutSquareScale_)
            );
        }
        if (render.value("smooth_brightness").isBool()) {
            previewSmoothBrightness_ = render.value("smooth_brightness").toBool(previewSmoothBrightness_);
        }
        const QString scaleMode = render.value("background_scale_mode").toString().trimmed().toLower();
        if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
            previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
        } else if (!scaleMode.isEmpty()) {
            previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
        }
        if (render.value("note_flow_speed").isDouble()) {
            previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
                render.value("note_flow_speed").toDouble(previewNoteFlowSpeed_)
            );
        }
        if (render.value("show_debug_info").isBool()) {
            previewShowDebugInfo_ = render.value("show_debug_info").toBool(previewShowDebugInfo_);
        }
        if (render.value("show_timestamp").isBool()) {
            previewShowTimestamp_ = render.value("show_timestamp").toBool(previewShowTimestamp_);
        }
        if (render.value("auto_restore_square_after_export").isBool()) {
            previewAutoRestoreSquareAfterExport_ =
                render.value("auto_restore_square_after_export").toBool(previewAutoRestoreSquareAfterExport_);
        }
        if (render.value("canvas_aspect_ratio").isDouble()) {
            setPreviewCanvasAspectRatio(render.value("canvas_aspect_ratio").toDouble(previewCanvasAspectRatio_), false);
        }
    }
    const int savedDifficultyId = root.value("last_opened_difficulty").toInt(0);
    if (SimaiDocument::isDifficultyId(savedDifficultyId)) {
        projectLastOpenedDifficultyId_ = savedDifficultyId;
    }
    previewAudioSettings_.normalize();
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
    }
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
    QJsonObject render;
    render.insert("background_brightness", previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_outer", previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_inner", previewBackgroundBrightnessInner_);
    render.insert("layout_square_scale", previewLayoutSquareScale_);
    render.insert("smooth_brightness", previewSmoothBrightness_);
    render.insert(
        "background_scale_mode",
        previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    render.insert("note_flow_speed", previewNoteFlowSpeed_);
    render.insert("show_debug_info", previewShowDebugInfo_);
    render.insert("show_timestamp", previewShowTimestamp_);
    render.insert("canvas_aspect_ratio", previewCanvasAspectRatio_);
    render.insert("auto_restore_square_after_export", previewAutoRestoreSquareAfterExport_);
    root.insert("render", render);
    root.insert("last_opened_difficulty", projectLastOpenedDifficultyId_);
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

void MainWindow::onToggleBreakSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Break", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleBreakForSelection(text, changedCount);
    });
}

void MainWindow::onToggleExSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle EX", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleExForSelection(text, changedCount);
    });
}

void MainWindow::onToggleFireworkSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Firework", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleFireworkForSelection(text, changedCount);
    });
}

void MainWindow::onRandomRotateSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Random Rotate", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::randomRotateForSelection(text, changedCount);
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
    schedulePreviewArrange(80);
}

void MainWindow::schedulePreviewArrange(int delayMs)
{
    const int safeDelay = qMax(0, delayMs);
    const quint64 generation = ++previewArrangeGeneration_;
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-schedule",
            QString("queue generation=%1 delay_ms=%2 retry=%3")
                .arg(generation)
                .arg(safeDelay)
                .arg(previewArrangeRetryCount_)
        );
        logNativeWindowDebug(
            QString("schedule_queue generation=%1 delay=%2 retry=%3")
                .arg(generation)
                .arg(safeDelay)
                .arg(previewArrangeRetryCount_)
        );
    }
    QTimer::singleShot(safeDelay, this, [this, generation, safeDelay]() {
        if (generation != previewArrangeGeneration_) {
            if (runtimeDebugOutputEnabled_) {
                appendOutput(
                    "preview/layout-schedule",
                    QString("drop generation_mismatch task=%1 latest=%2 delay_ms=%3")
                        .arg(generation)
                        .arg(previewArrangeGeneration_)
                        .arg(safeDelay)
                );
                logNativeWindowDebug(
                    QString("schedule_drop_generation task=%1 latest=%2 delay=%3")
                        .arg(generation)
                        .arg(previewArrangeGeneration_)
                        .arg(safeDelay)
                );
            }
            return;
        }
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-schedule",
                QString("execute generation=%1 delay_ms=%2").arg(generation).arg(safeDelay)
            );
            logNativeWindowDebug(QString("schedule_execute generation=%1 delay=%2").arg(generation).arg(safeDelay));
        }
        arrangeWithPreviewWindow();
    });
}

void MainWindow::arrangeWithPreviewWindow()
{
#ifdef Q_OS_WIN
    logWindowGeometryDebug("arrange_enter");
    logNativeWindowDebug("arrange_enter");
    if (QApplication::activeModalWidget() != nullptr) {
        logWindowGeometryDebug("arrange_skip_active_modal_widget");
        logNativeWindowDebug("arrange_skip_active_modal_widget");
        if (runtimeDebugOutputEnabled_) {
            appendOutput("preview/layout", "skip active_modal_widget");
        }
        return;
    }
    if (!isActiveWindow()) {
        logWindowGeometryDebug("arrange_skip_mainwindow_not_active");
        logNativeWindowDebug("arrange_skip_mainwindow_not_active");
        if (runtimeDebugOutputEnabled_) {
            appendOutput("preview/layout", "skip mainwindow_not_active");
        }
        return;
    }
    const HWND selfHwnd = reinterpret_cast<HWND>(winId());
    const HWND foregroundHwnd = GetForegroundWindow();
    if (foregroundHwnd != nullptr && foregroundHwnd != selfHwnd) {
        const HWND foregroundRootOwner = GetAncestor(foregroundHwnd, GA_ROOTOWNER);
        // Native/common dialogs may be wrapped in extra owner chains; use root owner
        // instead of one-hop GW_OWNER to detect ownership robustly.
        if (foregroundRootOwner == selfHwnd) {
            logWindowGeometryDebug("arrange_skip_foreground_owned_dialog");
            logNativeWindowDebug("arrange_skip_foreground_owned_dialog");
            if (runtimeDebugOutputEnabled_) {
                appendOutput(
                    "preview/layout",
                    QString("skip foreground_owned_dialog fg=0x%1 root_owner=0x%2")
                        .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                        .arg(reinterpret_cast<quintptr>(foregroundRootOwner), 0, 16)
                );
            }
            return;
        }
    }
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        logWindowGeometryDebug("arrange_skip_preview_process_not_running");
        logNativeWindowDebug("arrange_skip_preview_process_not_running");
        if (runtimeDebugOutputEnabled_) {
            appendOutput("preview/layout", "skip preview_process_not_running");
        }
        return;
    }
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

    QString detail;
    const qint64 pid = previewProcess_->processId();
    if (!PreviewIntegration::placePreviewWindow(pid, layout.previewRect, &detail)) {
        logWindowGeometryDebug("arrange_place_failed", detail);
        logNativeWindowDebug("arrange_place_failed");
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout",
                QString("place_failed pid=%1 retry=%2 detail=%3")
                    .arg(pid)
                    .arg(previewArrangeRetryCount_)
                    .arg(detail)
            );
        }
        if (previewArrangeRetryCount_ < 30) {
            ++previewArrangeRetryCount_;
            schedulePreviewArrange(120);
        } else {
            appendOutput("preview/layout", "preview window placement failed: " + detail);
        }
        return;
    }

    if (geometry() != editorRect) {
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout",
                QString("apply_editor_geometry rect=[%1,%2 %3x%4]")
                    .arg(editorRect.left())
                    .arg(editorRect.top())
                    .arg(editorRect.width())
                    .arg(editorRect.height())
            );
        }
        logNativeWindowDebug("arrange_before_set_geometry");
        setGeometry(editorRect);
        logWindowGeometryDebug(
            "arrange_after_set_geometry",
            QString("target=[%1,%2 %3x%4]")
                .arg(editorRect.left())
                .arg(editorRect.top())
                .arg(editorRect.width())
                .arg(editorRect.height())
        );
        logNativeWindowDebug("arrange_after_set_geometry");
    }

    if (previewArrangeRetryCount_ > 0) {
        appendOutput("preview/layout", QString("arranged after retry=%1 (%2)").arg(previewArrangeRetryCount_).arg(detail));
    } else {
        appendOutput("preview/layout", "arranged (" + detail + ")");
    }
    logWindowGeometryDebug("arrange_success", detail);
    logNativeWindowDebug("arrange_success");
    previewArrangeRetryCount_ = 0;
#endif
}

void MainWindow::onStopPreview()
{
    stopQtPreviewPlayback(false);
    seekPreviewToSecond(0.0, true);
    statusBar()->showMessage("Qt preview stopped.");
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
    openPreviewSettingsDialog(
        true,
        false,
        uiText("dialog.audio_settings.title", "Audio Settings")
    );
}

void MainWindow::onPreviewVideoSettings()
{
    openPreviewSettingsDialog(
        false,
        true,
        uiText("dialog.video_settings.title", "Video Settings")
    );
}

#include "sections/preferences/MainWindow.PreferencesDialog.cpp"
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
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName("AboutIcon");
    iconLabel->setFixedSize(64, 64);
    QPixmap appIcon = QIcon(":/icons/app.png").pixmap(48, 48);
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon);
        iconLabel->setAlignment(Qt::AlignCenter);
    }
    titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* titleTextCol = new QVBoxLayout();
    titleTextCol->setSpacing(4);
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    QString displayVersion = QString::fromLatin1(MIACODE_VERSION_STRING).trimmed();
    if (displayVersion.isEmpty()) {
        displayVersion = QCoreApplication::applicationVersion().trimmed();
    }
    if (displayVersion.isEmpty()) {
        displayVersion = QStringLiteral("0.0.0");
    }
    auto* versionLabel = new QLabel(QStringLiteral("v%1").arg(displayVersion), card);
    versionLabel->setObjectName("AboutVersion");
    titleTextCol->addWidget(titleLabel, 0, Qt::AlignLeft);
    titleTextCol->addWidget(versionLabel, 0, Qt::AlignLeft);
    titleRow->addLayout(titleTextCol, 0);
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

void MainWindow::onExportPreviewVideo()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage(QStringLiteral("当前未选中难度，无法导出视频。"));
        return;
    }
    if (previewCanvas_ == nullptr) {
        statusBar()->showMessage(QStringLiteral("预览画布未初始化，无法导出视频。"));
        return;
    }
    if (!legacyPygamePreviewEnabled_ && qtPreviewPlaying_) {
        onTogglePreviewPause();
    }

    refreshTimelineMetadata();

    VideoExportTask task;
    task.chartPath = currentFilePath_;
    task.trackPath = resolveDefaultTrackPath();
    task.noteMarkers = previewStatsNoteMarkers_;
    task.audioSettings = previewAudioSettings_;
    task.backgroundBrightnessOuter = previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = previewBackgroundBrightnessInner_;
    task.layoutSquareScale = previewLayoutSquareScale_;
    task.smoothBrightness = previewSmoothBrightness_;
    task.backgroundScaleMode = previewBackgroundScaleMode_;
    task.noteFlowSpeed = previewNoteFlowSpeed_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = qMax(0.0, previewDurationSeconds());
    const double currentAspect = normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
    if (qAbs(currentAspect - (16.0 / 9.0)) < 0.05) {
        task.outputWidth = 1280;
        task.outputHeight = 720;
    } else if (qAbs(currentAspect - (4.0 / 3.0)) < 0.05) {
        task.outputWidth = 1024;
        task.outputHeight = 768;
    } else {
        task.outputWidth = 1024;
        task.outputHeight = 1024;
    }
    task.fps = 60;
    task.showTimestamp = previewShowTimestamp_;

    const QFileInfo chartInfo(currentFilePath_);
    const QString difficultyName = hasActiveDifficulty()
        ? SimaiDocument::difficultyShortName(activeDifficultyId_).replace(':', '_')
        : QStringLiteral("chart");
    const QString outputName = QString("%1_%2_preview.mp4")
        .arg(chartInfo.completeBaseName().isEmpty() ? QStringLiteral("export") : chartInfo.completeBaseName())
        .arg(difficultyName);
    task.outputPath = chartInfo.absoluteDir().filePath(outputName);

    const auto currentPreviewSecond = [this]() -> double {
        double second = qMax(0.0, qtPreviewPauseSecond_);
        if (qtPreviewPlaying_) {
            if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
                second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
            } else if (previewMediaController_ != nullptr) {
                second = qMax(0.0, previewMediaController_->currentPlaybackSecond());
            }
        }
        return second;
    };

    VideoExportDialog dialog(
        task,
        previewCanvas_,
        [this](double second) {
            seekPreviewToSecond(second, false);
        },
        [this](double second) {
            if (legacyPygamePreviewEnabled_) {
                return;
            }
            startQtPreviewPlayback(second, true);
            updatePauseButtonAppearance();
        },
        [this]() {
            if (legacyPygamePreviewEnabled_) {
                return;
            }
            if (qtPreviewPlaying_) {
                stopQtPreviewPlayback(true);
                updatePauseButtonAppearance();
            }
        },
        [this]() -> bool {
            return !legacyPygamePreviewEnabled_ && qtPreviewPlaying_;
        },
        currentPreviewSecond,
        [this](double ratio) {
            setPreviewCanvasAspectRatio(ratio, false);
        },
        [this](double outer, double inner) {
            previewBackgroundBrightnessOuter_ = qBound(0.0, outer, 1.0);
            previewBackgroundBrightnessInner_ = qBound(0.0, inner, 1.0);
            if (previewMediaController_ != nullptr) {
                previewMediaController_->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
                previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](double scale) {
            previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(scale);
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](bool smooth) {
            previewSmoothBrightness_ = smooth;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](PreviewBackgroundScaleMode mode) {
            previewBackgroundScaleMode_ = mode;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](double flowSpeed) {
            previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        this
    );

    dialog.adjustSize();
    QRect anchorRect = geometry();
    bool hasAnchor = false;
    auto mergeGlobalRect = [&anchorRect, &hasAnchor](const QWidget* widget) {
        if (widget == nullptr || !widget->isVisible()) {
            return;
        }
        const QRect local = widget->rect();
        const QRect global(widget->mapToGlobal(local.topLeft()), local.size());
        if (!hasAnchor) {
            anchorRect = global;
            hasAnchor = true;
            return;
        }
        anchorRect = anchorRect.united(global);
    };
    mergeGlobalRect(outlineList_);
    mergeGlobalRect(previewLeftColumn_);
    if (!hasAnchor && workspaceSplitter_ != nullptr && previewPanel_ != nullptr && previewPanel_->isVisible()) {
        const QRect splitterRect = workspaceSplitter_->rect();
        const QRect previewRect = previewPanel_->geometry();
        const int leftWidth = qMax(1, previewRect.left());
        const QRect localLeftArea(0, 0, leftWidth, splitterRect.height());
        anchorRect = QRect(workspaceSplitter_->mapToGlobal(localLeftArea.topLeft()), localLeftArea.size());
    }
    if (hasAnchor) {
        const int preferredWidth = qRound(anchorRect.width() * 0.5);
        dialog.resize(qMax(dialog.minimumWidth(), preferredWidth), dialog.height());
    }
    QPoint targetTopLeft(
        anchorRect.center().x() - dialog.width() / 2,
        anchorRect.center().y() - dialog.height() / 2
    );
    QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (targetScreen == nullptr && windowHandle() != nullptr) {
        targetScreen = windowHandle()->screen();
    }
    if (targetScreen != nullptr) {
        const QRect avail = targetScreen->availableGeometry();
        targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog.width() + 1));
        targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog.height() + 1));
    }
    dialog.move(targetTopLeft);
    dialog.exec();
    if (previewAutoRestoreSquareAfterExport_
        && (dialog.exportSucceeded() || dialog.previewAspectChangedByDialog())) {
        setPreviewCanvasAspectRatio(1.0, false);
    }
}

bool MainWindow::exportPreviewVideoFromCli(
    const CliVideoExportRequest& request,
    QString* resolvedOutputPath,
    QString* errorMessage,
    QString* details
)
{
    if (resolvedOutputPath != nullptr) {
        resolvedOutputPath->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (details != nullptr) {
        details->clear();
    }

    const auto fail = [errorMessage, details](const QString& message, const QString& detail = QString()) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        if (details != nullptr) {
            *details = detail;
        }
        return false;
    };

    if (previewCanvas_ == nullptr) {
        return fail(QStringLiteral("preview canvas is not initialized"));
    }
    if (request.outputWidth <= 0 || request.outputHeight <= 0 || request.fps <= 0) {
        return fail(QStringLiteral("output width/height and fps must be positive integers"));
    }
    if (request.outputWidth < request.outputHeight) {
        return fail(QStringLiteral("output size currently requires width >= height"));
    }

    const QString chartPath = resolveChartPathFromCliInput(request.chartPathOrDirectory);
    if (chartPath.isEmpty()) {
        return fail(
            QStringLiteral("cannot resolve chart file from input path"),
            request.chartPathOrDirectory
        );
    }

    bool usedSystemEncoding = false;
    const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
    if (chartText.isNull()) {
        return fail(QStringLiteral("failed to read chart file"), chartPath);
    }

    setCurrentFilePath(chartPath);
    loadDocument(SimaiDocument::fromText(chartText));
    refreshWaveformCache();

    const int difficultyId = difficultyIdFromCliToken(request.difficulty);
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return fail(
            QStringLiteral("invalid difficulty token"),
            QStringLiteral("expected one of: ESY/BAS/ADV/EXP/MAS/REM/UTG or 1..7")
        );
    }

    if (document_.difficulty(difficultyId) == nullptr) {
        QStringList available;
        const QVector<int> ids = document_.difficultyIds();
        available.reserve(ids.size());
        for (int id : ids) {
            available.append(SimaiDocument::difficultyShortName(id));
        }
        return fail(
            QStringLiteral("requested difficulty is missing in chart"),
            QStringLiteral("requested=%1 available=%2")
                .arg(SimaiDocument::difficultyShortName(difficultyId))
                .arg(available.join(','))
        );
    }
    if (!switchToDifficultyField(difficultyId)) {
        return fail(QStringLiteral("failed to switch to requested difficulty"));
    }

    refreshTimelineMetadata();
    if (previewStatsNoteMarkers_.isEmpty()) {
        return fail(QStringLiteral("no parsed note markers for requested difficulty"));
    }

    bool skinLoaded = previewCanvas_->hasCoreSkinAssetsLoadedForDebug();
    const int skinWaitMs = qBound(0, request.skinLoadWaitMs, 20000);
    if (!skinLoaded && skinWaitMs > 0) {
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!skinLoaded && waitTimer.elapsed() < skinWaitMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            skinLoaded = previewCanvas_->hasCoreSkinAssetsLoadedForDebug();
        }
    }

    const QFileInfo chartInfo(currentFilePath_);
    const QString difficultyName = SimaiDocument::difficultyShortName(difficultyId).replace(':', '_');
    const QString defaultOutputName = QString("%1_%2_preview.mp4")
        .arg(chartInfo.completeBaseName().isEmpty() ? QStringLiteral("export") : chartInfo.completeBaseName())
        .arg(difficultyName);

    QString outputPath = request.outputPath.trimmed();
    if (outputPath.isEmpty()) {
        outputPath = chartInfo.absoluteDir().filePath(defaultOutputName);
    } else {
        const bool trailingSeparator = outputPath.endsWith('/') || outputPath.endsWith('\\');
        const QFileInfo outputInfo(outputPath);
        if ((outputInfo.exists() && outputInfo.isDir()) || trailingSeparator) {
            const QString outputDirPath = outputInfo.exists() && outputInfo.isDir()
                ? outputInfo.absoluteFilePath()
                : outputPath;
            outputPath = QDir(outputDirPath).filePath(defaultOutputName);
        }
    }
    outputPath = QDir::cleanPath(outputPath);

    const QFileInfo outputInfo(outputPath);
    const QString outputDirPath = outputInfo.absolutePath();
    if (!outputDirPath.isEmpty() && !QDir(outputDirPath).exists()) {
        if (!QDir().mkpath(outputDirPath)) {
            return fail(QStringLiteral("cannot create output directory"), outputDirPath);
        }
    }

    const double exportStartSeconds = qMax(0.0, request.exportStartSeconds);
    const double totalDuration = previewDurationSeconds();
    const double maxDuration = qMax(0.0, totalDuration - exportStartSeconds);
    const double contentDurationSeconds = request.contentDurationSeconds > 0.0
        ? request.contentDurationSeconds
        : maxDuration;
    if (contentDurationSeconds <= 0.0) {
        return fail(
            QStringLiteral("content duration is not positive"),
            QStringLiteral("start=%1 total=%2")
                .arg(exportStartSeconds, 0, 'f', 3)
                .arg(totalDuration, 0, 'f', 3)
        );
    }

    VideoExportTask task;
    task.chartPath = currentFilePath_;
    task.trackPath = resolveDefaultTrackPath();
    task.noteMarkers = previewStatsNoteMarkers_;
    task.audioSettings = previewAudioSettings_;
    task.backgroundBrightnessOuter = previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = previewBackgroundBrightnessInner_;
    task.layoutSquareScale = previewLayoutSquareScale_;
    task.smoothBrightness = previewSmoothBrightness_;
    task.backgroundScaleMode = previewBackgroundScaleMode_;
    task.noteFlowSpeed = previewNoteFlowSpeed_;
    task.exportStartSeconds = exportStartSeconds;
    task.contentDurationSeconds = contentDurationSeconds;
    task.outputWidth = request.outputWidth;
    task.outputHeight = request.outputHeight;
    task.fps = request.fps;
    task.showTimestamp = request.showTimestamp;
    task.outputPath = outputPath;

    const VideoExportResult exportResult = VideoExportController::exportFullPreview(task, previewCanvas_, nullptr);
    if (!exportResult.success) {
        return fail(exportResult.message, exportResult.details);
    }

    if (resolvedOutputPath != nullptr) {
        *resolvedOutputPath = outputPath;
    }
    if (details != nullptr) {
        QStringList detailLines;
        detailLines << QStringLiteral("chart=%1").arg(chartPath);
        detailLines << QStringLiteral("difficulty=%1").arg(SimaiDocument::difficultyShortName(difficultyId));
        detailLines << QStringLiteral("encoding=%1").arg(usedSystemEncoding ? QStringLiteral("system") : QStringLiteral("utf8"));
        detailLines << QStringLiteral("noteCount=%1").arg(previewStatsNoteMarkers_.size());
        detailLines << QStringLiteral("trackPath=%1").arg(task.trackPath.isEmpty() ? QStringLiteral("(none)") : task.trackPath);
        detailLines << QStringLiteral("skinLoaded=%1").arg(skinLoaded ? 1 : 0);
        *details = detailLines.join('\n');
    }
    return true;
}

void MainWindow::onOpenLatencyDetector()
{
    const QString trackPath = resolveLatencyDetectorTrackPath();
    bool wholeBpmOk = false;
    const double wholeBpm = parsedWholeBpm(&wholeBpmOk);
    const QString meterId = parsedLatencyMeterId();
    const double offsetSeconds = parsedFirstSeconds();
    if (trackPath.isEmpty()) {
        statusBar()->showMessage(UiText::isChineseUi()
            ? QStringLiteral("当前谱面目录缺少 track.mp3，无法打开BPM&偏移检测。")
            : QStringLiteral("track.mp3 was not found next to the current chart."));
        updateLatencyDetectorAvailability();
        return;
    }

    if (latencyDetectorDialog_ != nullptr) {
        if (latencyDetectorDialog_->trackPath() == trackPath) {
            latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
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
    latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
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

void MainWindow::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    previewAudioSettings_.normalize();
    if (legacyPygamePreviewEnabled_) {
        ensurePreviewSessionStarted();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(520);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());

    const auto createDialogMenuButton = [](QWidget* parent, const QString& text) {
        auto* button = new QToolButton(parent);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        return button;
    };
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        button->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton {"
                " color: %1;"
                " background: transparent;"
                " border: none;"
                " padding: 6px 20px 6px 12px;"
                " text-align: left;"
                "}"
                "QToolButton:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
            if (onTriggered) {
                onTriggered();
            }
            action->trigger();
            menu->close();
        });
        action->setDefaultWidget(button);
        menu->addAction(action);
    };

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

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
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.firework", "Firework Volume"), previewAudioSettings_.fireworkPercent(), &fireworkSlider, &fireworkLabel);

    auto* restoreButton = new QPushButton(uiText("dialog.render_settings.button.restore_project_default", "Restore Project Audio to Software Default"), audioGroup);
    restoreButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    audioFormLayout->addRow(QString(), restoreButton);

    const auto addVideoSliderRow = [](
        QWidget* parent,
        int minimum,
        int maximum,
        int step,
        int value,
        const QString& suffix,
        QSlider** sliderOut,
        QLabel** labelOut
    ) {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(value);
        auto* label = new QLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);

    QSlider* outerBrightnessSlider = nullptr;
    QLabel* outerBrightnessLabel = nullptr;
    QWidget* outerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(previewBackgroundBrightnessOuter_ * 100.0),
        QStringLiteral("%"),
        &outerBrightnessSlider,
        &outerBrightnessLabel
    );
    QSlider* innerBrightnessSlider = nullptr;
    QLabel* innerBrightnessLabel = nullptr;
    QWidget* innerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(previewBackgroundBrightnessInner_ * 100.0),
        QStringLiteral("%"),
        &innerBrightnessSlider,
        &innerBrightnessLabel
    );
    QSlider* layoutSquareScaleSlider = nullptr;
    QLabel* layoutSquareScaleLabel = nullptr;
    QWidget* layoutSquareScaleRow = addVideoSliderRow(
        videoGroup,
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(previewLayoutSquareScale_ * 100.0),
        QStringLiteral("%"),
        &layoutSquareScaleSlider,
        &layoutSquareScaleLabel
    );
    double selectedFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(previewNoteFlowSpeed_);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const int flowSpeedOptionCount = qRound((flowSpeedMax - flowSpeedMin) / flowSpeedStep);
    selectedFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((selectedFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    QString selectedFlowSpeedLabel = QString::number(selectedFlowSpeed, 'f', 1);
    auto* flowSpeedButton = createDialogMenuButton(videoGroup, selectedFlowSpeedLabel);
    auto* flowSpeedMenu = new QMenu(flowSpeedButton);
    styleRoundedMenu(*flowSpeedMenu);
    for (int optionIndex = 0; optionIndex <= flowSpeedOptionCount; ++optionIndex) {
        const double flowSpeed = flowSpeedMin + optionIndex * flowSpeedStep;
        const QString label = QString::number(flowSpeed, 'f', 1);
        addDialogMenuChoice(flowSpeedMenu, label, [&, flowSpeed, label]() {
            selectedFlowSpeed = flowSpeed;
            flowSpeedButton->setText(label);
            previewNoteFlowSpeed_ = selectedFlowSpeed;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setNoteFlowSpeed(selectedFlowSpeed);
            }
            saveProjectRenderState();
            savePortableState();
        });
    }
    flowSpeedButton->setMenu(flowSpeedMenu);

    const QString scaleFillLabel = uiText("dialog.render_settings.video.scale.fill", "Fill (crop if needed)");
    const QString scaleFitLabel = uiText("dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)");
    PreviewBackgroundScaleMode selectedScaleMode = previewBackgroundScaleMode_;
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        selectedScaleMode == PreviewBackgroundScaleMode::FitContain ? scaleFitLabel : scaleFillLabel
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, scaleFillLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FillCrop;
        scaleModeButton->setText(scaleFillLabel);
        previewBackgroundScaleMode_ = selectedScaleMode;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        saveProjectRenderState();
        savePortableState();
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, scaleFitLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FitContain;
        scaleModeButton->setText(scaleFitLabel);
        previewBackgroundScaleMode_ = selectedScaleMode;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        saveProjectRenderState();
        savePortableState();
    });
    scaleModeButton->setMenu(scaleModeMenu);

    struct CanvasAspectOption {
        double ratio;
        QString label;
    };
    const QList<CanvasAspectOption> canvasAspectOptions{
        {1.0, uiText("dialog.render_settings.video.canvas_aspect.square", "1:1 (Square)")},
        {(4.0 / 3.0), uiText("dialog.render_settings.video.canvas_aspect.4_3", "4:3")},
        {(16.0 / 9.0), uiText("dialog.render_settings.video.canvas_aspect.16_9", "16:9")},
    };
    double selectedCanvasAspect = previewCanvasAspectRatio_;
    QString selectedCanvasAspectLabel = canvasAspectOptions.front().label;
    double bestAspectDiff = 1e9;
    for (const CanvasAspectOption& option : canvasAspectOptions) {
        const double diff = qAbs(option.ratio - previewCanvasAspectRatio_);
        if (diff < bestAspectDiff) {
            bestAspectDiff = diff;
            selectedCanvasAspect = option.ratio;
            selectedCanvasAspectLabel = option.label;
        }
    }
    auto* canvasAspectButton = createDialogMenuButton(videoGroup, selectedCanvasAspectLabel);
    auto* canvasAspectMenu = new QMenu(canvasAspectButton);
    styleRoundedMenu(*canvasAspectMenu);
    for (const CanvasAspectOption& option : canvasAspectOptions) {
        const double ratio = option.ratio;
        const QString label = option.label;
        addDialogMenuChoice(canvasAspectMenu, label, [&, ratio, label]() {
            selectedCanvasAspect = ratio;
            canvasAspectButton->setText(label);
            setPreviewCanvasAspectRatio(ratio, true);
        });
    }
    canvasAspectButton->setMenu(canvasAspectMenu);
    auto* restoreSquareCheck = new QCheckBox(
        uiText("dialog.render_settings.video.auto_restore_square", "Auto restore 1:1 after export"),
        videoGroup
    );
    restoreSquareCheck->setChecked(previewAutoRestoreSquareAfterExport_);
    auto* smoothBrightnessCheck = new QCheckBox(
        uiText("dialog.render_settings.video.smooth_brightness", "Smooth brightness"),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        uiText("dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"),
        videoGroup
    );
    timestampCheck->setChecked(previewShowTimestamp_);

    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Layout Size"), layoutSquareScaleRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.flow_speed", "Flow Speed"), flowSpeedButton);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"), scaleModeButton);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.canvas_aspect", "Preview Canvas Aspect"), canvasAspectButton);
    auto* videoCheckRow = new QWidget(videoGroup);
    auto* videoCheckLayout = new QGridLayout(videoCheckRow);
    videoCheckLayout->setContentsMargins(0, 0, 0, 0);
    videoCheckLayout->setHorizontalSpacing(10);
    videoCheckLayout->setVerticalSpacing(6);
    videoCheckLayout->setColumnStretch(0, 1);
    videoCheckLayout->setColumnStretch(1, 1);
    videoCheckLayout->addWidget(smoothBrightnessCheck, 0, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(timestampCheck, 0, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(restoreSquareCheck, 1, 0, Qt::AlignLeft);
    auto* debugCheck = new QCheckBox(uiText("dialog.render_settings.video.debug", "Show preview debug info"), videoGroup);
    debugCheck->setChecked(previewShowDebugInfo_);
    videoCheckLayout->addWidget(debugCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        rootLayout->addWidget(videoGroup, 0);
    }
    auto* buttonBox = new QDialogButtonBox(&dialog);
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
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
    connect(fireworkSlider, &QSlider::valueChanged, &dialog, [this, fireworkLabel, queueAudioApply](int value) {
        previewAudioSettings_.setFireworkPercent(value);
        fireworkLabel->setText(QString::number(previewAudioSettings_.fireworkPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("firework");
    });

    connect(restoreButton, &QPushButton::clicked, &dialog, [this, bgmSlider, answerSlider, slideSlider, breakSlider, exSlider, touchSlider, touchholdSlider, fireworkSlider]() {
        previewAudioSettings_ = softwarePreviewAudioSettings_;
        previewAudioSettings_.normalize();
        {
            QSignalBlocker b1(bgmSlider), b2(answerSlider), b3(slideSlider), b4(breakSlider), b5(exSlider), b6(touchSlider), b7(touchholdSlider), b8(fireworkSlider);
            bgmSlider->setValue(previewAudioSettings_.bgmPercent());
            answerSlider->setValue(previewAudioSettings_.answerPercent());
            slideSlider->setValue(previewAudioSettings_.slidePercent());
            breakSlider->setValue(previewAudioSettings_.breakPercent());
            exSlider->setValue(previewAudioSettings_.exPercent());
            touchSlider->setValue(previewAudioSettings_.touchPercent());
            touchholdSlider->setValue(previewAudioSettings_.touchholdPercent());
            fireworkSlider->setValue(previewAudioSettings_.fireworkPercent());
        }
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        sendPreviewConfigCommand();
        statusBar()->showMessage(uiText("status.audio_restored_default", "Project audio restored to software defaults."));
    });

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, bgmSlider, answerSlider, slideSlider, breakSlider, exSlider, touchSlider, touchholdSlider, fireworkSlider, &pendingAudition]() {
        if (bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || touchholdSlider->isSliderDown()
            || fireworkSlider->isSliderDown()) {
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

    connect(outerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, outerBrightnessLabel](int value) {
        previewBackgroundBrightnessOuter_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        outerBrightnessLabel->setText(QString::number(value) + "%");
        if (previewMediaController_ != nullptr) {
            previewMediaController_->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
        }
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(previewLayoutSquareScale_ * 100.0)) + "%");
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(restoreSquareCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewAutoRestoreSquareAfterExport_ = checked;
        saveProjectRenderState();
        savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewSmoothBrightness_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowTimestamp_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        }
        saveProjectRenderState();
        savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowDebugInfo_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        }
        saveProjectRenderState();
        savePortableState();
    });

    dialog.adjustSize();
    dialog.exec();
}

#include "sections/preview/MainWindow.PreviewSessionFlow.cpp"


