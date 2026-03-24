namespace {

enum class ValidationSeverityLevel {
    Error,
    Warning,
};

struct ValidationMessageParts {
    QString severityPrefix;
    QString body;
    ValidationSeverityLevel severity = ValidationSeverityLevel::Error;
};

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
    item->setData(Qt::UserRole, line);
    item->setData(Qt::UserRole + 1, col);
    item->setData(Qt::UserRole + 2, second);
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
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(label);

    list->setItemWidget(item, rowWidget);
    relayoutWrappedListRows(list);
    return item;
}

void MainWindow::relayoutWrappedListRows(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }

    const int rowWidth = qMax(220, list->viewport()->width() - 12);
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
        label->setFixedWidth(rowWidth);
        label->adjustSize();
        item->setSizeHint(QSize(rowWidth, qMax(26, label->sizeHint().height() + 4)));
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
        || editorValidationWarningCountLabel_ == nullptr) {
        return;
    }

    if (!hasActiveDifficulty()) {
        editorValidationSummaryWidget_->hide();
        return;
    }

    int errorCount = 0;
    int warningCount = 0;
    const auto cacheIt = validationCacheByDifficulty_.constFind(activeDifficultyId());
    if (cacheIt != validationCacheByDifficulty_.constEnd()) {
        const ValidationCacheEntry& entry = cacheIt.value();
        if (entry.chartText == activeChartText() && entry.chineseUi == UiText::isChineseUi()) {
            errorCount = entry.errorCount;
            warningCount = entry.warningCount;
        }
    }
    const int muriIssueCount = muriAnalysisReport_.diagnostics.size() + muriStaticReferences_.size();
    errorCount += muriIssueCount;

    const QColor mutedColor = UiTheme::colors().textMuted;
    const QColor errorColor = errorCount > 0 ? QColor(QStringLiteral("#D94A4A")) : mutedColor;
    const QColor warningColor = warningCount > 0 ? QColor(QStringLiteral("#D4A12A")) : mutedColor;
    const bool showError = errorCount > 0;
    const bool showWarning = warningCount > 0;

    editorValidationErrorIconLabel_->setPixmap(makeEditorValidationSummaryIcon(errorColor, false));
    editorValidationErrorCountLabel_->setText(QString::number(errorCount));
    editorValidationErrorCountLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor.name(QColor::HexRgb)));
    editorValidationErrorIconLabel_->setVisible(showError);
    editorValidationErrorCountLabel_->setVisible(showError);

    editorValidationWarningIconLabel_->setPixmap(makeEditorValidationSummaryIcon(warningColor, true));
    editorValidationWarningCountLabel_->setText(QString::number(warningCount));
    editorValidationWarningCountLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(warningColor.name(QColor::HexRgb)));
    editorValidationWarningIconLabel_->setVisible(showWarning);
    editorValidationWarningCountLabel_->setVisible(showWarning);

    const QString summaryTooltip = muriIssueCount > 0
        ? uiText(
              "editor.validation_summary.tooltip_with_muri",
              "%1 error(s), %2 warning(s) (%3 muri issue(s) included)"
          ).arg(errorCount).arg(warningCount).arg(muriIssueCount)
        : uiText(
              "editor.validation_summary.tooltip",
              "%1 error(s), %2 warning(s)"
          ).arg(errorCount).arg(warningCount);
    const bool showSummary = previewShowValidationSummary_ && (showError || showWarning);
    editorValidationSummaryWidget_->setProperty("hasContent", showSummary);
    editorValidationSummaryWidget_->setToolTip(summaryTooltip);
    editorValidationErrorIconLabel_->setToolTip(summaryTooltip);
    editorValidationErrorCountLabel_->setToolTip(summaryTooltip);
    editorValidationWarningIconLabel_->setToolTip(summaryTooltip);
    editorValidationWarningCountLabel_->setToolTip(summaryTooltip);
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

