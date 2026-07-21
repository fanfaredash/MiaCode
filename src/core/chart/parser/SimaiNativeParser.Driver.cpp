namespace ValidationMessage {

const QString& kInvalidNotePrefix()
{
    static const QString value = QStringLiteral("Invalid note: ");
    return value;
}

const QString& kUnterminatedBpmBlock()
{
    static const QString value = QStringLiteral("Unterminated BPM block");
    return value;
}

const QString& kInvalidBpmValue()
{
    static const QString value = QStringLiteral("Invalid BPM value");
    return value;
}

const QString& kUnterminatedBeatBlock()
{
    static const QString value = QStringLiteral("Unterminated beat block");
    return value;
}

const QString& kInvalidBeatValue()
{
    static const QString value = QStringLiteral("Invalid beat value");
    return value;
}

const QString& kInvalidBeatValueStrictPrefix()
{
    static const QString value = QStringLiteral("Invalid beat value for strict mode: ");
    return value;
}

const QString& kStrictDivisorSuffix()
{
    static const QString value = QStringLiteral(" (must be a positive divisor of 384)");
    return value;
}

const QString& kBeatValueAbove384Prefix()
{
    static const QString value = QStringLiteral("Beat value above 384 may cause transfer issues: ");
    return value;
}

const QString& kUnterminatedHsBracket()
{
    static const QString value = QStringLiteral("Unterminated <HS*> bracket");
    return value;
}

const QString& kInvalidHsValue()
{
    static const QString value = QStringLiteral("Invalid <HS*N> value");
    return value;
}

const QString& kInvalidBreakSlideModifierPositionPrefix()
{
    static const QString value = QStringLiteral("Invalid break slide modifier position: ");
    return value;
}

const QString& kInvalidSlideDurationPlacementPrefix()
{
    static const QString value = QStringLiteral("Invalid slide duration placement: ");
    return value;
}

const QString& kInvalidSlideDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid slide duration: ");
    return value;
}

const QString& kInvalidHoldDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid hold duration: ");
    return value;
}

const QString& kInvalidHoldModifierSequencePrefix()
{
    static const QString value = QStringLiteral("Invalid hold modifier sequence: ");
    return value;
}

const QString& kNonCanonicalHoldModifierPlacementPrefix()
{
    static const QString value = QStringLiteral("Non-canonical hold modifier placement: ");
    return value;
}

const QString& kInvalidTouchHoldDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid touch-hold duration: ");
    return value;
}

const QString& kTouchDurationRequiresHPrefix()
{
    static const QString value = QStringLiteral("Touch duration requires 'h': ");
    return value;
}

const QString& kInvalidTouchTokenPrefix()
{
    static const QString value = QStringLiteral("Invalid touch token: ");
    return value;
}

const QString& kNonCanonicalCenterTouchPrefix()
{
    static const QString value = QStringLiteral("Non-canonical center touch token (use C): ");
    return value;
}

const QString& kMisplacedSlideHeadModifierPrefix()
{
    static const QString value = QStringLiteral(
        "Slide head modifier (?, !, @) may only appear between slide head and shape: ");
    return value;
}

const QString& kMisplacedTapStarModifierPrefix()
{
    static const QString value = QStringLiteral(
        "Tap-star modifier ($) may only appear within a tap: ");
    return value;
}

const QString& kInvalidSlideChainPrefix()
{
    static const QString value = QStringLiteral("Invalid slide chain syntax: ");
    return value;
}

const QString& kInvalidTouchModifierPrefix()
{
    static const QString value = QStringLiteral("Invalid touch modifier: ");
    return value;
}

const QString& kFullwidthDigitPrefix()
{
    static const QString value = QStringLiteral("Fullwidth digit detected, use halfwidth digits: ");
    return value;
}

const QString& kFullwidthTouchRegionPrefix()
{
    static const QString value = QStringLiteral("Fullwidth touch region letter detected, use halfwidth region letters: ");
    return value;
}

const QString& kFullwidthModifierPrefix()
{
    static const QString value = QStringLiteral("Fullwidth modifier detected, use halfwidth modifiers: ");
    return value;
}

const QString& kFullwidthBracketPrefix()
{
    static const QString value = QStringLiteral("Fullwidth bracket detected, use halfwidth brackets: ");
    return value;
}

const QString& kFullwidthSeparatorPrefix()
{
    static const QString value = QStringLiteral("Fullwidth separator detected, use halfwidth separators: ");
    return value;
}

const QString& kFullwidthSlideSymbolPrefix()
{
    static const QString value = QStringLiteral("Fullwidth slide symbol detected, use halfwidth slide symbols: ");
    return value;
}

const QString& kFullwidthLatinLetterPrefix()
{
    static const QString value = QStringLiteral("Fullwidth latin letter detected, use halfwidth letters: ");
    return value;
}

const QString& kMissingBeatSeparator()
{
    static const QString value = QStringLiteral("Missing beat separator ','");
    return value;
}

const QString& kLineEndNoteMissingComma()
{
    static const QString value = QStringLiteral("Line-end note is missing trailing ','");
    return value;
}

const QString& kMissingCommaBeforeDirectivePrefix()
{
    static const QString value = QStringLiteral("Missing ',' between note and directive: ");
    return value;
}

const QString& kInvalidSlideStarBranchPrefix()
{
    static const QString value = QStringLiteral("Invalid '*' slide branch (must omit the slide head): ");
    return value;
}

const QString& kEmptySlideStarBranchPrefix()
{
    static const QString value = QStringLiteral("Invalid empty '*' slide branch: ");
    return value;
}

const QString& kRepeatedSlashSeparator()
{
    static const QString value = QStringLiteral("Repeated separator '//' is not allowed");
    return value;
}

const QString& kSeparatorMissingOperand()
{
    static const QString value = QStringLiteral("Separator '/' or '`' is missing an adjacent note");
    return value;
}

const QString& kUnmatchedClosingBracketPrefix()
{
    static const QString value = QStringLiteral("Unmatched closing bracket '");
    return value;
}

const QString& kUnclosedBracketPrefix()
{
    static const QString value = QStringLiteral("Unclosed bracket '");
    return value;
}

const QString& kChartEmpty()
{
    static const QString value = QStringLiteral("Chart is empty.");
    return value;
}

const QString& kInvalidTerminalMarkerPrefix()
{
    static const QString value = QStringLiteral("Invalid terminal marker placement: ");
    return value;
}

QString formatInvalidNote(const QString& token)
{
    return QStringLiteral("%1%2").arg(kInvalidNotePrefix(), token);
}

