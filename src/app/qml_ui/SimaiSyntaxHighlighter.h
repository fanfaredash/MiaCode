#pragma once

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlockUserData>
#include <QTextDocument>
#include <QVector>
#include <QVariantList>
#include <QtQmlIntegration>

// simai 源码编辑器的原生高亮器。它直接格式化 TextArea 使用的
// QTextDocument，因此输入法预编辑、选区、光标和文本布局始终共享同一份文档。
//
// 词法分类沿用工作台规格的五类高亮：指令（BPM、拍号、HS 变速）、时值、
// 修饰符、数字和注释；括号作用域状态机参考 MiaCode 的 BracketScopeHighlighter，
// 使 ( { [ 的着色可以跨行延续，并避免把滑条方向 < > 误判成指令块。
class SimaiSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(QColor keywordColor READ keywordColor WRITE setKeywordColor NOTIFY keywordColorChanged)
    Q_PROPERTY(QColor commentColor READ commentColor WRITE setCommentColor NOTIFY commentColorChanged)
    Q_PROPERTY(QColor durationColor READ durationColor WRITE setDurationColor NOTIFY durationColorChanged)
    Q_PROPERTY(QColor modifierColor READ modifierColor WRITE setModifierColor NOTIFY modifierColorChanged)
    Q_PROPERTY(QColor errorColor READ errorColor WRITE setErrorColor NOTIFY errorColorChanged)
    Q_PROPERTY(QColor warningColor READ warningColor WRITE setWarningColor NOTIFY warningColorChanged)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics WRITE setDiagnostics NOTIFY diagnosticsChanged)

public:
    explicit SimaiSyntaxHighlighter(QObject* parent = nullptr);

    // 返回每个逻辑行顶部在文档坐标系中的 y 坐标。TextArea 开启自动换行后
    // 行高不再固定，行号 gutter 需要按真实行顶对齐而不是按行数乘固定行高。
    Q_INVOKABLE QVariantList lineTopPositions() const;

    QQuickTextDocument* textDocument() const;
    QColor keywordColor() const;
    QColor commentColor() const;
    QColor durationColor() const;
    QColor modifierColor() const;
    QColor errorColor() const;
    QColor warningColor() const;
    QVariantList diagnostics() const;

    void setTextDocument(QQuickTextDocument* value);
    void setKeywordColor(const QColor& value);
    void setCommentColor(const QColor& value);
    void setDurationColor(const QColor& value);
    void setModifierColor(const QColor& value);
    void setErrorColor(const QColor& value);
    void setWarningColor(const QColor& value);
    void setDiagnostics(const QVariantList& value);

signals:
    void textDocumentChanged();
    void keywordColorChanged();
    void commentColorChanged();
    void durationColorChanged();
    void modifierColorChanged();
    void errorColorChanged();
    void warningColorChanged();
    void diagnosticsChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    enum class BracketKind {
        Paren = 0,
        Brace = 1,
        Square = 2,
    };

    struct StackEntry {
        BracketKind kind = BracketKind::Paren;
        QChar closing;
    };

    // 行末括号栈持久化到 block user data，下一行从上一行恢复，
    // 未闭合的指令/时值块颜色可以跨行延续。
    class BlockData final : public QTextBlockUserData
    {
    public:
        QVector<StackEntry> stackAtEnd;
    };

    static bool isOpeningBracket(QChar ch, BracketKind* kindOut, QChar* closingOut);
    static bool isClosingBracket(QChar ch, BracketKind* kindOut);
    static QString modifierCharacters();
    QTextCharFormat formatForKind(BracketKind kind) const;
    void applyDiagnostics(const QString& text);

    QPointer<QQuickTextDocument> textDocument_;
    // 颜色默认不在此处定义，统一由 QML 侧从 Theme.colors.syntax 绑定注入，
    // Theme.qml 是语法颜色的唯一事实来源。
    QColor keywordColor_;
    QColor commentColor_;
    QColor durationColor_;
    QColor modifierColor_;
    QColor errorColor_;
    QColor warningColor_;
    QVariantList diagnostics_;
    // 诊断按行索引，避免每次高亮块时遍历全部诊断。
    QHash<int, QVariantList> diagnosticsByLine_;
};

