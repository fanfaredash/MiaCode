namespace {

using miacode::muri::MuriPanelEntry;

constexpr int kIssueLineRole = Qt::UserRole;
constexpr int kIssueColRole = Qt::UserRole + 1;
constexpr int kIssueAuxRole = Qt::UserRole + 2;
constexpr int kIssueTypeKeyRole = Qt::UserRole + 3;
constexpr int kIssueTypeLabelRole = Qt::UserRole + 4;
constexpr int kIssueIgnoredRole = Qt::UserRole + 5;

enum class ValidationSeverityLevel {
    Error,
    Warning,
};

struct ValidationMessageParts {
    QString severityPrefix;
    QString body;
    ValidationSeverityLevel severity = ValidationSeverityLevel::Error;
};

QString issueTypeSegment(const QString& text)
{
    QString normalized = text.trimmed();
    const int split = normalized.indexOf(QStringLiteral(": "));
    if (split > 0) {
        normalized = normalized.left(split).trimmed();
    }
    return normalized;
}

QString issueDetailTail(const QString& text)
{
    const QString normalized = text.trimmed();
    const int split = normalized.indexOf(QStringLiteral(": "));
    if (split < 0 || split + 2 >= normalized.size()) {
        return QString();
    }
    return normalized.mid(split + 2).trimmed();
}

ValidationMessageParts parseValidationMessage(const QString& rawMessage)
{
    ValidationMessageParts parts;
    parts.body = rawMessage.trimmed();
    const auto matchPrefix = [&](const QString& prefix, ValidationSeverityLevel severity) -> bool {
        if (!parts.body.startsWith(prefix)) {
            return false;
        }
        parts.severityPrefix = prefix;
        parts.severity = severity;
        parts.body = parts.body.mid(prefix.size()).trimmed();
        return true;
    };

    if (matchPrefix(QStringLiteral("[ERROR]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[WARNING]"), ValidationSeverityLevel::Warning)) return parts;
    if (matchPrefix(QStringLiteral("[错误]"), ValidationSeverityLevel::Error)) return parts;
    if (matchPrefix(QStringLiteral("[警告]"), ValidationSeverityLevel::Warning)) return parts;

    parts.severityPrefix = QStringLiteral("[ERROR]");
    parts.severity = ValidationSeverityLevel::Error;
    return parts;
}

QString highlightValidationDetailHtml(const QString& body)
{
    const QString escapedBody = body.toHtmlEscaped();
    const int split = body.indexOf(QStringLiteral(": "));
    if (split < 0 || split + 2 >= body.size()) {
        return escapedBody;
    }

    const QString prefix = body.left(split + 2).toHtmlEscaped();
    const QString codeTail = body.mid(split + 2).trimmed();
    if (codeTail.isEmpty()) {
        return escapedBody;
    }

    return QStringLiteral("%1<span style=\"color:%2;\">%3</span>")
        .arg(prefix, UiTheme::colors().accent.name(QColor::HexRgb), codeTail.toHtmlEscaped());
}

QColor severityColor(ValidationSeverityLevel severity)
{
    return severity == ValidationSeverityLevel::Warning
        ? QColor(QStringLiteral("#B07B00"))
        : QColor(QStringLiteral("#C62828"));
}

QString validationIssueTypeKeyFromRawMessage(const QString& rawMessage)
{
    return QStringLiteral("validation:%1").arg(issueTypeSegment(rawMessage));
}

QString validationIssueTypeLabelFromDisplayMessage(const QString& displayMessage)
{
    const ValidationMessageParts parts = parseValidationMessage(displayMessage);
    return issueTypeSegment(parts.body.isEmpty() ? displayMessage : parts.body);
}

QString muriIssueTypeKey(MuriKind kind)
{
    return QStringLiteral("muri:%1").arg(static_cast<int>(kind));
}

struct WrappedListEntryText {
    QString html;
    QString plainText;
};

QString muriLocationText(int line, int col)
{
    return QStringLiteral("L%1C%2").arg(qMax(1, line)).arg(qMax(1, col));
}

QString localizeMuriDetail(QString rawDetail, bool chineseUi)
{
    rawDetail = rawDetail.trimmed();
    if (!chineseUi || rawDetail.isEmpty()) {
        return rawDetail;
    }

    const auto unwrap = [](const QString& text, const QString& prefix, const QString& suffix, QString* middle) {
        if (middle == nullptr || !text.startsWith(prefix) || !text.endsWith(suffix)) {
            return false;
        }
        *middle = text.mid(prefix.size(), text.size() - prefix.size() - suffix.size()).trimmed();
        return true;
    };
    const auto trimTrailingPeriod = [](QString text) {
        text = text.trimmed();
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
        return text.trimmed();
    };
    const auto splitText = [](const QString& text, const QString& separator, QString* left, QString* right) {
        if (left == nullptr || right == nullptr) {
            return false;
        }
        const int splitIndex = text.indexOf(separator);
        if (splitIndex < 0) {
            return false;
        }
        *left = text.left(splitIndex).trimmed();
        *right = text.mid(splitIndex + separator.size()).trimmed();
        return !left->isEmpty() && !right->isEmpty();
    };
    const auto splitGapText =
        [&trimTrailingPeriod](const QString& text,
                              const QString& separator,
                              QString* left,
                              QString* right,
                              QString* gapText) {
            if (left == nullptr || right == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            const int gapIndex = text.lastIndexOf(QStringLiteral(", gap "));
            if (splitIndex < 0 || gapIndex < 0 || gapIndex <= splitIndex) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *right =
                text.mid(splitIndex + separator.size(), gapIndex - splitIndex - separator.size()).trimmed();
            *gapText = trimTrailingPeriod(text.mid(gapIndex + QStringLiteral(", gap ").size()));
            return !left->isEmpty() && !right->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };
    const auto splitSingleGapText =
        [&trimTrailingPeriod](const QString& text,
                              const QString& separator,
                              QString* left,
                              QString* gapText) {
            if (left == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            if (splitIndex < 0) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *gapText = trimTrailingPeriod(text.mid(splitIndex + separator.size()));
            return !left->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };

    QString middle;
    QString left;
    QString right;
    QString gapText;
    if (splitSingleGapText(
            rawDetail,
            QStringLiteral(" start will early-judge a following tap, gap "),
            &left,
            &gapText)) {
        return QStringLiteral("%1 启动，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitSingleGapText(
            rawDetail,
            QStringLiteral(" jump-start will early-judge a following tap, gap "),
            &left,
            &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" tail may collide with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 结尾可能撞到 %2，间隔 %3。").arg(left, right, gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" was early-judged by "), &left, &right, &gapText)) {
        return QStringLiteral("%1 被 %2 提前判定，间隔 %3。").arg(left, right, gapText);
    }
    if (rawDetail.endsWith(QStringLiteral(" resolved outside its critical window."))
        || rawDetail.contains(QStringLiteral(" resolved outside its critical window, gap "))) {
        const int splitIndex = rawDetail.indexOf(QStringLiteral(" resolved outside its critical window"));
        if (splitIndex > 0) {
            const QString target = rawDetail.left(splitIndex).trimmed();
            const int gapIndex = rawDetail.lastIndexOf(QStringLiteral(", gap "));
            if (gapIndex > splitIndex) {
                gapText = trimTrailingPeriod(rawDetail.mid(gapIndex + QStringLiteral(", gap ").size()));
                return QStringLiteral("%1 提前完成，间隔 %2。").arg(target, gapText);
            }
            return QStringLiteral("%1 的判定落在临界窗之外。").arg(target);
        }
    }
    if (unwrap(rawDetail, QStringLiteral("Multi-touch formed by "), QStringLiteral("."), &middle)) {
        return QStringLiteral("%1 构成多押。").arg(middle);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap."))
        && splitText(
            trimTrailingPeriod(rawDetail.left(rawDetail.size() - QStringLiteral(" formed overlap.").size())),
            QStringLiteral(" and same-position "),
            &left,
            &right)) {
        return QStringLiteral("%1 与相同位置的 %2 构成叠键。").arg(left, right);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap at the same position."))) {
        const QString target = rawDetail.left(
            rawDetail.size() - QStringLiteral(" formed overlap at the same position.").size()).trimmed();
        if (!target.isEmpty()) {
            return QStringLiteral("%1 在相同位置构成叠键。").arg(target);
        }
    }

    if (unwrap(
            rawDetail,
            QStringLiteral("Slide-head trigger from "),
            QStringLiteral(" early-judged this note."),
            &middle)) {
        return QStringLiteral("来自 %1 的滑键头触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Pad trigger from "),
            QStringLiteral(" early-judged this note."),
            &middle)) {
        return QStringLiteral("来自 %1 的按下触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Runtime simple-note judge for "),
            QStringLiteral(" missed the critical window and resolved as overlap."),
            &middle)) {
        return QStringLiteral("运行时普通物件判定 %1 错过了判定临界窗，并最终判为叠键。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Slide runtime judge for "),
            QStringLiteral(" resolved outside its critical window."),
            &middle)) {
        return QStringLiteral("Slide 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Slide "),
            QStringLiteral(" was cleared earlier than its normal judge timing."),
            &middle)) {
        return QStringLiteral("Slide %1 的完成时间早于正常判定时机。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Wifi runtime judge for "),
            QStringLiteral(" resolved outside its critical window."),
            &middle)) {
        return QStringLiteral("Wifi 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Wifi "),
            QStringLiteral(" was cleared earlier than its normal judge timing."),
            &middle)) {
        return QStringLiteral("Wifi %1 的完成时间早于正常判定时机。").arg(middle);
    }

    if (rawDetail.startsWith(QStringLiteral("Static reference from "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Static reference from ").size()).trimmed();
        const int deltaIndex = tail.lastIndexOf(QStringLiteral(", Δ "));
        if (deltaIndex >= 0 && tail.endsWith(QStringLiteral(" ms"))) {
            const QString source = tail.left(deltaIndex).trimmed();
            const QString deltaText = tail.mid(deltaIndex + QStringLiteral(", Δ ").size());
            return QStringLiteral("静态参考：%1，Δ %2").arg(source, deltaText);
        }
        return QStringLiteral("静态参考：%1").arg(tail);
    }

    if (rawDetail.startsWith(QStringLiteral("Runtime hand actions formed "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Runtime hand actions formed ").size()).trimmed();
        const QString marker = QStringLiteral("-hand multi-touch: ");
        const int split = tail.indexOf(marker);
        if (split > 0) {
            const QString handCount = tail.left(split).trimmed();
            const QString actions = tail.mid(split + marker.size()).trimmed();
            return QStringLiteral("运行时手部动作形成了 %1 手多押：%2").arg(handCount, actions);
        }
    }

    return rawDetail;
}

WrappedListEntryText buildMuriPanelEntryText(const MuriPanelEntry& entry, bool ignoredInHeader)
{
    const bool chineseUi = UiText::isChineseUi();
    const ValidationSeverityLevel severity = entry.alertLevel == MuriAlertLevel::Warning
        ? ValidationSeverityLevel::Warning
        : ValidationSeverityLevel::Error;
    const QColor summaryColor = ignoredInHeader
        ? UiTheme::colors().textMuted
        : severityColor(severity);
    const QString alertText = QStringLiteral("[%1]").arg(muriAlertLevelDisplayName(entry.alertLevel, chineseUi));
    const QString typeText = muriKindDisplayName(entry.kind, chineseUi);
    const QString locationText = muriLocationText(entry.line, entry.col);
    const QString detailText = localizeMuriDetail(entry.rawDetail, chineseUi).trimmed();
    const QString summaryText = QStringLiteral("%1 %2 %3").arg(alertText, typeText, locationText);
    QString contentHtml = QStringLiteral("<span style=\"font-weight:700;color:%1;\">%2</span>")
                              .arg(summaryColor.name(QColor::HexRgb), summaryText.toHtmlEscaped());

    WrappedListEntryText text;
    if (!detailText.isEmpty()) {
        contentHtml += QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
                           .arg(
                               (ignoredInHeader ? UiTheme::colors().textSecondary : UiTheme::colors().textPrimary)
                                   .name(QColor::HexRgb),
                               detailText.toHtmlEscaped());
    }
    text.html = QStringLiteral("<div style=\"line-height:128%; margin:1px 0;\">%1</div>").arg(contentHtml);

    text.plainText = detailText.isEmpty()
        ? summaryText
        : QStringLiteral("%1\n%2").arg(summaryText, detailText);
    return text;
}

int wrappedRichTextHeight(const QString& html, const QFont& font, int width)
{
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(html);
    document.setTextWidth(qMax(1, width));
    return qMax(1, qCeil(document.size().height()));
}

bool buildEditorSelectionCursor(PlainCodeEditor* editor, int line, int col, int endCol, QTextCursor* cursorOut)
{
    if (editor == nullptr || editor->document() == nullptr || cursorOut == nullptr) {
        return false;
    }

    const int normalizedLine = qMax(1, line);
    const int normalizedCol = qMax(1, col);
    const int normalizedEndCol = qMax(normalizedCol, endCol);
    QTextBlock block = editor->document()->findBlockByNumber(normalizedLine - 1);
    if (!block.isValid()) {
        return false;
    }

    const QString blockText = block.text();
    const int lineLength = blockText.size();
    const int localIndex = qBound(0, normalizedCol - 1, qMax(0, lineLength));
    const int localEndIndex = qBound(localIndex, normalizedEndCol - 1, qMax(0, lineLength));

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + localIndex);
    if (lineLength > 0) {
        const int selectionLength = qMax(0, localEndIndex - localIndex + (localIndex < lineLength ? 1 : 0));
        if (selectionLength > 0) {
            cursor.setPosition(block.position() + localIndex + selectionLength, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(block.position() + qMax(0, lineLength - 1));
            cursor.setPosition(block.position() + lineLength, QTextCursor::KeepAnchor);
        }
    }

    *cursorOut = cursor;
    return true;
}

}  // namespace

QListWidgetItem* MainWindow::addWrappedListEntry(
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

    auto* item = new QListWidgetItem(list);
    item->setToolTip(plainText);
    item->setData(kIssueLineRole, line);
    item->setData(kIssueColRole, col);
    item->setData(kIssueAuxRole, second);
    if (!enabled) {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    item->setText(QString());

    auto* label = new QLabel(list);
    label->setObjectName(QStringLiteral("WrappedListEntryLabel"));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setStyleSheet(UiTheme::validationMessageLabelStyleSheet());
    label->setText(html);

    auto* rowWidget = new QWidget(list);
    auto* rowLayout = new QVBoxLayout(rowWidget);
    const bool roomyLayout = (list == muriList_);
    rowLayout->setContentsMargins(roomyLayout ? 4 : 0, roomyLayout ? 3 : 0, roomyLayout ? 4 : 0, roomyLayout ? 4 : 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(label);

    list->setItemWidget(item, rowWidget);
    relayoutWrappedListRows(list);
    scheduleWrappedListRelayout(list);
    return item;
}

void MainWindow::relayoutWrappedListRows(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }

    const int rowWidth = qMax(220, list->viewport()->width());
    const bool roomyLayout = (list == muriList_);
    const int horizontalMargin = roomyLayout ? 8 : 0;
    const int verticalMargin = roomyLayout ? 7 : 2;
    const int labelWidth = qMax(1, rowWidth - horizontalMargin);
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem* item = list->item(index);
        if (item == nullptr) {
            continue;
        }
        QWidget* rowWidget = list->itemWidget(item);
        if (rowWidget == nullptr) {
            continue;
        }
        QLabel* label = rowWidget->findChild<QLabel*>(QStringLiteral("WrappedListEntryLabel"));
        if (label == nullptr) {
            continue;
        }
        label->setFixedWidth(labelWidth);
        const int labelHeight = wrappedRichTextHeight(label->text(), label->font(), labelWidth);
        label->setFixedHeight(labelHeight);
        const int rowHeight = qMax(roomyLayout ? 40 : 28, labelHeight + verticalMargin);
        rowWidget->setMinimumHeight(rowHeight);
        rowWidget->setMaximumHeight(rowHeight);
        item->setSizeHint(QSize(rowWidth, rowHeight));
    }
}

void MainWindow::scheduleWrappedListRelayout(QListWidget* list)
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

QString MainWindow::currentValidationIgnoreScopeKey() const
{
    return currentFilePath_.isEmpty() ? QStringLiteral("<unsaved>") : currentFilePath_;
}

bool MainWindow::isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const
{
    if (issueTypeKey.isEmpty()) {
        return false;
    }
    const auto it = ignoredHeaderIssueTypesByFile_.constFind(currentValidationIgnoreScopeKey());
    return it != ignoredHeaderIssueTypesByFile_.constEnd() && it->contains(issueTypeKey);
}

void MainWindow::setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored)
{
    if (issueTypeKey.isEmpty()) {
        return;
    }
    const QString scopeKey = currentValidationIgnoreScopeKey();
    QSet<QString>& ignoredTypes = ignoredHeaderIssueTypesByFile_[scopeKey];
    if (ignored) {
        ignoredTypes.insert(issueTypeKey);
    } else {
        ignoredTypes.remove(issueTypeKey);
        if (ignoredTypes.isEmpty()) {
            ignoredHeaderIssueTypesByFile_.remove(scopeKey);
        }
    }
}

void MainWindow::refreshEditorExtraSelections()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    if (editor == nullptr) {
        return;
    }

    QVector<QTextEdit::ExtraSelection> selections;
    selections.reserve(validationDecorations_.size() + (previewFollowDecorationActive_ ? 1 : 0));

    for (const ValidationDecoration& decoration : validationDecorations_) {
        QTextCursor cursor;
        if (!buildEditorSelectionCursor(editor, decoration.line, decoration.col, decoration.endCol, &cursor)) {
            continue;
        }

        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        sel.format.setUnderlineColor(decoration.warning
                                         ? severityColor(ValidationSeverityLevel::Warning)
                                         : severityColor(ValidationSeverityLevel::Error));
        sel.format.setToolTip(decoration.message);
        selections.append(sel);
    }

    if (previewFollowDecorationActive_) {
        QTextCursor cursor;
        if (buildEditorSelectionCursor(
                editor,
                previewFollowDecorationLine_,
                previewFollowDecorationCol_,
                previewFollowDecorationCol_,
                &cursor)) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            QColor highlight = UiTheme::colors().accent;
            highlight.setAlpha(UiTheme::colors().dark ? 88 : 56);
            sel.format.setBackground(highlight);
            sel.format.setProperty(QTextFormat::FullWidthSelection, false);
            selections.append(sel);
        }
    }

    editor->setExtraSelections(selections);
}