QString formatStrictBeatValue(int beats)
{
    return QStringLiteral("%1%2%3")
        .arg(kInvalidBeatValueStrictPrefix())
        .arg(beats)
        .arg(kStrictDivisorSuffix());
}

QString formatBeatValueClamped(int beats)
{
    return QStringLiteral("%1%2").arg(kBeatValueAbove384Prefix(), QString::number(beats));
}

const QHash<QString, QString>& zhExactMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBpmValue(), QStringLiteral("BPM 数值无效")},
        {kInvalidBeatValue(), QStringLiteral("分拍数值无效")},
        {kUnterminatedBpmBlock(), QStringLiteral("BPM 块未闭合")},
        {kUnterminatedBeatBlock(), QStringLiteral("分拍块未闭合")},
        {kUnterminatedHsBracket(), QStringLiteral("<HS*> 括号未闭合")},
        {kInvalidHsValue(), QStringLiteral("<HS*N> 数值无效")},
        {kMissingBeatSeparator(), QStringLiteral("缺少拍间分隔符 ','")},
        {kRepeatedSlashSeparator(), QStringLiteral("不允许使用连续分隔符 '//'")},
        {kSeparatorMissingOperand(), QStringLiteral("分隔符 '/' 或 '`' 缺少相邻音符")},
        {kChartEmpty(), QStringLiteral("谱面为空。")},
    };
    return map;
}

const QHash<QString, QString>& zhPrefixMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBreakSlideModifierPositionPrefix(), QStringLiteral("Break Slide 修饰符 b 位置可能导致转谱错误：")},
        {kInvalidSlideDurationPlacementPrefix(), QStringLiteral("Slide 时值块位置可能导致转谱错误：")},
        {kInvalidSlideDurationPrefix(), QStringLiteral("Slide 时值无效：")},
        {kInvalidHoldDurationPrefix(), QStringLiteral("Hold 时值无效：")},
        {kInvalidHoldModifierSequencePrefix(), QStringLiteral("Hold 修饰符顺序无效：")},
        {kNonCanonicalHoldModifierPlacementPrefix(), QStringLiteral("Hold 修饰符位置可能导致上机转换错误：")},
        {kInvalidTouchHoldDurationPrefix(), QStringLiteral("TouchHold 时值无效：")},
        {kTouchDurationRequiresHPrefix(), QStringLiteral("Touch 时值需要 'h' 修饰符：")},
        {kInvalidTouchTokenPrefix(), QStringLiteral("Touch 音符无效：")},
        {kNonCanonicalCenterTouchPrefix(), QStringLiteral("非典范的中心 Touch 音符（应使用 C）：")},
        {kInvalidTouchModifierPrefix(), QStringLiteral("Touch 修饰符无效：")},
        {kMisplacedSlideHeadModifierPrefix(), QStringLiteral("Slide head 修饰符（?、!、@）只能出现在 slide head 与 shape 之间：")},
        {kMisplacedTapStarModifierPrefix(), QStringLiteral("Tap-star 修饰符（$）只能出现在 tap 中：")},
        {kInvalidSlideChainPrefix(), QStringLiteral("Slide 段链语法无效：")},
        {kMissingCommaBeforeDirectivePrefix(), QStringLiteral("音符与指令（{} () <HS*>）之间缺少分隔符 ','：")},
        {kInvalidSlideStarBranchPrefix(), QStringLiteral("'*' 同头 Slide 分支语法无效（* 后必须省略 slide 头部，如 5q2[4:1]*p8[4:1]）：")},
        {kEmptySlideStarBranchPrefix(), QStringLiteral("'*' 同头 Slide 分支不能为空：")},
        {kFullwidthDigitPrefix(), QStringLiteral("检测到全角数字，请改用半角数字：")},
        {kFullwidthTouchRegionPrefix(), QStringLiteral("检测到全角触摸区域字母，请改用半角区域字母：")},
        {kFullwidthModifierPrefix(), QStringLiteral("检测到全角修饰符，请改用半角修饰符：")},
        {kFullwidthBracketPrefix(), QStringLiteral("检测到全角括号，请改用半角括号：")},
        {kFullwidthSeparatorPrefix(), QStringLiteral("检测到全角分隔符，请改用半角分隔符：")},
        {kFullwidthSlideSymbolPrefix(), QStringLiteral("检测到全角 Slide 符号，请改用半角符号：")},
        {kFullwidthLatinLetterPrefix(), QStringLiteral("检测到全角字母，请改用半角字母：")},
        {kInvalidTerminalMarkerPrefix(), QStringLiteral("终止标记 E 位置无效：")},
        {kInvalidNotePrefix(), QStringLiteral("音符无效：")},
        {kInvalidBeatValueStrictPrefix(), QStringLiteral("分拍数值可能导致转谱错误：")},
        {kBeatValueAbove384Prefix(), QStringLiteral("分拍数值大于 384，可能导致转谱错误：")},
        {kUnmatchedClosingBracketPrefix(), QStringLiteral("未匹配的右括号 '")},
        {kUnclosedBracketPrefix(), QStringLiteral("未闭合的左括号 '")},
    };
    return map;
}

const QHash<QString, QString>& jaExactMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBpmValue(), QStringLiteral("BPM の値が無効です")},
        {kInvalidBeatValue(), QStringLiteral("分音数の値が無効です")},
        {kUnterminatedBpmBlock(), QStringLiteral("BPM 括弧が閉じられていません")},
        {kUnterminatedBeatBlock(), QStringLiteral("分音括弧が閉じられていません")},
        {kUnterminatedHsBracket(), QStringLiteral("<HS*> 括弧が閉じられていません")},
        {kInvalidHsValue(), QStringLiteral("<HS*N> の値が無効です")},
        {kMissingBeatSeparator(), QStringLiteral("拍区切りの ',' がありません")},
        {kRepeatedSlashSeparator(), QStringLiteral("連続する区切り記号 '//' は使えません")},
        {kSeparatorMissingOperand(), QStringLiteral("区切り記号 '/' または '`' の隣にノーツがありません")},
        {kChartEmpty(), QStringLiteral("譜面が空です。")},
    };
    return map;
}