void MainWindow::addValidationError(int line, int col, const QString& message)
{
    if (errorList_ == nullptr) {
        return;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    const QColor sevColor = severityColor(parts.severity);
    const QString headerText = QStringLiteral("%1 L%2 C%3")
        .arg(parts.severityPrefix, QString::number(line), QString::number(col));
    const QString plainText = parts.body.isEmpty()
        ? headerText
        : QStringLiteral("%1\n%2").arg(headerText, parts.body);

    const QString headerHtml = QStringLiteral(
        "<span style=\"font-weight:700;color:%1;\">%2</span> "
        "<span style=\"color:%5;\">L%3 C%4</span>"
    )
        .arg(
            sevColor.name(),
            parts.severityPrefix.toHtmlEscaped(),
            QString::number(line),
            QString::number(col),
            UiTheme::colors().textSecondary.name(QColor::HexRgb)
        );
    const QString detailHtml = parts.body.isEmpty()
        ? QString()
        : QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
              .arg(UiTheme::colors().textPrimary.name(QColor::HexRgb), highlightValidationDetailHtml(parts.body));
    QListWidgetItem* item = addWrappedListEntry(errorList_, headerHtml + detailHtml, plainText, line, col, -1.0, true);
    if (item != nullptr) {
        item->setData(Qt::UserRole + 2, parts.severity == ValidationSeverityLevel::Warning ? 1 : 0);
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
    refreshEditorExtraSelections();
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
    const int line = item->data(Qt::UserRole).toInt();
    const int col = item->data(Qt::UserRole + 1).toInt();
    jumpToLocation(line, col);
}

void MainWindow::onMuriItemActivated(QListWidgetItem* item)
{
    if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) {
        return;
    }
    const int line = item->data(Qt::UserRole).toInt();
    const int col = item->data(Qt::UserRole + 1).toInt();
    const double second = item->data(Qt::UserRole + 2).toDouble();
    if (second >= 0.0) {
        navigateTimelineToSecond(second, true);
        return;
    }
    jumpToLocation(line, col);
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

    struct MuriPanelEntry {
        bool isStatic = false;
        MuriKind kind = MuriKind::Overlap;
        double second = 0.0;
        int line = 1;
        int col = 1;
        QString detail;
    };

    QVector<MuriPanelEntry> entries;
    entries.reserve(muriAnalysisReport_.diagnostics.size() + muriStaticReferences_.size());
    for (const MuriDiagnostic& diagnostic : muriAnalysisReport_.diagnostics) {
        MuriPanelEntry entry;
        entry.kind = diagnostic.kind;
        entry.second = diagnostic.second;
        entry.line = diagnostic.line;
        entry.col = diagnostic.col;
        entry.detail = diagnostic.detail;
        entries.append(entry);
    }
    for (const MuriStaticReference& reference : muriStaticReferences_) {
        MuriPanelEntry entry;
        entry.isStatic = true;
        entry.kind = reference.kind;
        entry.second = reference.affected.second;
        entry.line = reference.affected.line;
        entry.col = reference.affected.col;
        const QString causeText = QStringLiteral("L%1 C%2")
            .arg(reference.cause.line)
            .arg(reference.cause.col);
        if (reference.hasDelta) {
            entry.detail = UiText::isChineseUi()
                ? QStringLiteral("静态参考  Δ %1 ms  原因 %2")
                      .arg(QString::number(reference.deltaSecond * 1000.0, 'f', 1), causeText)
                : QStringLiteral("Static reference  Δ %1 ms  Cause %2")
                      .arg(QString::number(reference.deltaSecond * 1000.0, 'f', 1), causeText);
        } else {
            entry.detail = UiText::isChineseUi()
                ? QStringLiteral("静态参考  原因 %1").arg(causeText)
                : QStringLiteral("Static reference  Cause %1").arg(causeText);
        }
        entries.append(entry);
    }
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
        if (a.isStatic != b.isStatic) {
            return !a.isStatic && b.isStatic;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });

    for (const MuriPanelEntry& entry : entries) {
        const QString title = muriKindDisplayName(entry.kind, UiText::isChineseUi());
        const QString staticBadge = entry.isStatic
            ? QStringLiteral("<span style=\"font-weight:700;color:%1;\">[%2]</span> ")
                  .arg(UiTheme::colors().accent.name(QColor::HexRgb),
                       UiText::isChineseUi() ? QStringLiteral("静态") : QStringLiteral("Static"))
            : QString();
        const QString summary = QStringLiteral("%1<span style=\"font-weight:700;color:%2;\">[%3]</span> "
                                               "<span style=\"color:%4;\">%5</span>  "
                                               "<span style=\"color:%6;\">L%7 C%8</span>")
                                    .arg(
                                        staticBadge,
                                        entry.isStatic ? QColor(QStringLiteral("#C48A1A")).name(QColor::HexRgb)
                                                       : QColor(QStringLiteral("#D45B5B")).name(QColor::HexRgb),
                                        formatPreviewTimestamp(entry.second),
                                        UiTheme::colors().textPrimary.name(QColor::HexRgb),
                                        title.toHtmlEscaped(),
                                        UiTheme::colors().textSecondary.name(QColor::HexRgb),
                                        QString::number(entry.line),
                                        QString::number(entry.col));
        const QString detailHtml = entry.detail.isEmpty()
            ? QString()
            : QStringLiteral("<br/><span style=\"color:%1;\">%2</span>")
                  .arg(UiTheme::colors().textPrimary.name(QColor::HexRgb), entry.detail.toHtmlEscaped());
        const QString plainText = QStringLiteral("[%1] %2  L%3 C%4%5%6")
                                      .arg(formatPreviewTimestamp(entry.second))
                                      .arg(title)
                                      .arg(entry.line)
                                      .arg(entry.col)
                                      .arg(entry.isStatic ? QStringLiteral(" [Static]") : QString())
                                      .arg(entry.detail.isEmpty() ? QString() : QStringLiteral("\n") + entry.detail);
        addWrappedListEntry(muriList_, summary + detailHtml, plainText, entry.line, entry.col, entry.second, true);
    }
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
        addValidationError(issue.line, issue.col, issue.displayMessage);
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    updateEditorValidationSummary();
}