void MainWindow::updateEditorValidationSummary()
{
    if (editorValidationSummaryWidget_ == nullptr
        || editorValidationErrorIconLabel_ == nullptr
        || editorValidationErrorCountLabel_ == nullptr
        || editorValidationWarningIconLabel_ == nullptr
        || editorValidationWarningCountLabel_ == nullptr
        || editorValidationMuriIconLabel_ == nullptr
        || editorValidationMuriCountLabel_ == nullptr) {
        return;
    }

    if (!hasActiveDifficulty()) {
        editorValidationSummaryWidget_->hide();
        return;
    }

    int errorCount = 0;
    int warningCount = 0;
    int muriIssueCount = 0;
    const QSet<QString> ignoredTypes = ignoredHeaderIssueTypesByFile_.value(currentValidationIgnoreScopeKey());
    const auto cacheIt = validationCacheByDifficulty_.constFind(activeDifficultyId());
    if (cacheIt != validationCacheByDifficulty_.constEnd()) {
        const ValidationCacheEntry& entry = cacheIt.value();
        if (entry.chartText == activeChartText() && entry.chineseUi == UiText::isChineseUi()) {
            for (const ValidationCachedIssue& issue : entry.issues) {
                const QString issueTypeKey = issue.issueTypeKey.isEmpty()
                    ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
                    : issue.issueTypeKey;
                if (ignoredTypes.contains(issueTypeKey)) {
                    continue;
                }
                const ValidationMessageParts parts = parseValidationMessage(issue.displayMessage);
                if (parts.severity == ValidationSeverityLevel::Warning) {
                    ++warningCount;
                } else {
                    ++errorCount;
                }
            }
        }
    }
    const QVector<MuriPanelEntry> muriEntries =
        miacode::muri::buildVisibleMuriPanelEntries(muriAnalysisReport_, muriStaticReferences_);
    for (const MuriPanelEntry& entry : muriEntries) {
        if (ignoredTypes.contains(muriIssueTypeKey(entry.kind))) {
            continue;
        }
        ++muriIssueCount;
    }

    const QColor mutedColor = UiTheme::colors().textMuted;
    const QColor errorColor = errorCount > 0 ? QColor(QStringLiteral("#D94A4A")) : mutedColor;
    const QColor warningColor = warningCount > 0 ? QColor(QStringLiteral("#D4A12A")) : mutedColor;
    const QColor muriColor = muriIssueCount > 0 ? QColor(QStringLiteral("#A24AD9")) : mutedColor;
    const bool showError = errorCount > 0;
    const bool showWarning = warningCount > 0;
    const bool showMuri = muriIssueCount > 0;

    editorValidationErrorIconLabel_->setPixmap(
        makeEditorValidationSummaryIcon(errorColor, EditorValidationSummaryIconKind::Error));
    editorValidationErrorCountLabel_->setText(QString::number(errorCount));
    editorValidationErrorCountLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor.name(QColor::HexRgb)));
    editorValidationErrorIconLabel_->setVisible(showError);
    editorValidationErrorCountLabel_->setVisible(showError);

    editorValidationWarningIconLabel_->setPixmap(
        makeEditorValidationSummaryIcon(warningColor, EditorValidationSummaryIconKind::Warning));
    editorValidationWarningCountLabel_->setText(QString::number(warningCount));
    editorValidationWarningCountLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(warningColor.name(QColor::HexRgb)));
    editorValidationWarningIconLabel_->setVisible(showWarning);
    editorValidationWarningCountLabel_->setVisible(showWarning);

    editorValidationMuriIconLabel_->setPixmap(
        makeEditorValidationSummaryIcon(muriColor, EditorValidationSummaryIconKind::Muri));
    editorValidationMuriCountLabel_->setText(QString::number(muriIssueCount));
    editorValidationMuriCountLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(muriColor.name(QColor::HexRgb)));
    editorValidationMuriIconLabel_->setVisible(showMuri);
    editorValidationMuriCountLabel_->setVisible(showMuri);

    const QString summaryTooltip = showMuri
        ? uiText(
              "editor.validation_summary.tooltip_with_muri",
              "%1 error(s), %2 warning(s), %3 muri issue(s)"
          ).arg(errorCount).arg(warningCount).arg(muriIssueCount)
        : uiText(
              "editor.validation_summary.tooltip",
              "%1 error(s), %2 warning(s)"
          ).arg(errorCount).arg(warningCount);
    const bool showSummary = previewShowValidationSummary_ && (showError || showWarning || showMuri);
    editorValidationSummaryWidget_->setProperty("hasContent", showSummary);
    editorValidationSummaryWidget_->setToolTip(summaryTooltip);
    editorValidationErrorIconLabel_->setToolTip(summaryTooltip);
    editorValidationErrorCountLabel_->setToolTip(summaryTooltip);
    editorValidationWarningIconLabel_->setToolTip(summaryTooltip);
    editorValidationWarningCountLabel_->setToolTip(summaryTooltip);
    editorValidationMuriIconLabel_->setToolTip(summaryTooltip);
    editorValidationMuriCountLabel_->setToolTip(summaryTooltip);
    editorValidationSummaryWidget_->setVisible(showSummary);
    editorValidationSummaryWidget_->adjustSize();
    updateEditorHeaderLayoutMode();
}