const QHash<QString, QString>& jaPrefixMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBreakSlideModifierPositionPrefix(), QStringLiteral("Break Slide の修飾子 b の位置により譜面変換でエラーになる可能性があります：")},
        {kInvalidSlideDurationPlacementPrefix(), QStringLiteral("Slide の音価ブロックの位置により譜面変換でエラーになる可能性があります：")},
        {kInvalidSlideDurationPrefix(), QStringLiteral("Slide の音価が無効です：")},
        {kInvalidHoldDurationPrefix(), QStringLiteral("Hold の音価が無効です：")},
        {kInvalidHoldModifierSequencePrefix(), QStringLiteral("Hold の修飾子の順序が無効です：")},
        {kNonCanonicalHoldModifierPlacementPrefix(), QStringLiteral("Hold の修飾子の位置により実機変換でエラーになる可能性があります：")},
        {kInvalidTouchHoldDurationPrefix(), QStringLiteral("TouchHold の音価が無効です：")},
        {kTouchDurationRequiresHPrefix(), QStringLiteral("Touch の音価には 'h' 修飾子が必要です：")},
        {kInvalidTouchTokenPrefix(), QStringLiteral("Touch ノーツが無効です：")},
        {kNonCanonicalCenterTouchPrefix(), QStringLiteral("中央 Touch の非標準表記です（C を使ってください）：")},
        {kInvalidTouchModifierPrefix(), QStringLiteral("Touch の修飾子が無効です：")},
        {kMisplacedSlideHeadModifierPrefix(), QStringLiteral("Slide head 修飾子（?、!、@）は slide head と shape の間にのみ置けます：")},
        {kMisplacedTapStarModifierPrefix(), QStringLiteral("Tap-star 修飾子（$）は tap にのみ置けます：")},
        {kInvalidSlideChainPrefix(), QStringLiteral("Slide チェーンの構文が無効です：")},
        {kMissingCommaBeforeDirectivePrefix(), QStringLiteral("ノーツと指令（{} () <HS*>）の間に区切りの ',' がありません：")},
        {kInvalidSlideStarBranchPrefix(), QStringLiteral("'*' による同一始点 Slide 分岐の構文が無効です（* の後は slide 頭部を省略します。例：5q2[4:1]*p8[4:1]）：")},
        {kEmptySlideStarBranchPrefix(), QStringLiteral("'*' による同一始点 Slide 分岐を空にはできません：")},
        {kFullwidthDigitPrefix(), QStringLiteral("全角数字が見つかりました。半角数字を使ってください：")},
        {kFullwidthTouchRegionPrefix(), QStringLiteral("全角のタッチ領域文字が見つかりました。半角文字を使ってください：")},
        {kFullwidthModifierPrefix(), QStringLiteral("全角の修飾子が見つかりました。半角を使ってください：")},
        {kFullwidthBracketPrefix(), QStringLiteral("全角括弧が見つかりました。半角括弧を使ってください：")},
        {kFullwidthSeparatorPrefix(), QStringLiteral("全角の区切り記号が見つかりました。半角を使ってください：")},
        {kFullwidthSlideSymbolPrefix(), QStringLiteral("全角の Slide 記号が見つかりました。半角を使ってください：")},
        {kFullwidthLatinLetterPrefix(), QStringLiteral("全角の英字が見つかりました。半角を使ってください：")},
        {kInvalidTerminalMarkerPrefix(), QStringLiteral("終端マーカー E の位置が無効です：")},
        {kInvalidNotePrefix(), QStringLiteral("ノーツが無効です：")},
        {kInvalidBeatValueStrictPrefix(), QStringLiteral("分音数の値により譜面変換でエラーになる可能性があります：")},
        {kBeatValueAbove384Prefix(), QStringLiteral("分音数が 384 を超えています。譜面変換でエラーになる可能性があります：")},
        {kUnmatchedClosingBracketPrefix(), QStringLiteral("対応していない閉じ括弧 '")},
        {kUnclosedBracketPrefix(), QStringLiteral("閉じられていない開き括弧 '")},
    };
    return map;
}

const QVector<QString>& zhPrefixOrder()
{
    static const QVector<QString> order{
        kInvalidBeatValueStrictPrefix(),
        kBeatValueAbove384Prefix(),
        kInvalidBreakSlideModifierPositionPrefix(),
        kInvalidSlideDurationPlacementPrefix(),
        kInvalidSlideDurationPrefix(),
        kInvalidHoldDurationPrefix(),
        kInvalidHoldModifierSequencePrefix(),
        kNonCanonicalHoldModifierPlacementPrefix(),
        kInvalidTouchHoldDurationPrefix(),
        kTouchDurationRequiresHPrefix(),
        kInvalidTouchTokenPrefix(),
        kNonCanonicalCenterTouchPrefix(),
        kInvalidTouchModifierPrefix(),
        kMisplacedSlideHeadModifierPrefix(),
        kMisplacedTapStarModifierPrefix(),
        kInvalidSlideChainPrefix(),
        kMissingCommaBeforeDirectivePrefix(),
        kInvalidSlideStarBranchPrefix(),
        kEmptySlideStarBranchPrefix(),
        kFullwidthDigitPrefix(),
        kFullwidthTouchRegionPrefix(),
        kFullwidthModifierPrefix(),
        kFullwidthBracketPrefix(),
        kFullwidthSeparatorPrefix(),
        kFullwidthSlideSymbolPrefix(),
        kFullwidthLatinLetterPrefix(),
        kInvalidTerminalMarkerPrefix(),
        kInvalidNotePrefix(),
        kUnmatchedClosingBracketPrefix(),
        kUnclosedBracketPrefix(),
    };
    return order;
}

}  // namespace ValidationMessage

// Strict-only check. Returns the validation message to emit, or empty
// string if the token has no `?`, `!`, `@` (or all of them are validly
// placed in the slide-head region between the lane digit and the first
// slide-shape character).
QString detectMisplacedSlideHeadModifierMessage(const QString& token)
{
    bool hasSlideHeadCh = false;
    for (QChar ch : token) {
        if (ch == QChar('?') || ch == QChar('!') || ch == QChar('@')) {
            hasSlideHeadCh = true;
            break;
        }
    }
    if (!hasSlideHeadCh) {
        return QString();
    }

    // The token must contain a slide-shape character to be a slide. If
    // there is no shape character, `?`/`!`/`@` cannot be valid head
    // modifiers — they are slide-only. Examples that hit this branch:
    // `1!`, `1h@`, `A1?`.
    int firstShapeIdx = -1;
    for (int i = 1; i < token.size(); ++i) {
        if (isSlideShapeChar(token.at(i))) {
            firstShapeIdx = i;
            break;
        }
    }
    if (firstShapeIdx < 0) {
        return ValidationMessage::kMisplacedSlideHeadModifierPrefix() + token;
    }

    // Each `?`/`!`/`@` must sit at index in [1, firstShapeIdx). Order
    // relative to other head modifiers (`b`, `x`) is unrestricted —
    // parseSlideHeadModifierPrefix already accepts arbitrary
    // interleavings within the head region.
    for (int i = 0; i < token.size(); ++i) {
        const QChar ch = token.at(i);
        if (ch != QChar('?') && ch != QChar('!') && ch != QChar('@')) {
            continue;
        }
        if (i < 1 || i >= firstShapeIdx) {
            return ValidationMessage::kMisplacedSlideHeadModifierPrefix() + token;
        }
    }
    return QString();
}

