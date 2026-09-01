#include "runtime/validation/ValidationHost.h"
#include "runtime/Shared.h"

#include "UiTheme.h"
#include "common/ProjectPreferences.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::runtime::shared;

namespace {

constexpr int kIssueLineRole = Qt::UserRole;
constexpr int kIssueColRole = Qt::UserRole + 1;
constexpr int kIssueAuxRole = Qt::UserRole + 2;
constexpr int kIssueIgnoredRole = Qt::UserRole + 5;
constexpr int kIssueHtmlRole = Qt::UserRole + 6;
constexpr qreal kIgnoredIssueOpacity = 0.58;
constexpr int kWrappedIssueHorizontalPadding = 4;
constexpr int kWrappedIssueTopPadding = 3;
constexpr int kWrappedIssueBottomPadding = 4;
constexpr int kWrappedIssueMinimumHeight = 40;
constexpr const char* kIgnoreMuriIssuePromptsPrefKey = "ignore_muri_issue_prompts";

int wrappedRichTextHeight(const QString& html, const QFont& font, int width)
{
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(html);
    document.setTextWidth(qMax(1, width));
    return qMax(1, qCeil(document.size().height()));
}

int wrappedListRowWidth(const QListWidget* list)
{
    if (list == nullptr || list->viewport() == nullptr) {
        return 220;
    }
    return qMax(220, list->viewport()->width());
}

QRect wrappedIssueTextRect(const QRect& rowRect)
{
    return rowRect.adjusted(
        kWrappedIssueHorizontalPadding,
        kWrappedIssueTopPadding,
        -kWrappedIssueHorizontalPadding,
        -kWrappedIssueBottomPadding);
}

int wrappedIssueRowHeight(const QString& html, const QFont& font, int rowWidth)
{
    const QRect textRect = wrappedIssueTextRect(QRect(0, 0, rowWidth, 0));
    const int textHeight = wrappedRichTextHeight(html, font, qMax(1, textRect.width()));
    return qMax(kWrappedIssueMinimumHeight, textHeight + kWrappedIssueTopPadding + kWrappedIssueBottomPadding);
}

class WrappedRichTextItemDelegate final : public QStyledItemDelegate {
public:
    explicit WrappedRichTextItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString html = index.data(kIssueHtmlRole).toString();
        if (html.isEmpty()) {
            return QStyledItemDelegate::sizeHint(option, index);
        }

        const auto* list = qobject_cast<const QListWidget*>(option.widget);
        const int rowWidth = wrappedListRowWidth(list);
        return QSize(rowWidth, wrappedIssueRowHeight(html, option.font, rowWidth));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString html = index.data(kIssueHtmlRole).toString();
        if (html.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);

        const UiTheme::Colors& colors = UiTheme::colors();
        const QColor selectedBorder = colors.dark ? QColor("#6B8BB8") : QColor("#9EC2EF");
        const QColor selectedFill = colors.dark ? QColor("#314158") : QColor("#F1F6FF");
        const QColor hoverFill = colors.dark ? QColor("#2A3442") : QColor("#F3F7FD");

        painter->save();
        const QRect fillRect = drawOption.rect.adjusted(1, 1, -1, -1);
        if (drawOption.state.testFlag(QStyle::State_Selected)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(selectedBorder, 1.0));
            painter->setBrush(selectedFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        } else if (drawOption.state.testFlag(QStyle::State_MouseOver)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }

        const QRect textRect = wrappedIssueTextRect(drawOption.rect);
        QTextDocument document;
        document.setDocumentMargin(0.0);
        document.setDefaultFont(drawOption.font);
        document.setHtml(html);
        document.setTextWidth(qMax(1, textRect.width()));

        QAbstractTextDocumentLayout::PaintContext paintContext;
        paintContext.palette.setColor(QPalette::Text, colors.textPrimary);
        qreal contentOpacity = 1.0;
        if (index.data(kIssueIgnoredRole).toBool()) {
            contentOpacity *= kIgnoredIssueOpacity;
        }
        if (!(index.flags() & Qt::ItemIsEnabled)) {
            contentOpacity *= 0.75;
        }
        painter->setOpacity(contentOpacity);
        painter->translate(textRect.topLeft());
        painter->setClipRect(QRect(QPoint(0, 0), textRect.size()));
        document.documentLayout()->draw(painter, paintContext);
        painter->restore();
    }
};

void ensureWrappedIssueDelegate(QListWidget* list)
{
    if (list == nullptr || list->property("wrappedIssueDelegateInstalled").toBool()) {
        return;
    }
    list->setItemDelegate(new WrappedRichTextItemDelegate(list));
    list->setProperty("wrappedIssueDelegateInstalled", true);
}

}  // namespace

