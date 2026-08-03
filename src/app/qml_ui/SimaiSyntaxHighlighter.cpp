#include "SimaiSyntaxHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextDocument>
#include <QVariantMap>

SimaiSyntaxHighlighter::SimaiSyntaxHighlighter(QObject* parent)
    : QSyntaxHighlighter(parent)
{
}

QVariantList SimaiSyntaxHighlighter::lineTopPositions() const
{
    QVariantList positions;
    if (!textDocument_ || !textDocument_->textDocument()) {
        return positions;
    }
    QTextDocument* document = textDocument_->textDocument();
    qreal firstTop = 0.0;
    bool first = true;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const qreal top = document->documentLayout()->blockBoundingRect(block).y();
        if (first) {
            firstTop = top;
            first = false;
        }
        positions.append(top - firstTop);
    }
    return positions;
}

QQuickTextDocument* SimaiSyntaxHighlighter::textDocument() const
{
    return textDocument_;
}

QColor SimaiSyntaxHighlighter::keywordColor() const
{
    return keywordColor_;
}

QColor SimaiSyntaxHighlighter::commentColor() const { return commentColor_; }
QColor SimaiSyntaxHighlighter::durationColor() const { return durationColor_; }
QColor SimaiSyntaxHighlighter::modifierColor() const { return modifierColor_; }
QColor SimaiSyntaxHighlighter::errorColor() const { return errorColor_; }
QColor SimaiSyntaxHighlighter::warningColor() const { return warningColor_; }
QVariantList SimaiSyntaxHighlighter::diagnostics() const { return diagnostics_; }

void SimaiSyntaxHighlighter::setTextDocument(QQuickTextDocument* value)
{
    if (textDocument_ == value) {
        return;
    }

    textDocument_ = value;
    setDocument(value != nullptr ? value->textDocument() : nullptr);
    emit textDocumentChanged();
}

void SimaiSyntaxHighlighter::setKeywordColor(const QColor& value)
{
    if (keywordColor_ == value) {
        return;
    }

    keywordColor_ = value;
    rehighlight();
    emit keywordColorChanged();
}

void SimaiSyntaxHighlighter::setCommentColor(const QColor& value)
{
    if (commentColor_ == value) return;
    commentColor_ = value;
    rehighlight();
    emit commentColorChanged();
}

void SimaiSyntaxHighlighter::setDurationColor(const QColor& value)
{
    if (durationColor_ == value) return;
    durationColor_ = value;
    rehighlight();
    emit durationColorChanged();
}

void SimaiSyntaxHighlighter::setModifierColor(const QColor& value)
{
    if (modifierColor_ == value) return;
    modifierColor_ = value;
    rehighlight();
    emit modifierColorChanged();
}

void SimaiSyntaxHighlighter::setErrorColor(const QColor& value)
{
    if (errorColor_ == value) return;
    errorColor_ = value;
    rehighlight();
    emit errorColorChanged();
}

void SimaiSyntaxHighlighter::setWarningColor(const QColor& value)
{
    if (warningColor_ == value) return;
    warningColor_ = value;
    rehighlight();
    emit warningColorChanged();
}

void SimaiSyntaxHighlighter::setDiagnostics(const QVariantList& value)
{
    if (diagnostics_ == value) return;
    diagnostics_ = value;
    diagnosticsByLine_.clear();
    for (const QVariant& item : diagnostics_) {
        const QVariantMap diagnostic = item.toMap();
        const int line = diagnostic.value(QStringLiteral("line")).toInt();
        diagnosticsByLine_[line].append(item);
    }
    rehighlight();
    emit diagnosticsChanged();
}

bool SimaiSyntaxHighlighter::isOpeningBracket(QChar ch, BracketKind* kindOut, QChar* closingOut)
{
    if (ch == QLatin1Char('(')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Paren;
        if (closingOut != nullptr) *closingOut = QLatin1Char(')');
        return true;
    }
    if (ch == QLatin1Char('{')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Brace;
        if (closingOut != nullptr) *closingOut = QLatin1Char('}');
        return true;
    }
    if (ch == QLatin1Char('[')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Square;
        if (closingOut != nullptr) *closingOut = QLatin1Char(']');
        return true;
    }
    return false;
}

bool SimaiSyntaxHighlighter::isClosingBracket(QChar ch, BracketKind* kindOut)
{
    if (ch == QLatin1Char(')')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Paren;
        return true;
    }
    if (ch == QLatin1Char('}')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Brace;
        return true;
    }
    if (ch == QLatin1Char(']')) {
        if (kindOut != nullptr) *kindOut = BracketKind::Square;
        return true;
    }
    return false;
}

QTextCharFormat SimaiSyntaxHighlighter::formatForKind(BracketKind kind) const
{
    QTextCharFormat format;
    switch (kind) {
    case BracketKind::Paren:
    case BracketKind::Brace:
        format.setForeground(keywordColor_);
        break;
    case BracketKind::Square:
        format.setForeground(durationColor_);
        break;
    }
    return format;
}

QString SimaiSyntaxHighlighter::modifierCharacters()
{
    // simai note 语法中的修饰符与方向符号：hold h、break b、ex x、mine m、
    // 烟花 f、滑条方向 - > < ^ v V、滑条类型 p q s w、touch z、star $ @、
    // 无头滑条 ? !、星形分支 *，以及 each 分隔 / 与反引号。
    static const QString characters = QStringLiteral("-^vV<>pqszwWbxhmf$@?!*/`");
    return characters;
}