// Strict-only check. Returns the validation message to emit, or empty
// string if the token has no `$` (or the `$` sits in a pure-tap context).
// "Pure tap" = lane-digit-led, no slide-shape character, no `[…]`
// duration block, no `h` modifier (case-insensitive). `$` may be freely
// combined with `b` / `x` in either order — the parser already accepts
// that combination.
QString detectMisplacedTapStarModifierMessage(const QString& token)
{
    if (!token.contains(QChar('$'))) {
        return QString();
    }
    if (token.isEmpty() || !isDigitLane(token.at(0))) {
        return ValidationMessage::kMisplacedTapStarModifierPrefix() + token;
    }
    if (tokenContainsSlideShape(token)) {
        return ValidationMessage::kMisplacedTapStarModifierPrefix() + token;
    }
    if (token.contains(QChar('['))) {
        return ValidationMessage::kMisplacedTapStarModifierPrefix() + token;
    }
    if (token.contains(QChar('h'), Qt::CaseInsensitive)) {
        return ValidationMessage::kMisplacedTapStarModifierPrefix() + token;
    }
    return QString();
}

void parseToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr) {
        return;
    }
    if (token.isEmpty()) {
        return;
    }

    if (const QString fullwidthIssue = detectFullwidthSyntaxIssueMessage(token); !fullwidthIssue.isEmpty()) {
        appendTokenError(state, lineNumber, column, fullwidthIssue, column + token.size() - 1);
        return;
    }

    // Strict-only character-placement checks. Emit before dispatching so
    // the user gets the specific diagnostic even when the downstream
    // parser would also reject the token with a more generic message.
    // Lenient mode is unchanged — these checks are gated on strictMode.
    if (state->strictMode) {
        if (const QString msg = detectMisplacedSlideHeadModifierMessage(token); !msg.isEmpty()) {
            appendTokenError(state, lineNumber, column, msg, column + token.size() - 1);
        }
        if (const QString msg = detectMisplacedTapStarModifierMessage(token); !msg.isEmpty()) {
            appendTokenError(state, lineNumber, column, msg, column + token.size() - 1);
        }
    }

    bool simpleDigitCluster = true;
    for (QChar ch : token) {
        if (!isDigitLane(ch)) {
            simpleDigitCluster = false;
            break;
        }
    }
    if (simpleDigitCluster && token.size() > 1) {
        for (int i = 0; i < token.size(); ++i) {
            parseTapOrHoldToken(state, token.mid(i, 1), lineNumber, column + i, groupIndices);
        }
        return;
    }

    if (isTouchPrefix(token)) {
        parseTouchToken(state, token, lineNumber, column, groupIndices);
        return;
    }
    if (isDigitLane(token.at(0))) {
        parseTapOrHoldToken(state, token, lineNumber, column, groupIndices);
        return;
    }

    appendTokenError(state, lineNumber, column, classifyInvalidNoteMessage(token));
}

bool isTerminalMarkerText(QString text)
{
    text = text.trimmed();
    return text.compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0;
}

bool lineTailIsTerminalMarker(const QString& line, int startIndex)
{
    if (startIndex < 0 || startIndex >= line.size()) {
        return false;
    }
    QString tail = line.mid(startIndex);
    const int commentIndex = tail.indexOf(QStringLiteral("||"));
    if (commentIndex >= 0) {
        tail = tail.left(commentIndex);
    }
    return isTerminalMarkerText(tail);
}

void applyInitialTimingMetadata(
    ParseState* state,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (state == nullptr) {
        return;
    }
    state->meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : kDefaultMeterNumerator;
    state->meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : kDefaultMeterDenominator;
}

struct TimedMarkerRef {
    int markerIndex = -1;
    double second = 0.0;
};

struct TouchWindowBuckets {
    QVector<TimedMarkerRef> slideHead;
    QVector<TimedMarkerRef> wifiHead;
    QVector<TimedMarkerRef> padEnter;
};

void sortTimedMarkerRefs(QVector<TimedMarkerRef>* refs)
{
    if (refs == nullptr || refs->size() < 2) {
        return;
    }
    std::sort(refs->begin(), refs->end(), [](const TimedMarkerRef& a, const TimedMarkerRef& b) {
        if (a.second != b.second) {
            return a.second < b.second;
        }
        return a.markerIndex < b.markerIndex;
    });
}

template<typename MarkFn>
void markOpenIntervalMatches(
    const QVector<TimedMarkerRef>& centers,
    const QVector<TimedMarkerRef>& queries,
    double lowerRadius,
    double upperRadius,
    MarkFn&& markFn,
    bool excludeSelf = false)
{
    if (centers.isEmpty() || queries.isEmpty()) {
        return;
    }

    int left = 0;
    int right = 0;
    const int centerCount = centers.size();
    for (const TimedMarkerRef& query : queries) {
        const double lowerBound = query.second - lowerRadius;
        const double upperBound = query.second + upperRadius;

        while (left < centerCount && !(centers[left].second > lowerBound)) {
            ++left;
        }
        if (right < left) {
            right = left;
        }
        while (right < centerCount && centers[right].second < upperBound) {
            ++right;
        }

        const int matchCount = right - left;
        if (matchCount <= 0) {
            continue;
        }
        if (excludeSelf && matchCount == 1 && centers[left].markerIndex == query.markerIndex) {
            continue;
        }
        markFn(query.markerIndex);
    }
}