QListWidgetItem* miacode::runtime::ValidationHost::addWrappedListEntry(
    QListWidget* list,
    const QString& html,
    const QString& plainText,
    int line,
    int col,
    double second,
    bool enabled)
{
    if (list == nullptr) {
        return nullptr;
    }

    ensureWrappedIssueDelegate(list);

    auto* item = new QListWidgetItem(list);
    item->setToolTip(plainText);
    item->setData(kIssueLineRole, line);
    item->setData(kIssueColRole, col);
    item->setData(kIssueAuxRole, second);
    item->setData(kIssueHtmlRole, html);
    item->setText(plainText);
    if (!enabled) {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    scheduleWrappedListRelayout(list);
    return item;
}

void miacode::runtime::ValidationHost::relayoutWrappedListRows(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }

    const int rowWidth = wrappedListRowWidth(list);
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem* item = list->item(index);
        if (item == nullptr) {
            continue;
        }
        const QString html = item->data(kIssueHtmlRole).toString();
        if (html.isEmpty()) {
            continue;
        }
        item->setSizeHint(QSize(rowWidth, wrappedIssueRowHeight(html, list->font(), rowWidth)));
    }
    list->viewport()->update();
}

void miacode::runtime::ValidationHost::scheduleWrappedListRelayout(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }
    if (list->property("wrappedRelayoutPending").toBool()) {
        return;
    }
    list->setProperty("wrappedRelayoutPending", true);
    QTimer::singleShot(0, list, [this, list]() {
        if (list == nullptr) {
            return;
        }
        list->setProperty("wrappedRelayoutPending", false);
        relayoutWrappedListRows(list);
    });
}

QString miacode::runtime::ValidationHost::currentValidationIgnoreScopeKey() const
{
    return state_.currentFilePath_.isEmpty() ? QStringLiteral("<unsaved>") : state_.currentFilePath_;
}

bool miacode::runtime::ValidationHost::isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const
{
    if (issueTypeKey.isEmpty()) {
        return false;
    }
    const auto it = state_.ignoredHeaderIssueTypesByFile_.constFind(currentValidationIgnoreScopeKey());
    return it != state_.ignoredHeaderIssueTypesByFile_.constEnd() && it->contains(issueTypeKey);
}

void miacode::runtime::ValidationHost::setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored)
{
    if (issueTypeKey.isEmpty()) {
        return;
    }
    const QString scopeKey = currentValidationIgnoreScopeKey();
    QSet<QString>& ignoredTypes = state_.ignoredHeaderIssueTypesByFile_[scopeKey];
    if (ignored) {
        ignoredTypes.insert(issueTypeKey);
    } else {
        ignoredTypes.remove(issueTypeKey);
        if (ignoredTypes.isEmpty()) {
            state_.ignoredHeaderIssueTypesByFile_.remove(scopeKey);
        }
    }
}

void miacode::runtime::ValidationHost::loadProjectValidationPreferences()
{
    bool ignoreMuriIssuePrompts = false;
    if (!state_.currentFilePath_.isEmpty()) {
        const QJsonObject prefs = miacode::project_preferences::load(state_.currentFilePath_);
        const QJsonValue stored = prefs.value(QLatin1String(kIgnoreMuriIssuePromptsPrefKey));
        if (stored.isBool()) {
            ignoreMuriIssuePrompts = stored.toBool(false);
        }
    }
    applyIgnoreMuriIssuePrompts(ignoreMuriIssuePrompts, false);
}

void miacode::runtime::ValidationHost::saveProjectValidationPreferences(const QString& chartFilePath) const
{
    const QString targetPath = chartFilePath.isEmpty() ? state_.currentFilePath_ : chartFilePath;
    if (targetPath.isEmpty()) {
        return;
    }
    const QString preferencesPath = miacode::project_preferences::projectPreferencesFilePath(targetPath);
    QJsonObject prefs = miacode::project_preferences::load(targetPath);
    if (!state_.ignoreMuriIssuePrompts_
        && !QFileInfo::exists(preferencesPath)
        && !prefs.contains(QLatin1String(kIgnoreMuriIssuePromptsPrefKey))) {
        return;
    }
    prefs[QLatin1String(kIgnoreMuriIssuePromptsPrefKey)] = state_.ignoreMuriIssuePrompts_;
    miacode::project_preferences::save(targetPath, prefs);
}

void miacode::runtime::ValidationHost::applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference)
{
    state_.ignoreMuriIssuePrompts_ = enabled;
    if (persistPreference) {
        saveProjectValidationPreferences();
    }
    session_.applyAlignedMuriAnalysisReportToViews();
    updateEditorValidationSummary();
}

QListWidgetItem* Session::addWrappedListEntry(
    QListWidget* list,
    const QString& html,
    const QString& plainText,
    int line,
    int col,
    double second,
    bool enabled)
{
    return validation_->addWrappedListEntry(list, html, plainText, line, col, second, enabled);
}

void Session::relayoutWrappedListRows(QListWidget* list)
{
    validation_->relayoutWrappedListRows(list);
}

void Session::scheduleWrappedListRelayout(QListWidget* list)
{
    validation_->scheduleWrappedListRelayout(list);
}

QString Session::currentValidationIgnoreScopeKey() const
{
    return validation_->currentValidationIgnoreScopeKey();
}

bool Session::isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const
{
    return validation_->isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey);
}

void Session::setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored)
{
    validation_->setIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey, ignored);
}

void Session::loadProjectValidationPreferences()
{
    validation_->loadProjectValidationPreferences();
}

void Session::saveProjectValidationPreferences(const QString& chartFilePath) const
{
    validation_->saveProjectValidationPreferences(chartFilePath);
}

void Session::applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference)
{
    validation_->applyIgnoreMuriIssuePrompts(enabled, persistPreference);
    if (persistPreference) emit muriPromptPreferenceChanged();
}