void MainWindow::setPreviewFollowDecoration(int line, int col)
{
    previewFollowDecorationActive_ = true;
    previewFollowDecorationLine_ = qMax(1, line);
    previewFollowDecorationCol_ = qMax(1, col);
    refreshEditorExtraSelections();
}

void MainWindow::clearPreviewFollowDecoration()
{
    if (!previewFollowDecorationActive_) {
        return;
    }
    previewFollowDecorationActive_ = false;
    refreshEditorExtraSelections();
}

void MainWindow::clearValidationErrors()
{
    if (errorList_ == nullptr) {
        return;
    }
    errorList_->clear();
}

void MainWindow::clearMuriDiagnostics()
{
    if (muriList_ == nullptr) {
        updateEditorValidationSummary();
        return;
    }
    muriList_->clear();
    updateEditorValidationSummary();
}

void MainWindow::clearValidationDecorations()
{
    validationDecorations_.clear();
    refreshEditorExtraSelections();
}

void MainWindow::addValidationError(
    int line,
    int col,
    const QString& message,
    const QString& issueTypeKey,
    const QString& issueTypeLabel,
    bool ignoredInHeader)
{
    if (errorList_ == nullptr) {
        return;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    const QColor sevColor = ignoredInHeader ? UiTheme::colors().textMuted : severityColor(parts.severity);
    const QColor detailColor = ignoredInHeader ? UiTheme::colors().textSecondary : UiTheme::colors().textPrimary;
    const QString issueTitle = issueTypeLabel.isEmpty() ? issueTypeSegment(parts.body) : issueTypeLabel;
    const QString detailTail = issueDetailTail(parts.body);
    const QString detailText = detailTail.isEmpty() ? parts.body : detailTail;
    const QString headerText = QStringLiteral("%1 %2 L%3 C%4")
        .arg(parts.severityPrefix, issueTitle, QString::number(line), QString::number(col));
    const QString plainText = detailText.isEmpty()
        ? headerText
        : QStringLiteral("%1\n%2").arg(headerText, detailText);

    const QString headerHtml = QStringLiteral(
        "<span style=\"font-weight:700;color:%1;\">%2</span> "
        "<span style=\"color:%5;\">%6</span>  "
        "<span style=\"color:%5;\">L%3 C%4</span>"
    )
        .arg(
            sevColor.name(),
            parts.severityPrefix.toHtmlEscaped(),
            QString::number(line),
            QString::number(col),
            UiTheme::colors().textSecondary.name(QColor::HexRgb),
            issueTitle.toHtmlEscaped()
        );
    const QString detailHtml = detailText.isEmpty()
        ? QString()
        : QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
              .arg(
                  detailColor.name(QColor::HexRgb),
                  ignoredInHeader ? detailText.toHtmlEscaped() : detailText.toHtmlEscaped()
              );
    QListWidgetItem* item = addWrappedListEntry(errorList_, headerHtml + detailHtml, plainText, line, col, -1.0, true);
    if (item != nullptr) {
        item->setData(kIssueAuxRole, parts.severity == ValidationSeverityLevel::Warning ? 1 : 0);
        item->setData(kIssueTypeKeyRole, issueTypeKey);
        item->setData(kIssueTypeLabelRole, issueTypeLabel);
        item->setData(kIssueIgnoredRole, ignoredInHeader);
        if (ignoredInHeader) {
            if (QWidget* rowWidget = errorList_->itemWidget(item)) {
                auto* effect = new QGraphicsOpacityEffect(rowWidget);
                effect->setOpacity(0.58);
                rowWidget->setGraphicsEffect(effect);
            }
        }
    }
}

void MainWindow::addValidationDecoration(int line, int col, const QString& message, int endCol)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }
    if (endCol < col) {
        endCol = col;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    ValidationDecoration decoration;
    decoration.line = qMax(1, line);
    decoration.col = qMax(1, col);
    decoration.endCol = qMax(decoration.col, endCol);
    decoration.message = message;
    decoration.warning = (parts.severity == ValidationSeverityLevel::Warning);
    validationDecorations_.append(decoration);
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
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }

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
    clearPreviewFollowDecoration();
}