SimaiNativeParseResult parseInternal(
    const QString& text,
    bool strictMode,
    bool allowInvalidStarFallback,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    ParseState state;
    state.strictMode = strictMode;
    state.allowInvalidStarFallback = allowInvalidStarFallback;
    applyInitialTimingMetadata(&state, timingMetadata);

    QString token;
    int tokenColumn = 1;
    QVector<int> currentGroup;
    // Strict-mode tracking for the each-group separators '/' and '`': a divider
    // must have a note on each side. eachOperandPending is true once a note has
    // been read on the separator's left; pendingSeparatorCol holds the column of
    // a '/' or '`' still awaiting a note on its right (-1 = none pending).
    bool eachOperandPending = false;
    int pendingSeparatorCol = -1;
    bool initializedMeasureLines = false;
    // Strict-mode tracking for directives ({beats}, (bpm), <HS*N>) that directly
    // follow a note with no ',' in between — usually a forgotten beat separator
    // (e.g. "{16} 1,1,1,1,1\n{16},,,"). True once a note character has been read
    // since the last ','; deliberately survives whitespace and line breaks
    // because neither is a beat separator.
    bool noteSinceComma = false;
    int pendingLineEndNoteCol = -1;
    int pendingLineEndNoteEndCol = -1;
    bool lineSawComma = false;

    const auto clearPendingLineEndNote = [&]() {
        pendingLineEndNoteCol = -1;
        pendingLineEndNoteEndCol = -1;
    };

    const auto flushToken = [&](int lineNumber) {
        if (token.isEmpty()) {
            return;
        }
        const int parsedTokenColumn = tokenColumn;
        const int parsedTokenEndCol = tokenColumn + token.size() - 1;
        const int noteCountBefore = state.result.noteMarkers.size();
        parseToken(&state, token, lineNumber, tokenColumn, &currentGroup);
        if (state.result.noteMarkers.size() > noteCountBefore) {
            pendingLineEndNoteCol = parsedTokenColumn;
            pendingLineEndNoteEndCol = parsedTokenEndCol;
        }
        token.clear();
    };
    const auto warnDirectiveAfterNote = [&](int lineNumber, const QString& line, int openIndex, int closeIndex) {
        if (!strictMode || !noteSinceComma) {
            return;
        }
        const int warningCol = pendingLineEndNoteCol >= 0 ? pendingLineEndNoteCol : openIndex + 1;
        const int warningEndCol = pendingLineEndNoteEndCol >= 0 ? pendingLineEndNoteEndCol : closeIndex + 1;
        appendTokenWarning(
            &state,
            lineNumber,
            warningCol,
            ValidationMessage::kMissingCommaBeforeDirectivePrefix()
                + line.mid(openIndex, closeIndex - openIndex + 1),
            warningEndCol
        );
        // One warning per forgotten ',' — a directive run like "1{16}(120)"
        // is a single missing separator, not two.
        noteSinceComma = false;
        clearPendingLineEndNote();
    };
    const auto advanceMeasureLinesTo = [&](double targetSecond) {
        const double measureDuration = measureDurationSeconds(
            state.bpm,
            state.meterNumerator,
            state.meterDenominator);
        while (state.currentMeasureStartSecond + measureDuration <= targetSecond + kTimelineEpsilon) {
            state.currentMeasureStartSecond += measureDuration;
            appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
        }
    };

    const QStringList lines = text.split('\n');
    const auto nextSignificantLineDirectiveText = [&](int nextLineIndex) {
        for (int i = nextLineIndex; i < lines.size(); ++i) {
            QString nextLine = lines.at(i);
            if (nextLine.endsWith('\r')) {
                nextLine.chop(1);
            }
            const QString trimmed = nextLine.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("||"))) {
                continue;
            }
            if (trimmed.startsWith(QChar('('))) {
                const int close = trimmed.indexOf(QChar(')'), 1);
                return close >= 0 ? trimmed.left(close + 1) : QString();
            }
            if (trimmed.startsWith(QChar('{'))) {
                const int close = trimmed.indexOf(QChar('}'), 1);
                return close >= 0 ? trimmed.left(close + 1) : QString();
            }
            if (trimmed.startsWith(QStringLiteral("<HS*"))) {
                const int close = trimmed.indexOf(QChar('>'), 4);
                return close >= 0 ? trimmed.left(close + 1) : QString();
            }
            return QString();
        }
        return QString();
    };
    const auto warnLineEndNoteMissingComma = [&](int lineNumber, int nextLineIndex) {
        if (!strictMode
            || !lineSawComma
            || pendingLineEndNoteCol < 0) {
            return;
        }
        const QString nextDirective = nextSignificantLineDirectiveText(nextLineIndex);
        const QString message = nextDirective.isEmpty()
            ? ValidationMessage::kLineEndNoteMissingComma()
            : ValidationMessage::kMissingCommaBeforeDirectivePrefix() + nextDirective;
        appendTokenWarning(
            &state,
            lineNumber,
            pendingLineEndNoteCol,
            message,
            pendingLineEndNoteEndCol
        );
        noteSinceComma = false;
        clearPendingLineEndNote();
    };
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QString line = lines.at(lineIndex);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const int lineNumber = lineIndex + 1;
        // Each-groups never span a line break, so a separator's operands are
        // scoped to the current line.
        eachOperandPending = false;
        pendingSeparatorCol = -1;
        clearPendingLineEndNote();
        lineSawComma = false;
        if (isTerminalMarkerText(line)) {
            continue;
        }
        if (!initializedMeasureLines) {
            appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
            initializedMeasureLines = true;
        }
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);

            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken(lineNumber);
                int numerator = 0;
                int denominator = 0;
                if (miacode::simai::parseInlineTimeSignatureComment(
                        line,
                        i,
                        &numerator,
                        &denominator,
                        nullptr)) {
                    state.meterNumerator = numerator;
                    state.meterDenominator = denominator;
                    state.currentMeasureStartSecond = state.second;
                    appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
                }
                break;
            }

            if (ch.isSpace()) {
                flushToken(lineNumber);
                continue;
            }

            if (ch == QChar('(')) {
                flushToken(lineNumber);
                int close = line.indexOf(')', i + 1);
                if (close < 0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedBpmBlock());
                    break;
                }
                warnDirectiveAfterNote(lineNumber, line, i, close);
                bool bpmOk = false;
                const double bpm = line.mid(i + 1, close - i - 1).trimmed().toDouble(&bpmOk);
                if (!bpmOk || bpm <= 0.0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidBpmValue());
                } else {
                    // Any (bpm) directive restarts the measure phase, even when the
                    // value is unchanged (变BPM 一律重启小节相位). Kept in lockstep
                    // with TimelineQuickModel + ChartNormalization.
                    state.currentMeasureStartSecond = state.second;
                    appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
                    state.bpm = bpm;
                }
                i = close;
                continue;
            }

            if (ch == QChar('{')) {
                flushToken(lineNumber);
                int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedBeatBlock());
                    break;
                }
                warnDirectiveAfterNote(lineNumber, line, i, close);
                bool beatsOk = false;
                const int beats = line.mid(i + 1, close - i - 1).trimmed().toInt(&beatsOk);
                if (!beatsOk || beats <= 0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidBeatValue());
                } else if (beats > 384) {
                    state.beats = beats;
                    if (strictMode) {
                        int warningEndCol = close + 1;
                        while (warningEndCol < line.size() && line.at(warningEndCol) == QChar(',')) {
                            ++warningEndCol;
                        }
                        appendTokenWarning(
                            &state,
                            lineNumber,
                            i + 1,
                            ValidationMessage::formatBeatValueClamped(beats),
                            warningEndCol
                        );
                    }
                } else if (strictMode && (384 % beats) != 0) {
                    state.beats = beats;
                    appendTokenWarning(
                        &state,
                        lineNumber,
                        i + 1,
                        ValidationMessage::formatStrictBeatValue(beats)
                    );
                } else {
                    state.beats = beats;
                }
                i = close;
                continue;
            }

                // <HS*N> hi-speed multiplier directive. Mutates state.hs;
                // the next appendNote() call will stamp the new value onto
                // the emitted marker. Q1: the old bracket-less `HS*N>` form
                // is no longer recognized — charts using it will fall
                // through to the generic invalid-token path.
                if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                    flushToken(lineNumber);
                    const int close = line.indexOf('>', i + 4);
                    if (close < 0) {
                        appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedHsBracket());
                        break;
                    }
                    warnDirectiveAfterNote(lineNumber, line, i, close);
                    bool hsOk = false;
                    const double hs = line.mid(i + 4, close - i - 4).trimmed().toDouble(&hsOk);
                    // Q7: zero is always invalid. Negative is invalid UNLESS the
                    // negative-HS compat switch is on (reverse-flow gimmick).
                    const bool hsValueValid = hsOk
                        && hs != 0.0
                        && (g_allowNegativeHs || hs > 0.0);
                    if (!hsValueValid) {
                        appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidHsValue());
                    } else {
                        state.hs = hs;
                    }
                    i = close;
                    continue;
                }

            if (ch == QChar('/')) {
                if (strictMode && i + 1 < line.size() && line.at(i + 1) == QChar('/')) {
                    flushToken(lineNumber);
                    clearPendingLineEndNote();
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kRepeatedSlashSeparator(), i + 2);
                    eachOperandPending = false;
                    pendingSeparatorCol = -1;
                    ++i;
                    continue;
                }
                if (strictMode && !eachOperandPending) {
                    // e.g. ",/7" or a leading '/': nothing to the left.
                    flushToken(lineNumber);
                    clearPendingLineEndNote();
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kSeparatorMissingOperand());
                    pendingSeparatorCol = -1;
                    continue;
                }
                flushToken(lineNumber);
                clearPendingLineEndNote();
                if (strictMode) {
                    pendingSeparatorCol = i + 1;
                    eachOperandPending = false;
                }
                continue;
            }

            if (ch == QChar('`')) {
                int runEnd = i + 1;
                while (runEnd < line.size() && line.at(runEnd) == QChar('`')) {
                    ++runEnd;
                }
                if (strictMode && !eachOperandPending) {
                    // e.g. ",`7", ",``7", or a leading '`': nothing to the left.
                    flushToken(lineNumber);
                    clearPendingLineEndNote();
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kSeparatorMissingOperand());
                    finalizeEachGroup(&state, currentGroup);
                    currentGroup.clear();
                    pendingSeparatorCol = -1;
                    i = runEnd - 1;
                    continue;
                }
                flushToken(lineNumber);
                clearPendingLineEndNote();
                finalizeEachGroup(&state, currentGroup);
                currentGroup.clear();
                if (strictMode) {
                    pendingSeparatorCol = i + 1;
                    eachOperandPending = false;
                }
                i = runEnd - 1;
                continue;
            }

            if (ch == QChar(',')) {
                if (strictMode && pendingSeparatorCol >= 0) {
                    // e.g. "7/," — the divider has no note on its right.
                    appendTokenError(&state, lineNumber, pendingSeparatorCol, ValidationMessage::kSeparatorMissingOperand());
                }
                eachOperandPending = false;
                pendingSeparatorCol = -1;
                noteSinceComma = false;
                flushToken(lineNumber);
                clearPendingLineEndNote();
                lineSawComma = true;
                finalizeEachGroup(&state, currentGroup);
                currentGroup.clear();
                TimelineBeatMarker marker;
                marker.second = state.second;
                marker.sourceLine = qMax(1, lineNumber);
                marker.sourceCol = qMax(1, i + 1);
                marker.major = false;
                state.result.beatMarkers.append(marker);
                state.result.durationSeconds = qMax(state.result.durationSeconds, state.second);
                state.second += noteStepSeconds(state.bpm, state.beats);
                advanceMeasureLinesTo(state.second);
                continue;
            }

            if (token.isEmpty()
                && (ch == QChar('E') || ch == QChar('e'))
                && lineTailIsTerminalMarker(line, i)) {
                flushToken(lineNumber);
                // Q2 — reset HS at chart-end so any post-terminal logic
                // (or future re-emission) starts from a known baseline.
                state.hs = 1.0;
                break;
            }

            if (token.isEmpty()) {
                tokenColumn = i + 1;
            }
            token.append(ch);
            // A note character supplies the operand a preceding '/' or '`' was
            // waiting for, and becomes the left operand for any following one.
            eachOperandPending = true;
            pendingSeparatorCol = -1;
            noteSinceComma = true;
        }

        if (strictMode && pendingSeparatorCol >= 0) {
            // A '/' or '`' at end of line with no following note.
            appendTokenError(&state, lineNumber, pendingSeparatorCol, ValidationMessage::kSeparatorMissingOperand());
        }
        pendingSeparatorCol = -1;
        eachOperandPending = false;

        flushToken(lineNumber);
        warnLineEndNoteMissingComma(lineNumber, lineIndex + 1);
        finalizeEachGroup(&state, currentGroup);
        currentGroup.clear();
    }

    if (strictMode) {
        runStrictFormatChecks(&state, lines);
    }

    std::sort(
        state.result.noteMarkers.begin(),
        state.result.noteMarkers.end(),
        [](const TimelineNoteMarker& a, const TimelineNoteMarker& b) {
            if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
                return a.second < b.second;
            }
            if (a.lane != b.lane) {
                return a.lane < b.lane;
            }
            return a.sourceLine < b.sourceLine;
        }
    );

    QHash<int, QHash<qint64, QVector<int>>> slideTraceGroups;
    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        const TimelineNoteMarker& marker = state.result.noteMarkers.at(i);
        if ((marker.type != "slide" && marker.type != "wifi") || marker.slideTraceSecond < 0.0) {
            continue;
        }
        const qint64 key = qRound64(marker.slideTraceSecond * 1000000.0);
        slideTraceGroups[marker.eachGroupId][key].append(i);
    }
    for (auto groupIt = slideTraceGroups.cbegin(); groupIt != slideTraceGroups.cend(); ++groupIt) {
        const auto& traceGroups = groupIt.value();
        for (auto traceIt = traceGroups.cbegin(); traceIt != traceGroups.cend(); ++traceIt) {
            const QVector<int>& group = traceIt.value();
            if (group.size() < 2) {
                continue;
            }
            for (int index : group) {
                if (index >= 0 && index < state.result.noteMarkers.size()) {
                    state.result.noteMarkers[index].slideEach = true;
                }
            }
        }
    }

    QHash<int, QVector<TimedMarkerRef>> traceByLane;
    QHash<int, QVector<TimedMarkerRef>> endByLane;
    QHash<QString, TouchWindowBuckets> touchWindowsByPad;
    QHash<int, QVector<TimedMarkerRef>> tapsByLane;
    QHash<int, QVector<TimedMarkerRef>> holdTailsByLane;
    QHash<QString, QVector<TimedMarkerRef>> touchesByPad;
    QHash<int, QVector<TimedMarkerRef>> traceQueriesByLane;
    QHash<int, QVector<TimedMarkerRef>> endQueriesByLane;

    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        const TimelineNoteMarker& marker = state.result.noteMarkers.at(i);
        if (marker.type == "tap") {
            tapsByLane[marker.lane].append(TimedMarkerRef{i, marker.second});
            continue;
        }
        if (marker.type == "hold" && marker.endSecond >= 0.0) {
            holdTailsByLane[marker.lane].append(TimedMarkerRef{i, marker.endSecond});
            continue;
        }
        if (marker.type == "touch") {
            if (!marker.touchPad.isEmpty()) {
                touchesByPad[marker.touchPad.toUpper()].append(TimedMarkerRef{i, marker.second});
            }
            continue;
        }
        if (!markerIsSlideLike(marker)) {
            continue;
        }

        if (marker.slideTraceSecond >= 0.0) {
            traceByLane[marker.lane].append(TimedMarkerRef{i, marker.slideTraceSecond});
            traceQueriesByLane[marker.lane].append(TimedMarkerRef{i, marker.slideTraceSecond});
            if (marker.hasHeadStar) {
                tapsByLane[marker.lane].append(TimedMarkerRef{i, marker.second});
            }

            const QString headPad = slideHeadPad(marker.lane);
            if (!headPad.isEmpty()) {
                TouchWindowBuckets& buckets = touchWindowsByPad[headPad.toUpper()];
                if (marker.type == "slide") {
                    buckets.slideHead.append(TimedMarkerRef{i, marker.slideTraceSecond});
                } else if (marker.type == "wifi") {
                    buckets.wifiHead.append(TimedMarkerRef{i, marker.slideTraceSecond});
                }
            }
        }

        if (marker.endSecond >= marker.slideTraceSecond) {
            endByLane[marker.endLane].append(TimedMarkerRef{i, marker.endSecond});
            endQueriesByLane[marker.endLane].append(TimedMarkerRef{i, marker.endSecond});
        }

        if (marker.type == "slide") {
            const int segmentCount = qMin(
                marker.slideSegmentPadEnterTimes.size(),
                qMin(marker.slideSegmentShootSeconds.size(), marker.slideSegmentDurations.size()));
            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const double shootSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
                const double durationSecond = marker.slideSegmentDurations.at(segmentIndex);
                for (const MuriPadTimeEntry& entry : marker.slideSegmentPadEnterTimes.at(segmentIndex)) {
                    if (entry.pad.isEmpty()) {
                        continue;
                    }
                    touchWindowsByPad[entry.pad.toUpper()].padEnter.append(
                        TimedMarkerRef{i, shootSecond + entry.proportion * durationSecond});
                }
            }
        } else if (marker.type == "wifi" && marker.endSecond >= marker.slideTraceSecond) {
            const double durationSecond = marker.endSecond - marker.slideTraceSecond;
            for (const MuriPadTimeEntry& entry : marker.wifiPadEnterTimes) {
                if (entry.pad.isEmpty()) {
                    continue;
                }
                touchWindowsByPad[entry.pad.toUpper()].padEnter.append(
                    TimedMarkerRef{i, marker.slideTraceSecond + entry.proportion * durationSecond});
            }
        }
    }

    for (auto it = traceByLane.begin(); it != traceByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = endByLane.begin(); it != endByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = tapsByLane.begin(); it != tapsByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = holdTailsByLane.begin(); it != holdTailsByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = touchesByPad.begin(); it != touchesByPad.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = traceQueriesByLane.begin(); it != traceQueriesByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = endQueriesByLane.begin(); it != endQueriesByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = touchWindowsByPad.begin(); it != touchWindowsByPad.end(); ++it) {
        sortTimedMarkerRefs(&it.value().slideHead);
        sortTimedMarkerRefs(&it.value().wifiHead);
        sortTimedMarkerRefs(&it.value().padEnter);
    }

    for (auto it = tapsByLane.cbegin(); it != tapsByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].slideHead = true;
            }
        );
    }

    for (auto it = holdTailsByLane.cbegin(); it != holdTailsByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].tailOnSlideHead = true;
            }
        );
    }

    for (auto it = touchesByPad.cbegin(); it != touchesByPad.cend(); ++it) {
        const auto bucketIt = touchWindowsByPad.constFind(it.key());
        if (bucketIt == touchWindowsByPad.constEnd()) {
            continue;
        }
        const TouchWindowBuckets& buckets = bucketIt.value();
        const QVector<TimedMarkerRef>& queries = it.value();
        markOpenIntervalMatches(
            buckets.slideHead,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
        markOpenIntervalMatches(
            buckets.wifiHead,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTouchOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
        markOpenIntervalMatches(
            buckets.padEnter,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTouchOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
    }

    for (auto it = traceQueriesByLane.cbegin(); it != traceQueriesByLane.cend(); ++it) {
        const auto endIt = endByLane.constFind(it.key());
        if (endIt == endByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            endIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].afterSlide = true;
            },
            true
        );
    }

    for (auto it = endQueriesByLane.cbegin(); it != endQueriesByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].beforeSlide = true;
            },
            true
        );
    }

    state.result.durationSeconds = qMax(
        state.result.durationSeconds,
        state.result.noteMarkers.isEmpty() ? 0.0 : state.result.noteMarkers.constLast().second
    );
    return state.result;
}