void SimaiSyntaxHighlighter::applyDiagnostics(const QString& text)
{
    const QColor errorColor = errorColor_;
    const QColor warningColor = warningColor_;
    const int line = currentBlock().blockNumber() + 1;
    const QVariantList lineDiagnostics = diagnosticsByLine_.value(line);
    for (const QVariant& value : lineDiagnostics) {
        const QVariantMap diagnostic = value.toMap();
        const int start = qBound(0, diagnostic.value(QStringLiteral("column")).toInt() - 1, text.size());
        const int end = qBound(start + 1, diagnostic.value(QStringLiteral("endColumn")).toInt(), qMax(start + 1, text.size()));
        const int length = qMax(1, end - start);
        for (int index = start; index < qMin(text.size(), start + length); ++index) {
            QTextCharFormat diagnosticFormat = format(index);
            diagnosticFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            diagnosticFormat.setUnderlineColor(
                diagnostic.value(QStringLiteral("severity")).toString() == QLatin1String("warning")
                    ? warningColor
                    : errorColor);
            setFormat(index, 1, diagnosticFormat);
        }
    }
}

void SimaiSyntaxHighlighter::highlightBlock(const QString& text)
{
    // 恢复上一行结束时的括号作用域，未闭合的 ( { [ 着色跨行延续。
    QVector<StackEntry> stack;
    const QTextBlock previous = currentBlock().previous();
    if (previous.isValid()) {
        if (const auto* previousData = dynamic_cast<const BlockData*>(previous.userData()); previousData != nullptr) {
            stack = previousData->stackAtEnd;
        }
    }

    const int commentStart = text.indexOf(QStringLiteral("||"));
    const int syntaxEnd = commentStart >= 0 ? commentStart : text.size();

    QTextCharFormat directiveFormat;
    directiveFormat.setForeground(keywordColor_);
    QTextCharFormat durationFormat;
    durationFormat.setForeground(durationColor_);
    QTextCharFormat modifierFormat;
    modifierFormat.setForeground(modifierColor_);

    // 颜色由 QML 从 Theme.colors.syntax 注入；绑定生效前或主题缺失时
    // 跳过对应分类，避免用无效色覆盖文本。
    const bool hasKeyword = keywordColor_.isValid();
    const bool hasDuration = durationColor_.isValid();
    const bool hasModifier = modifierColor_.isValid();

    for (int index = 0; index < syntaxEnd;) {
        const QChar ch = text.at(index);

        // 括号作用域内的文字沿用最内层括号的颜色，与 MiaCode
        // BracketScopeHighlighter 的逐字符作用域着色保持一致。
        const bool insideScope = !stack.isEmpty();
        if (insideScope) {
            const QTextCharFormat scopeFormat = formatForKind(stack.constLast().kind);
            if (scopeFormat.foreground().style() != Qt::NoBrush) {
                setFormat(index, 1, scopeFormat);
            }
        }

        // <HS*N> 变速指令整体作为指令染色。单独的 < > 属于滑条方向，
        // 走下面的修饰符分支，避免把 1<5 这类滑条误染成指令块。
        if (ch == QLatin1Char('<')
            && index + 4 <= syntaxEnd
            && text.mid(index + 1, 3) == QLatin1String("HS*")) {
            const int close = text.indexOf(QLatin1Char('>'), index + 1);
            if (hasKeyword && close >= 0 && close < syntaxEnd) {
                setFormat(index, close - index + 1, directiveFormat);
                index = close + 1;
                continue;
            }
        }

        BracketKind bracketKind;
        QChar closing;
        if (isOpeningBracket(ch, &bracketKind, &closing)) {
            const QTextCharFormat bracketFormat = formatForKind(bracketKind);
            if (bracketFormat.foreground().style() != Qt::NoBrush) {
                setFormat(index, 1, bracketFormat);
            }
            stack.append({bracketKind, closing});
            ++index;
            continue;
        }

        if (isClosingBracket(ch, &bracketKind)) {
            const QTextCharFormat bracketFormat = formatForKind(bracketKind);
            if (bracketFormat.foreground().style() != Qt::NoBrush) {
                setFormat(index, 1, bracketFormat);
            }
            if (!stack.isEmpty()) {
                if (stack.constLast().closing == ch) {
                    stack.removeLast();
                } else {
                    // 闭括号与栈顶不匹配时，向栈内找最近的同类闭括号，
                    // 丢弃中间的未闭合块，与 MiaCode 行为一致。
                    int matchingIndex = -1;
                    for (int i = stack.size() - 1; i >= 0; --i) {
                        if (stack.at(i).closing == ch) {
                            matchingIndex = i;
                            break;
                        }
                    }
                    if (matchingIndex >= 0) {
                        stack.resize(matchingIndex);
                    }
                }
            }
            ++index;
            continue;
        }

        if (!insideScope && hasModifier && modifierCharacters().contains(ch)) {
            setFormat(index, 1, modifierFormat);
        }
        ++index;
    }

    if (commentStart >= 0 && commentColor_.isValid()) {
        QTextCharFormat commentFormat;
        commentFormat.setForeground(commentColor_);
        setFormat(commentStart, text.size() - commentStart, commentFormat);
    }

    applyDiagnostics(text);

    auto* data = new BlockData();
    data->stackAtEnd = stack;
    setCurrentBlockUserData(data);

    // 把行末括号栈镜像到整型 block state：QSyntaxHighlighter 只在前一块的
    // userState() 变化时重高亮下一块，这里把作用域签名编码进去，保证
    // 上一行增删括号后下一行的着色立即刷新。
    unsigned signature = 0;
    for (const StackEntry& entry : stack) {
        signature = signature * 31u + static_cast<unsigned>(entry.kind) + 1u;
    }
    setCurrentBlockState(static_cast<int>(signature & 0x7fffffffu));
}