void MainWindow::onErrorItemActivated(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const int line = item->data(kIssueLineRole).toInt();
    const int col = item->data(kIssueColRole).toInt();
    jumpToLocation(line, col);
}

void MainWindow::onMuriItemActivated(QListWidgetItem* item)
{
    if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) {
        return;
    }
    const int line = item->data(kIssueLineRole).toInt();
    const int col = item->data(kIssueColRole).toInt();
    const double second = item->data(kIssueAuxRole).toDouble();
    if (second >= 0.0) {
        navigateTimelineToSecond(second, true);
        return;
    }
    jumpToLocation(line, col);
}

void MainWindow::showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList)
{
    if (list == nullptr) {
        return;
    }
    QListWidgetItem* item = list->itemAt(pos);
    if (item == nullptr) {
        return;
    }

    const QString issueTypeKey = item->data(kIssueTypeKeyRole).toString();
    const QString issueTypeLabel = item->data(kIssueTypeLabelRole).toString();
    const bool ignoredInHeader = item->data(kIssueIgnoredRole).toBool();

    QMenu menu(this);
    styleRoundedMenu(menu);

    QAction* copyAction = menu.addAction(
        UiText::isChineseUi() ? QStringLiteral("复制信息") : QStringLiteral("Copy Info")
    );
    connect(copyAction, &QAction::triggered, this, [this, item]() {
        const QString text = item->toolTip().trimmed();
        QGuiApplication::clipboard()->setText(text);
        if (statusBar() != nullptr) {
            statusBar()->showMessage(
                UiText::isChineseUi() ? QStringLiteral("已复制信息。") : QStringLiteral("Issue info copied.")
            );
        }
    });

    QAction* jumpAction = menu.addAction(
        UiText::isChineseUi() ? QStringLiteral("跳转到源") : QStringLiteral("Jump to Source")
    );
    connect(jumpAction, &QAction::triggered, this, [this, item, muriList]() {
        if (muriList) {
            onMuriItemActivated(item);
        } else {
            onErrorItemActivated(item);
        }
    });

    if (!issueTypeKey.isEmpty()) {
        QAction* ignoreAction = menu.addAction(
            ignoredInHeader
                ? (UiText::isChineseUi() ? QStringLiteral("取消忽视该类型提示")
                                         : QStringLiteral("Stop Ignoring This Issue Type"))
                : (UiText::isChineseUi() ? QStringLiteral("忽视该类型提示")
                                         : QStringLiteral("Ignore This Issue Type"))
        );
        connect(ignoreAction, &QAction::triggered, this, [this, issueTypeKey, ignoredInHeader]() {
            const int currentTabIndex = bottomTabs_ != nullptr ? bottomTabs_->currentIndex() : -1;
            setIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey, !ignoredInHeader);
            refreshValidationPanelForActiveField();
            refreshMuriDiagnosticsPanel();
            if (bottomTabs_ != nullptr && currentTabIndex >= 0) {
                bottomTabs_->setCurrentIndex(currentTabIndex);
            }
        });
    }

    menu.exec(list->viewport()->mapToGlobal(pos));
}