QString localizeValidationDetail(QString detail, SimaiNativeValidationLocale locale)
{
    if (locale == SimaiNativeValidationLocale::English) {
        return detail;
    }
    const bool japanese = locale == SimaiNativeValidationLocale::Japanese;

    if (detail == ValidationMessage::kLineEndNoteMissingComma()) {
        return japanese
            ? QStringLiteral("行末のノーツに終わりの ',' がありません")
            : QStringLiteral("行尾音符缺少结尾 ','");
    }

    const QHash<QString, QString>& exactMap =
        japanese ? ValidationMessage::jaExactMap() : ValidationMessage::zhExactMap();
    const auto exactIt = exactMap.constFind(detail);
    if (exactIt != exactMap.constEnd()) {
        return exactIt.value();
    }

    const QHash<QString, QString>& prefixMap =
        japanese ? ValidationMessage::jaPrefixMap() : ValidationMessage::zhPrefixMap();
    for (const QString& prefix : ValidationMessage::zhPrefixOrder()) {
        if (!detail.startsWith(prefix)) {
            continue;
        }
        QString localized = prefixMap.value(prefix) + detail.mid(prefix.size());
        if (prefix == ValidationMessage::kInvalidBeatValueStrictPrefix()) {
            localized.replace(
                ValidationMessage::kStrictDivisorSuffix(),
                japanese
                    ? QStringLiteral("（384 の正の約数である必要があります）")
                    : QStringLiteral("（必须是 384 的正整数约数）")
            );
        }
        return localized;
    }

    return detail;
}