void MainWindow::scheduleAutoValidation()
{
    if (validationRefreshTimer_ == nullptr) {
        return;
    }
    validationRefreshTimer_->stop();
    if (!hasActiveDifficulty()) {
        refreshValidationPanelForActiveField();
        return;
    }
    validationRefreshTimer_->start();
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

    ValidationCacheEntry entry;
    const auto cacheIt = validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->chineseUi == chineseUi) {
        entry = cacheIt.value();
    } else {
        const SimaiNativeValidationLocale locale = chineseUi
            ? SimaiNativeValidationLocale::Chinese
            : SimaiNativeValidationLocale::English;
        const SimaiNativeValidationReport report = SimaiNativeParser::buildValidationReport(chartText, locale);
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
            cachedIssue.displayMessage = issue.displayMessage;
            entry.issues.append(cachedIssue);
        }
        validationCacheByDifficulty_.insert(difficultyId, entry);
    }

    setValidationTabVisible(true);
    clearValidationErrors();
    clearValidationDecorations();
    for (const ValidationCachedIssue& issue : entry.issues) {
        addValidationError(issue.line, issue.col, issue.displayMessage);
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
    updateEditorValidationSummary();
    if (focusFirstIssue && !entry.issues.isEmpty() && bottomTabs_ != nullptr && errorList_ != nullptr) {
        const int errorTabIndex = bottomTabs_->indexOf(errorList_);
        if (errorTabIndex >= 0) {
            bottomTabs_->setCurrentIndex(errorTabIndex);
        }
        onErrorItemActivated(errorList_->item(0));
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

    ValidationCacheEntry entry;
    const auto cacheIt = validationCacheByDifficulty_.constFind(difficultyId);
    if (cacheIt != validationCacheByDifficulty_.constEnd()
        && cacheIt->chartText == chartText
        && cacheIt->chineseUi == chineseUi) {
        entry = cacheIt.value();
    } else {
        const SimaiNativeValidationLocale locale = chineseUi
            ? SimaiNativeValidationLocale::Chinese
            : SimaiNativeValidationLocale::English;
        const SimaiNativeValidationReport report = SimaiNativeParser::buildValidationReport(chartText, locale);
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
            cachedIssue.displayMessage = issue.displayMessage;
            entry.issues.append(cachedIssue);
        }
        validationCacheByDifficulty_.insert(difficultyId, entry);
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
        addValidationError(issue.line, issue.col, issue.displayMessage);
        addValidationDecoration(issue.line, issue.col, issue.displayMessage, issue.endCol);
    }
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