void MainWindow::refreshMuriDiagnosticsPanel()
{
    if (muriList_ == nullptr) {
        return;
    }

    muriList_->clear();
    if (muriAnalysisReport_.diagnostics.isEmpty() && muriStaticReferences_.isEmpty()) {
        auto* item = new QListWidgetItem(
            UiText::isChineseUi()
                ? QStringLiteral("未检测到无理。")
                : QStringLiteral("No muri issues detected."),
            muriList_
        );
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        updateEditorValidationSummary();
        return;
    }

    QVector<MuriPanelEntry> entries =
        miacode::muri::buildVisibleMuriPanelEntries(muriAnalysisReport_, muriStaticReferences_);
    std::sort(entries.begin(), entries.end(), [](const MuriPanelEntry& a, const MuriPanelEntry& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });

    for (const MuriPanelEntry& entry : entries) {
        const QString issueTypeKey = muriIssueTypeKey(entry.kind);
        const bool ignoredInHeader = isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey);
        const QString title = muriKindDisplayName(entry.kind, UiText::isChineseUi());
        const WrappedListEntryText text = buildMuriPanelEntryText(entry, ignoredInHeader);
        QListWidgetItem* item = addWrappedListEntry(
            muriList_,
            text.html,
            text.plainText,
            entry.line,
            entry.col,
            entry.second,
            true
        );
        if (item != nullptr) {
            item->setData(kIssueTypeKeyRole, issueTypeKey);
            item->setData(kIssueTypeLabelRole, title);
            item->setData(kIssueIgnoredRole, ignoredInHeader);
            if (ignoredInHeader) {
                if (QWidget* rowWidget = muriList_->itemWidget(item)) {
                    auto* effect = new QGraphicsOpacityEffect(rowWidget);
                    effect->setOpacity(0.58);
                    rowWidget->setGraphicsEffect(effect);
                }
            }
        }
    }
    scheduleWrappedListRelayout(muriList_);
    updateEditorValidationSummary();
}