QString validationSeverityPrefix(SimaiNativeValidationSeverity severity, SimaiNativeValidationLocale locale)
{
    if (locale == SimaiNativeValidationLocale::Chinese) {
        return severity == SimaiNativeValidationSeverity::Error
            ? QStringLiteral("[错误]")
            : QStringLiteral("[警告]");
    }
    if (locale == SimaiNativeValidationLocale::Japanese) {
        return severity == SimaiNativeValidationSeverity::Error
            ? QStringLiteral("[エラー]")
            : QStringLiteral("[警告]");
    }
    return severity == SimaiNativeValidationSeverity::Error
        ? QStringLiteral("[ERROR]")
        : QStringLiteral("[WARNING]");
}

}  // namespace

SimaiNativeParseResult SimaiNativeParser::parseForTimeline(
    const QString& text,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    MC_OP("SimaiNativeParser::parseForTimeline");
    _mc_op_.note(QStringLiteral("text_len=%1").arg(text.size()));
    return parseInternal(text, false, g_invalidStarPreviewEnabled, timingMetadata);
}

SimaiNativeParseResult SimaiNativeParser::validateSyntax(
    const QString& text,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    MC_OP("SimaiNativeParser::validateSyntax");
    _mc_op_.note(QStringLiteral("text_len=%1").arg(text.size()));
    SimaiNativeParseResult result = parseInternal(text, true, false, timingMetadata);
    return result;
}

