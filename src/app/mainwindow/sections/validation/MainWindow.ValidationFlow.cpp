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

}  // namespace

void MainWindow::clearValidationErrors()
{
    if (errorList_ == nullptr) {
        return;
    }
    errorList_->clear();
}

void MainWindow::clearValidationDecorations()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    if (editor == nullptr) {
        return;
    }
    editor->setExtraSelections({});
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

    auto* item = new QListWidgetItem(errorList_);
    item->setToolTip(plainText);
    item->setData(Qt::UserRole, line);
    item->setData(Qt::UserRole + 1, col);
    item->setData(Qt::UserRole + 2, parts.severity == ValidationSeverityLevel::Warning ? 1 : 0);
    item->setText(QString());

    auto* label = new QLabel(errorList_);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setStyleSheet(UiTheme::validationMessageLabelStyleSheet());

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
    label->setText(headerHtml + detailHtml);

    auto* rowWidget = new QWidget(errorList_);
    auto* rowLayout = new QVBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(label);

    const int rowWidth = qMax(220, errorList_->viewport()->width() - 12);
    label->setFixedWidth(rowWidth);
    label->adjustSize();
    item->setSizeHint(QSize(rowWidth, qMax(26, label->sizeHint().height() + 4)));
    errorList_->setItemWidget(item, rowWidget);
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
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }

    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }

    const ValidationMessageParts parts = parseValidationMessage(message);
    const QColor underlineColor = severityColor(parts.severity);

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qMax(0, col - 1));
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection sel;
    sel.cursor = cursor;
    sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    sel.format.setUnderlineColor(underlineColor);
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

void MainWindow::clearValidationCache()
{
    validationCacheByDifficulty_.clear();
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
        return;
    }

    setValidationTabVisible(true);
    const int difficultyId = activeDifficultyId();
    const auto it = validationCacheByDifficulty_.constFind(difficultyId);
    if (it == validationCacheByDifficulty_.constEnd()) {
        clearValidationErrors();
        clearValidationDecorations();
        return;
    }

    const QString chartText = activeChartText();
    const bool chineseUi = UiText::isChineseUi();
    const ValidationCacheEntry& entry = it.value();
    if (entry.chartText != chartText || entry.chineseUi != chineseUi) {
        clearValidationErrors();
        clearValidationDecorations();
        return;
    }

    clearValidationErrors();
    clearValidationDecorations();
    for (const ValidationCachedIssue& issue : entry.issues) {
        addValidationError(issue.line, issue.col, issue.displayMessage);
        addValidationDecoration(issue.line, issue.col, issue.displayMessage);
    }
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
        addValidationDecoration(issue.line, issue.col, issue.displayMessage);
    }
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