void MainWindow::clearValidationCache()
{
    validationCacheByDifficulty_.clear();
    updateEditorValidationSummary();
}

void MainWindow::setValidationTabVisible(bool visible)
{
    if (bottomTabs_ == nullptr || errorList_ == nullptr) {
        return;
    }
    const int errorTabIndex = bottomTabs_->indexOf(errorList_);
    if (errorTabIndex < 0) {
        return;
    }
    bottomTabs_->setTabVisible(errorTabIndex, visible);
}

void MainWindow::refreshValidationPanelForActiveField()
{
    if (!hasActiveDifficulty()) {
        setValidationTabVisible(false);
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    setValidationTabVisible(true);
    const int difficultyId = activeDifficultyId();
    const auto it = validationCacheByDifficulty_.constFind(difficultyId);
    if (it == validationCacheByDifficulty_.constEnd()) {
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    const QString chartText = activeChartText();
    const bool chineseUi = UiText::isChineseUi();
    const ValidationCacheEntry& entry = it.value();
    if (entry.chartText != chartText || entry.chineseUi != chineseUi) {
        clearValidationErrors();
        clearValidationDecorations();
        updateEditorValidationSummary();
        return;
    }

    clearValidationErrors();
    clearValidationDecorations();
    for (const ValidationCachedIssue& issue : entry.issues) {
        const QString issueTypeKey = issue.issueTypeKey.isEmpty()
            ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
            : issue.issueTypeKey;
        const QString issueTypeLabel = issue.issueTypeLabel.isEmpty()
            ? validationIssueTypeLabelFromDisplayMessage(issue.displayMessage)
            : issue.issueTypeLabel;
        addValidationError(
            issue.line,
            issue.col,
            issue.displayMessage,
            issueTypeKey,
            issueTypeLabel,
            isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey)
        );
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    refreshEditorExtraSelections();
    scheduleWrappedListRelayout(errorList_);
    updateEditorValidationSummary();
}

bool MainWindow::runValidateSimaiSilently(bool focusFirstIssue)
{
    if (!hasActiveDifficulty()) {
        refreshValidationPanelForActiveField();
        return false;
    }

    const int difficultyId = activeDifficultyId();
    const QString chartText = activeChartText();
    const bool chineseUi = UiText::isChineseUi();
    const SimaiNativeParseResult* cachedLenientResult =
        (lastTimelineParseDifficultyId_ == difficultyId && lastTimelineParseChartText_ == chartText)
        ? &lastTimelineParseResult_
        : nullptr;

    ValidationCacheEntry entry;
    const auto cacheIt = validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->chineseUi == chineseUi) {
        entry = cacheIt.value();
    } else {
        QElapsedTimer reportTimer;
        reportTimer.start();
        const SimaiNativeValidationLocale locale = chineseUi
            ? SimaiNativeValidationLocale::Chinese
            : SimaiNativeValidationLocale::English;
        const SimaiNativeValidationReport report =
            SimaiNativeParser::buildValidationReport(chartText, locale, cachedLenientResult);
        entry.chartText = chartText;
        entry.chineseUi = chineseUi;
        entry.ok = report.ok;
        entry.errorCount = report.errorCount;
        entry.warningCount = report.warningCount;
        entry.lenientNoteCount = report.lenientNoteCount;
        entry.lenientErrorCount = report.lenientErrorCount;
        entry.strictNoteCount = report.strictNoteCount;
        entry.strictErrorCount = report.strictErrorCount;
        entry.issues.clear();
        entry.issues.reserve(report.issues.size());
        for (const SimaiNativeValidationIssue& issue : report.issues) {
            ValidationCachedIssue cachedIssue;
            cachedIssue.line = issue.line;
            cachedIssue.col = issue.col;
            cachedIssue.endCol = issue.endCol;
            cachedIssue.rawMessage = issue.rawMessage;
            cachedIssue.displayMessage = issue.displayMessage;
            cachedIssue.issueTypeKey = validationIssueTypeKeyFromRawMessage(issue.rawMessage);
            cachedIssue.issueTypeLabel = validationIssueTypeLabelFromDisplayMessage(issue.displayMessage);
            entry.issues.append(cachedIssue);
        }
        validationCacheByDifficulty_.insert(difficultyId, entry);
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "edit/validation_perf",
                QStringLiteral("build_report=%1ms issues=%2 cached_lenient=%3")
                    .arg(reportTimer.elapsed())
                    .arg(entry.issues.size())
                    .arg(cachedLenientResult != nullptr ? QStringLiteral("1") : QStringLiteral("0"))
            );
        }
    }

    QElapsedTimer applyTimer;
    applyTimer.start();
    setValidationTabVisible(true);
    clearValidationErrors();
    clearValidationDecorations();
    for (const ValidationCachedIssue& issue : entry.issues) {
        const QString issueTypeKey = issue.issueTypeKey.isEmpty()
            ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
            : issue.issueTypeKey;
        const QString issueTypeLabel = issue.issueTypeLabel.isEmpty()
            ? validationIssueTypeLabelFromDisplayMessage(issue.displayMessage)
            : issue.issueTypeLabel;
        addValidationError(
            issue.line,
            issue.col,
            issue.displayMessage,
            issueTypeKey,
            issueTypeLabel,
            isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey)
        );
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    refreshEditorExtraSelections();
    scheduleWrappedListRelayout(errorList_);
    updateEditorValidationSummary();
    if (focusFirstIssue && !entry.issues.isEmpty() && bottomTabs_ != nullptr && errorList_ != nullptr) {
        const int errorTabIndex = bottomTabs_->indexOf(errorList_);
        if (errorTabIndex >= 0) {
            bottomTabs_->setCurrentIndex(errorTabIndex);
        }
        onErrorItemActivated(errorList_->item(0));
    }
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "edit/validation_apply_perf",
            QStringLiteral("apply_ui=%1ms issues=%2")
                .arg(applyTimer.elapsed())
                .arg(entry.issues.size())
        );
    }
    return entry.ok;
}