void SimaiNativeParser::setInvalidStarPreviewEnabled(bool enabled)
{
    g_invalidStarPreviewEnabled = enabled;
}

bool SimaiNativeParser::invalidStarPreviewEnabled()
{
    return g_invalidStarPreviewEnabled;
}

void SimaiNativeParser::setAllowNegativeHsEnabled(bool enabled)
{
    g_allowNegativeHs = enabled;
}

bool SimaiNativeParser::allowNegativeHsEnabled()
{
    return g_allowNegativeHs;
}

SimaiNativeValidationReport SimaiNativeParser::buildValidationReport(
    const QString& text,
    SimaiNativeValidationLocale locale,
    const SimaiNativeParseResult* lenientResult,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    MC_OP("SimaiNativeParser::buildValidationReport");
    _mc_op_.note(QStringLiteral("text_len=%1 locale=%2")
                     .arg(text.size())
                     .arg(static_cast<int>(locale)));
    // The lenient pass is now exclusively a chart-preview vehicle (see
    // parseForTimeline) and must not influence diagnostics. Validation severity
    // is whatever the strict parser emits — full stop. The legacy parameter is
    // retained for ABI/source compatibility with cached-result call sites; it
    // is intentionally unused here.
    Q_UNUSED(lenientResult);

    SimaiNativeValidationReport report;

    if (text.trimmed().isEmpty()) {
        SimaiNativeValidationIssue issue;
        issue.line = 1;
        issue.col = 1;
        issue.endCol = 1;
        issue.severity = SimaiNativeValidationSeverity::Error;
        issue.rawMessage = ValidationMessage::kChartEmpty();
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(issue.severity, locale), localizeValidationDetail(issue.rawMessage, locale));
        report.issues.append(issue);
        report.errorCount = 1;
        report.strictErrorCount = 1;
        report.ok = false;
        return report;
    }

    const SimaiNativeParseResult strictResult = validateSyntax(text, timingMetadata);

    // Lenient-side counters are kept in the report struct for ABI compatibility
    // with downstream consumers (MainWindow validation entries, etc.) but are
    // no longer populated — preview parsing is an entirely separate flow.
    report.lenientNoteCount = 0;
    report.lenientErrorCount = 0;
    report.strictNoteCount = strictResult.noteMarkers.size();
    report.strictErrorCount = strictResult.errors.size();

    report.issues.reserve(strictResult.errors.size() + strictResult.warnings.size());
    for (const SimaiNativeMessage& error : strictResult.errors) {
        ++report.errorCount;

        SimaiNativeValidationIssue issue;
        issue.line = error.line;
        issue.col = error.col;
        issue.endCol = error.endCol;
        issue.severity = SimaiNativeValidationSeverity::Error;
        issue.rawMessage = error.message;
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(issue.severity, locale), localizeValidationDetail(error.message, locale));
        report.issues.append(issue);
    }

    for (const SimaiNativeMessage& warning : strictResult.warnings) {
        ++report.warningCount;

        SimaiNativeValidationIssue issue;
        issue.line = warning.line;
        issue.col = warning.col;
        issue.endCol = warning.endCol;
        issue.severity = SimaiNativeValidationSeverity::Warning;
        issue.rawMessage = warning.message;
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(issue.severity, locale), localizeValidationDetail(warning.message, locale));
        report.issues.append(issue);
    }

    report.ok = (report.errorCount == 0);
    return report;
}