bool MainWindow::runValidateSimai()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage(
            uiText("status.syntax.select_difficulty", "Select a difficulty field first.")
        );
        return false;
    }

    const int difficultyId = activeDifficultyId();
    const QString chartText = activeChartText();
    const bool chineseUi = UiText::isChineseUi();
    const SimaiNativeParseResult* cachedLenientResult =
        (lastTimelineParseDifficultyId_ == difficultyId && lastTimelineParseChartText_ == chartText)
        ? &lastTimelineParseResult_
        : nullptr;

    ValidationCacheEntry entry;
    const auto cacheIt = validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->chineseUi == chineseUi) {
        entry = cacheIt.value();
    } else {
        QElapsedTimer reportTimer;
        reportTimer.start();
        const SimaiNativeValidationLocale locale = chineseUi
            ? SimaiNativeValidationLocale::Chinese
            : SimaiNativeValidationLocale::English;
        const SimaiNativeValidationReport report =
            SimaiNativeParser::buildValidationReport(chartText, locale, cachedLenientResult);
        entry.chartText = chartText;
        entry.chineseUi = chineseUi;
        entry.ok = report.ok;
        entry.errorCount = report.errorCount;
        entry.warningCount = report.warningCount;
        entry.lenientNoteCount = report.lenientNoteCount;
        entry.lenientErrorCount = report.lenientErrorCount;
        entry.strictNoteCount = report.strictNoteCount;
        entry.strictErrorCount = report.strictErrorCount;
        entry.issues.clear();
        entry.issues.reserve(report.issues.size());
        for (const SimaiNativeValidationIssue& issue : report.issues) {
            ValidationCachedIssue cachedIssue;
            cachedIssue.line = issue.line;
            cachedIssue.col = issue.col;
            cachedIssue.endCol = issue.endCol;
            cachedIssue.rawMessage = issue.rawMessage;
            cachedIssue.displayMessage = issue.displayMessage;
            cachedIssue.issueTypeKey = validationIssueTypeKeyFromRawMessage(issue.rawMessage);
            cachedIssue.issueTypeLabel = validationIssueTypeLabelFromDisplayMessage(issue.displayMessage);
            entry.issues.append(cachedIssue);
        }
        validationCacheByDifficulty_.insert(difficultyId, entry);
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "edit/validation_perf",
                QStringLiteral("build_report=%1ms issues=%2 cached_lenient=%3")
                    .arg(reportTimer.elapsed())
                    .arg(entry.issues.size())
                    .arg(cachedLenientResult != nullptr ? QStringLiteral("1") : QStringLiteral("0"))
            );
        }
    }

    const QString payload = QStringLiteral(
        "lenient_note_count=%1\n"
        "lenient_error_count=%2\n"
        "strict_note_count=%3\n"
        "strict_error_count=%4"
    )
        .arg(entry.lenientNoteCount)
        .arg(entry.lenientErrorCount)
        .arg(entry.strictNoteCount)
        .arg(entry.strictErrorCount);
    appendOutput("validate", payload);

    clearValidationErrors();
    clearValidationDecorations();
    for (const ValidationCachedIssue& issue : entry.issues) {
        const QString issueTypeKey = issue.issueTypeKey.isEmpty()
            ? validationIssueTypeKeyFromRawMessage(issue.rawMessage.isEmpty() ? issue.displayMessage : issue.rawMessage)
            : issue.issueTypeKey;
        const QString issueTypeLabel = issue.issueTypeLabel.isEmpty()
            ? validationIssueTypeLabelFromDisplayMessage(issue.displayMessage)
            : issue.issueTypeLabel;
        addValidationError(
            issue.line,
            issue.col,
            issue.displayMessage,
            issueTypeKey,
            issueTypeLabel,
            isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey)
        );
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    refreshEditorExtraSelections();
    scheduleWrappedListRelayout(errorList_);
    updateEditorValidationSummary();
    if (!entry.issues.isEmpty() && bottomTabs_ != nullptr && errorList_ != nullptr) {
        const int errorTabIndex = bottomTabs_->indexOf(errorList_);
        if (errorTabIndex >= 0) {
            bottomTabs_->setCurrentIndex(errorTabIndex);
        }
        onErrorItemActivated(errorList_->item(0));
    }

    if (entry.ok) {
        statusBar()->showMessage(uiText("status.syntax.passed", "Syntax check passed."));
        QMessageBox okDialog(this);
        okDialog.setIcon(QMessageBox::Information);
        okDialog.setWindowTitle(validateAction_ != nullptr ? validateAction_->text() : QStringLiteral("Syntax Check"));
        okDialog.setWindowIcon(windowIcon());
        okDialog.setText(uiText("dialog.syntax_ok.message", "No syntax errors or warnings found."));
        okDialog.setStandardButtons(QMessageBox::Ok);
        okDialog.setDefaultButton(QMessageBox::Ok);
        UiDialogs::localizeMessageBox(&okDialog);
        auto* closeOnSpace = new QShortcut(QKeySequence(Qt::Key_Space), &okDialog);
        connect(closeOnSpace, &QShortcut::activated, &okDialog, &QDialog::accept);
        auto* closeOnReturn = new QShortcut(QKeySequence(Qt::Key_Return), &okDialog);
        connect(closeOnReturn, &QShortcut::activated, &okDialog, &QDialog::accept);
        auto* closeOnEnter = new QShortcut(QKeySequence(Qt::Key_Enter), &okDialog);
        connect(closeOnEnter, &QShortcut::activated, &okDialog, &QDialog::accept);
        okDialog.exec();
        return true;
    }

    statusBar()->showMessage(
        uiText("status.syntax.failed_counts", "Syntax check failed: %1 error(s), %2 warning(s).")
            .arg(entry.errorCount)
            .arg(entry.warningCount)
    );
    return false;
}

void MainWindow::onValidateSimai()
{
    (void)runValidateSimai();
}
