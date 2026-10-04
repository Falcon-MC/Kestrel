#include "ui/JsonUiInternal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <unordered_map>

namespace kestrel::ui::jsonui {

std::string_view controlName(std::string_view key)
{
    return key.substr(0, key.find('@'));
}

std::string_view controlBase(std::string_view key)
{
    size_t at = key.find('@');
    return at == std::string_view::npos ? std::string_view() : key.substr(at + 1);
}

UiValue toValue(const json::Value* value)
{
    if (!value) {
        return {};
    }
    switch (value->mType) {
    case json::Value::Type::Boolean:
        return UiValue::of(value->mBoolean);
    case json::Value::Type::Number:
        return UiValue::of(value->mNumber);
    case json::Value::Type::String:
        return UiValue::of(value->mString);
    case json::Value::Type::Array: {
        std::string result;
        for (const auto& entry : value->mArray) {
            if (!result.empty()) {
                result += ',';
            }
            result += toValue(entry.get()).toText();
        }
        return UiValue::of(std::move(result));
    }
    default:
        return {};
    }
}

namespace {

constexpr size_t MaxCachedExpressions = 8192;
constexpr size_t MaxExpressionBytes = 64 * 1024;
constexpr size_t MaxExpressionTokens = 16 * 1024;
constexpr size_t MaxExpressionNesting = 512;
constexpr int MaxVariableDepth = 16;

/**
 * Operator codes, numbered as the game's OperatorType numbers them.
 */
enum : uint8_t {
    OpNone = 0,
    OpAnd = 1,
    OpOr = 2,
    OpGreater = 3,
    OpLess = 4,
    OpEqual = 5,
    OpPlus = 6,
    OpMinus = 7,
    OpTimes = 8,
    OpDivide = 9,
    OpNot = 10,
};

/**
 * A typed value the way the game's expression tokens carry one. Text starting
 * with '#' names a property, read only when it is the final result; an
 * operator spelled out as a text result is an untyped null.
 */
struct Operand {
    enum class Kind {
        Null,
        Bool,
        Int,
        Float,
        Str,
        PropertyText,
        OperatorText,
    };

    Kind kind = Kind::Null;
    bool flag = false;
    int32_t whole = 0;
    float real = 0.0f;
    std::string text;
    uint8_t op = OpNone;

    static Operand ofBool(bool value)
    {
        Operand result;
        result.kind = Kind::Bool;
        result.flag = value;
        return result;
    }

    static Operand ofInt(int32_t value)
    {
        Operand result;
        result.kind = Kind::Int;
        result.whole = value;
        return result;
    }

    static Operand ofFloat(float value)
    {
        Operand result;
        result.kind = Kind::Float;
        result.real = value;
        return result;
    }

    static Operand ofStr(std::string value)
    {
        Operand result;
        result.kind = Kind::Str;
        result.text = std::move(value);
        return result;
    }

    static Operand literalText(std::string value)
    {
        Operand result = ofStr(std::move(value));
        if (!result.text.empty() && result.text.front() == '#') {
            result.kind = Kind::PropertyText;
        }
        return result;
    }

    static Operand ofOperator(uint8_t code)
    {
        Operand result;
        result.kind = Kind::OperatorText;
        result.op = code;
        return result;
    }

    bool truthy() const
    {
        switch (kind) {
        case Kind::Bool:
            return flag;
        case Kind::Int:
            return whole != 0;
        case Kind::Float:
            return real != 0.0f;
        case Kind::Str:
        case Kind::PropertyText:
            return !text.empty();
        default:
            return false;
        }
    }

    int32_t asInt() const
    {
        switch (kind) {
        case Kind::Bool:
            return flag ? 1 : 0;
        case Kind::Int:
            return whole;
        case Kind::Float:
            return static_cast<int32_t>(real);
        default:
            return 0;
        }
    }

    float asFloat() const
    {
        switch (kind) {
        case Kind::Bool:
            return flag ? 1.0f : 0.0f;
        case Kind::Int:
            return static_cast<float>(whole);
        case Kind::Float:
            return real;
        default:
            return 0.0f;
        }
    }

    std::string_view asText() const
    {
        if (kind == Kind::Bool) {
            return flag ? "true" : "false";
        }
        if (kind == Kind::Str || kind == Kind::PropertyText) {
            return text;
        }
        return {};
    }

    bool isString() const
    {
        return kind == Kind::Str;
    }
};

/**
 * One token of an expression: a value, a #property or $variable read when
 * evaluated, an operator, or a parenthesised group of tokens.
 */
struct Token {
    enum class Kind {
        Value,
        Property,
        Variable,
        Operator,
        Group,
    };

    Kind kind = Kind::Value;
    Operand value;
    std::string name;
    uint8_t op = OpNone;
    std::vector<Token> group;
};

bool delimiter(char c)
{
    return c == ' ' || c == '$' || c == '(' || c == ')' || c == '*' || c == '+' || c == '-' || c == '/' || c == '<' || c == '=' || c == '>';
}

bool operatorChar(char c)
{
    return c == '*' || c == '+' || c == '-' || c == '/' || c == '<' || c == '=' || c == '>';
}

/**
 * strtol over the longest leading integer, failing on overflow of 32 bits.
 */
std::optional<int32_t> leadingInt(std::string_view word)
{
    while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) {
        word.remove_prefix(1);
    }
    size_t end = !word.empty() && (word.front() == '+' || word.front() == '-') ? 1 : 0;
    size_t digits = end;
    while (end < word.size() && std::isdigit(static_cast<unsigned char>(word[end]))) {
        ++end;
    }
    if (end == digits) {
        return std::nullopt;
    }
    long long value = std::strtoll(std::string(word.substr(0, end)).c_str(), nullptr, 10);
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<int32_t>(value);
}

/**
 * strtof over the longest leading float, inf and nan included, failing on
 * overflow.
 */
std::optional<float> leadingFloat(std::string_view word)
{
    while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) {
        word.remove_prefix(1);
    }
    size_t end = !word.empty() && (word.front() == '+' || word.front() == '-') ? 1 : 0;
    bool mantissa = false;
    while (end < word.size() && std::isdigit(static_cast<unsigned char>(word[end]))) {
        ++end;
        mantissa = true;
    }
    if (end < word.size() && word[end] == '.') {
        ++end;
        while (end < word.size() && std::isdigit(static_cast<unsigned char>(word[end]))) {
            ++end;
            mantissa = true;
        }
    }
    if (!mantissa) {
        std::string lower;
        for (char c : word) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        bool negative = !lower.empty() && lower.front() == '-';
        std::string_view unsigned_ = lower;
        while (!unsigned_.empty() && (unsigned_.front() == '+' || unsigned_.front() == '-')) {
            unsigned_.remove_prefix(1);
        }
        float special;
        if (unsigned_.substr(0, 3) == "inf") {
            special = std::numeric_limits<float>::infinity();
        } else if (unsigned_.substr(0, 3) == "nan") {
            special = std::numeric_limits<float>::quiet_NaN();
        } else {
            return std::nullopt;
        }
        return negative ? -special : special;
    }
    if (end < word.size() && (word[end] == 'e' || word[end] == 'E')) {
        size_t exponent = end + 1;
        if (exponent < word.size() && (word[exponent] == '+' || word[exponent] == '-')) {
            ++exponent;
        }
        size_t digits = exponent;
        while (exponent < word.size() && std::isdigit(static_cast<unsigned char>(word[exponent]))) {
            ++exponent;
        }
        if (exponent > digits) {
            end = exponent;
        }
    }
    float value = std::strtof(std::string(word.substr(0, end)).c_str(), nullptr);
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

/**
 * The game's toBool, ignoring case: true or false, yes or no, 1 or 0.
 */
std::optional<bool> wordBool(std::string_view word)
{
    std::string lower;
    for (char c : word) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "true" || lower == "yes" || lower == "1") {
        return true;
    }
    if (lower == "false" || lower == "no" || lower == "0") {
        return false;
    }
    return std::nullopt;
}

/**
 * Types one piece of an expression the way the game's token parser does:
 * keyword, quoted string, property, integer, float, flag word, operator, and
 * anything else a bare string.
 */
Token parseWord(std::string_view word)
{
    Token token;
    if (word == "or" || word == "and" || word == "not") {
        token.kind = Token::Kind::Operator;
        token.op = word == "or" ? OpOr : word == "and" ? OpAnd : OpNot;
        return token;
    }
    if (!word.empty()) {
        char first = word.front();
        if ((first == '\'' || first == '"') && word.size() > 1 && word.back() == first) {
            token.value = Operand::ofStr(std::string(word.substr(1, word.size() - 2)));
            return token;
        }
        if (first == '#') {
            token.kind = Token::Kind::Property;
            token.name = std::string(word);
            return token;
        }
    }
    if (std::optional<int32_t> whole = leadingInt(word)) {
        token.value = Operand::ofInt(*whole);
        return token;
    }
    if (std::optional<float> real = leadingFloat(word)) {
        token.value = Operand::ofFloat(*real);
        return token;
    }
    if (std::optional<bool> flag = wordBool(word)) {
        token.value = Operand::ofBool(*flag);
        return token;
    }
    if (!word.empty()) {
        uint8_t code = OpNone;
        switch (word.front()) {
        case '*':
            code = OpTimes;
            break;
        case '+':
            code = OpPlus;
            break;
        case '-':
            code = OpMinus;
            break;
        case '/':
            code = OpDivide;
            break;
        case '<':
            code = OpLess;
            break;
        case '=':
            code = OpEqual;
            break;
        case '>':
            code = OpGreater;
            break;
        default:
            break;
        }
        if (code != OpNone) {
            token.kind = Token::Kind::Operator;
            token.op = code;
            return token;
        }
    }
    token.value = Operand::ofStr(std::string(word));
    return token;
}

/**
 * An operator's text result read again as a whole token, empty text staying
 * an empty string.
 */
Operand reparse(std::string text)
{
    if (text.empty()) {
        return Operand::ofStr(std::move(text));
    }
    Token token = parseWord(text);
    switch (token.kind) {
    case Token::Kind::Value:
        return token.value;
    case Token::Kind::Property:
        return Operand::literalText(token.name);
    case Token::Kind::Operator:
        return Operand::ofOperator(token.op);
    default:
        return Operand::ofStr(std::move(text));
    }
}

/**
 * Splits an expression into tokens the way the game's evaluator does; empty
 * past its size, token or nesting bounds or on an unbalanced ')'. Groups left
 * open close at the end.
 */
std::optional<std::vector<Token>> tokenize(std::string_view source)
{
    if (source.size() > MaxExpressionBytes) {
        return std::nullopt;
    }
    std::vector<std::vector<Token>> groups(1);
    size_t count = 0;
    size_t i = 0;
    while (i < source.size()) {
        char c = source[i];
        if (c != ' ' && ++count > MaxExpressionTokens) {
            return std::nullopt;
        }
        if (c == ' ') {
            ++i;
            continue;
        }
        if (c == '(') {
            if (groups.size() > MaxExpressionNesting) {
                return std::nullopt;
            }
            groups.emplace_back();
            ++i;
            continue;
        }
        if (c == ')') {
            if (groups.size() < 2) {
                return std::nullopt;
            }
            Token group;
            group.kind = Token::Kind::Group;
            group.group = std::move(groups.back());
            groups.pop_back();
            groups.back().push_back(std::move(group));
            ++i;
            continue;
        }
        size_t start = i;
        if (operatorChar(c)) {
            ++i;
        } else {
            if (c == '\'' || c == '"') {
                ++i;
                while (i < source.size() && source[i] != c) {
                    ++i;
                }
                i = std::min(i + 1, source.size());
            } else {
                ++i;
            }
            while (i < source.size() && !delimiter(source[i])) {
                ++i;
            }
        }
        std::string_view word = source.substr(start, i - start);
        if (word.front() == '$') {
            Token variable;
            variable.kind = Token::Kind::Variable;
            variable.name = std::string(word);
            groups.back().push_back(std::move(variable));
        } else {
            groups.back().push_back(parseWord(word));
        }
    }
    while (groups.size() > 1) {
        Token group;
        group.kind = Token::Kind::Group;
        group.group = std::move(groups.back());
        groups.pop_back();
        groups.back().push_back(std::move(group));
    }
    return std::move(groups.front());
}

int precedence(uint8_t op)
{
    switch (op) {
    case OpAnd:
    case OpOr:
        return 1;
    case OpGreater:
    case OpLess:
    case OpEqual:
        return 2;
    case OpPlus:
    case OpMinus:
        return 3;
    case OpTimes:
    case OpDivide:
        return 4;
    default:
        return 0;
    }
}

bool unaryOp(uint8_t op)
{
    return op == OpPlus || op == OpMinus || op == OpNot;
}

/**
 * A stack entry: a value, or an operator waiting for its right side.
 */
struct Item {
    bool isOperatorToken = false;
    uint8_t code = OpNone;
    Operand value;

    uint8_t op() const
    {
        if (isOperatorToken) {
            return code;
        }
        return value.kind == Operand::Kind::OperatorText ? value.op : OpNone;
    }

    bool isOperator() const
    {
        return isOperatorToken || value.kind == Operand::Kind::OperatorText;
    }

    Operand operand() const
    {
        return isOperatorToken ? Operand::ofOperator(code) : value;
    }
};

Operand numeric(const Operand& lhs, const Operand& rhs, uint8_t op)
{
    if (lhs.kind == Operand::Kind::Float || rhs.kind == Operand::Kind::Float) {
        float a = lhs.asFloat();
        float b = rhs.asFloat();
        switch (op) {
        case OpPlus:
            return Operand::ofFloat(a + b);
        case OpMinus:
            return Operand::ofFloat(a - b);
        case OpTimes:
            return Operand::ofFloat(a * b);
        default:
            return Operand::ofFloat(a / b);
        }
    }
    uint32_t a = static_cast<uint32_t>(lhs.asInt());
    uint32_t b = static_cast<uint32_t>(rhs.asInt());
    switch (op) {
    case OpPlus:
        return Operand::ofInt(static_cast<int32_t>(a + b));
    case OpMinus:
        return Operand::ofInt(static_cast<int32_t>(a - b));
    case OpTimes:
        return Operand::ofInt(static_cast<int32_t>(a * b));
    default: {
        int32_t x = lhs.asInt();
        int32_t y = rhs.asInt();
        if (x == std::numeric_limits<int32_t>::min() && y == -1) {
            return Operand::ofInt(x);
        }
        return Operand::ofInt(x / y);
    }
    }
}

bool order(uint8_t op, const Operand& lhs, const Operand& rhs)
{
    bool greater = op == OpGreater;
    if (lhs.isString() && rhs.isString()) {
        int compared = lhs.asText().compare(rhs.asText());
        return greater ? compared > 0 : compared < 0;
    }
    if (lhs.isString() || rhs.isString()) {
        return greater ? lhs.truthy() && !rhs.truthy() : !lhs.truthy() && rhs.truthy();
    }
    return greater ? lhs.asFloat() > rhs.asFloat() : lhs.asFloat() < rhs.asFloat();
}

bool equal(const Operand& lhs, const Operand& rhs)
{
    if (lhs.isString() && rhs.isString()) {
        return lhs.asText() == rhs.asText();
    }
    if (lhs.isString() || rhs.isString()) {
        return !lhs.truthy() && !rhs.truthy();
    }
    if (lhs.kind == Operand::Kind::Bool || rhs.kind == Operand::Kind::Bool) {
        return lhs.truthy() == rhs.truthy();
    }
    return lhs.asFloat() == rhs.asFloat();
}

std::string spelled(const Operand& operand)
{
    if (operand.kind == Operand::Kind::Int) {
        return std::to_string(operand.whole);
    }
    return std::string(operand.asText());
}

/**
 * The N of a "%.Ns" format, read as strtoull reads it; a negative or
 * unreadable count keeps the whole text.
 */
std::optional<size_t> truncation(std::string_view format)
{
    if (format.size() < 3 || format.substr(0, 2) != "%." || format.back() != 's') {
        return std::nullopt;
    }
    std::string_view digits = format.substr(2, format.size() - 3);
    while (!digits.empty() && std::isspace(static_cast<unsigned char>(digits.front()))) {
        digits.remove_prefix(1);
    }
    if (!digits.empty() && digits.front() == '-') {
        return std::nullopt;
    }
    if (!digits.empty() && digits.front() == '+') {
        digits.remove_prefix(1);
    }
    size_t end = 0;
    while (end < digits.size() && std::isdigit(static_cast<unsigned char>(digits[end]))) {
        ++end;
    }
    if (end == 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(std::strtoull(std::string(digits.substr(0, end)).c_str(), nullptr, 10));
}

/**
 * One binary operator with the game's rules for each type: text joins,
 * removes, keeps a prefix of or counts matches in text, and numbers stay
 * integers unless a side is a float.
 */
Operand apply(uint8_t op, const Operand& lhs, const Operand& rhs)
{
    switch (op) {
    case OpAnd:
        return Operand::ofBool(lhs.truthy() && rhs.truthy());
    case OpOr:
        return Operand::ofBool(lhs.truthy() || rhs.truthy());
    case OpGreater:
    case OpLess:
        return Operand::ofBool(order(op, lhs, rhs));
    case OpEqual:
        return Operand::ofBool(equal(lhs, rhs));
    case OpPlus:
        if (lhs.isString()) {
            return reparse(std::string(lhs.asText()) + spelled(rhs));
        }
        if (rhs.isString()) {
            return reparse(spelled(lhs) + std::string(rhs.asText()));
        }
        return numeric(lhs, rhs, op);
    case OpMinus: {
        if (lhs.isString()) {
            std::string text(lhs.asText());
            std::string_view needle = rhs.asText();
            if (text.empty() || needle.empty()) {
                return reparse(std::move(text));
            }
            std::string result;
            size_t from = 0;
            for (size_t found; (found = text.find(needle, from)) != std::string::npos; from = found + needle.size()) {
                result.append(text, from, found - from);
            }
            result.append(text, from, std::string::npos);
            return reparse(std::move(result));
        }
        if (rhs.isString()) {
            return lhs;
        }
        return numeric(lhs, rhs, op);
    }
    case OpTimes: {
        if (lhs.isString()) {
            std::string_view right = rhs.asText();
            std::optional<size_t> keep = truncation(lhs.asText());
            size_t count = keep ? std::min(*keep, right.size()) : right.size();
            return reparse(std::string(right.substr(0, count)));
        }
        if (rhs.isString()) {
            return lhs;
        }
        return numeric(lhs, rhs, op);
    }
    case OpDivide: {
        if (lhs.isString()) {
            std::string_view text = lhs.asText();
            std::string_view needle = rhs.asText();
            if (needle.empty()) {
                return Operand::ofInt(1);
            }
            int32_t matches = 0;
            for (size_t from = 0, found; (found = text.find(needle, from)) != std::string_view::npos; from = found + needle.size()) {
                ++matches;
            }
            return Operand::ofInt(matches);
        }
        if (rhs.isString() || rhs.asFloat() == 0.0f) {
            return lhs;
        }
        return numeric(lhs, rhs, op);
    }
    default:
        return {};
    }
}

void reduceUnary(std::vector<Item>& stack)
{
    while (stack.size() >= 2) {
        size_t size = stack.size();
        const Item& prefix = stack[size - 2];
        uint8_t op = prefix.op();
        bool leads = size == 2 || stack[size - 3].isOperator();
        if (!unaryOp(op) || !prefix.isOperator() || !leads) {
            break;
        }
        Operand value = stack.back().operand();
        stack.pop_back();
        stack.pop_back();
        Item result;
        if (op == OpPlus) {
            result.value = value.kind == Operand::Kind::Float ? Operand::ofFloat(value.asFloat()) : Operand::ofInt(value.asInt());
        } else if (op == OpMinus) {
            result.value = value.kind == Operand::Kind::Float
                ? Operand::ofFloat(-value.asFloat())
                : Operand::ofInt(static_cast<int32_t>(0u - static_cast<uint32_t>(value.asInt())));
        } else {
            result.value = Operand::ofBool(!value.truthy());
        }
        stack.push_back(std::move(result));
    }
}

void reduceBinary(uint8_t next, std::vector<Item>& stack)
{
    int floor = precedence(next);
    while (stack.size() > 2) {
        size_t size = stack.size();
        const Item& top = stack[size - 2];
        if (precedence(top.op()) < floor || !top.isOperator()) {
            break;
        }
        uint8_t op = top.op();
        Operand rhs = stack[size - 1].operand();
        Operand lhs = stack[size - 3].operand();
        stack.resize(size - 3);
        Item result;
        result.value = apply(op, lhs, rhs);
        stack.push_back(std::move(result));
    }
}

/**
 * The value a binding or variable gives an expression token: whole numbers
 * read as integers, text starting with '#' as a property name.
 */
Operand fromUi(const UiValue& value)
{
    switch (value.kind) {
    case UiValue::Kind::Bool:
        return Operand::ofBool(value.flag);
    case UiValue::Kind::Number:
        if (value.number == std::floor(value.number) && value.number >= std::numeric_limits<int32_t>::min() && value.number <= std::numeric_limits<int32_t>::max()) {
            return Operand::ofInt(static_cast<int32_t>(value.number));
        }
        return Operand::ofFloat(static_cast<float>(value.number));
    case UiValue::Kind::String:
        return Operand::literalText(value.text);
    default:
        return {};
    }
}

UiValue toUi(const Operand& operand)
{
    switch (operand.kind) {
    case Operand::Kind::Bool:
        return UiValue::of(operand.flag);
    case Operand::Kind::Int:
        return UiValue::of(static_cast<double>(operand.whole));
    case Operand::Kind::Float:
        return UiValue::of(static_cast<double>(operand.real));
    case Operand::Kind::Str:
    case Operand::Kind::PropertyText:
        return UiValue::of(operand.text);
    case Operand::Kind::OperatorText:
        return UiValue::of(0.0);
    default:
        return {};
    }
}

/**
 * Parsed expressions, each split the first time it is met: the same bindings
 * are evaluated every frame for as long as a screen shows.
 */
const std::optional<std::vector<Token>>& compiled(std::string_view source)
{
    thread_local std::unordered_map<std::string, std::optional<std::vector<Token>>> cache;
    auto found = cache.find(std::string(source));
    if (found != cache.end()) {
        return found->second;
    }
    if (cache.size() >= MaxCachedExpressions) {
        cache.clear();
    }
    return cache.emplace(std::string(source), tokenize(source)).first->second;
}

class Evaluator {
public:
    Evaluator(const UiLookup& lookup, int depth)
        : lookup(lookup)
        , depth(depth)
    {
    }

    /**
     * A final result naming a property reads it; one the data lacks stays
     * the name, as the game reads a property with no bag to look in.
     */
    Operand resolveFinal(Operand operand) const
    {
        if (operand.kind != Operand::Kind::PropertyText) {
            return operand;
        }
        UiValue found = lookup(operand.text);
        if (found.kind == UiValue::Kind::None) {
            return Operand::ofStr(operand.text);
        }
        return fromUi(found);
    }

    Operand run(const std::vector<Token>& tokens) const
    {
        std::vector<Item> stack;
        stack.reserve(tokens.size());
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            Item item;
            switch (token.kind) {
            case Token::Kind::Group: {
                Operand inner = resolveFinal(run(token.group));
                item.value = settle(std::move(inner));
                break;
            }
            case Token::Kind::Property:
                item.value = fromUi(lookup(token.name));
                break;
            case Token::Kind::Variable:
                item.value = variable(token.name);
                break;
            case Token::Kind::Value:
                item.value = token.value;
                break;
            case Token::Kind::Operator:
                item.isOperatorToken = true;
                item.code = token.op;
                break;
            }
            bool pushedOperator = item.isOperator();
            stack.push_back(std::move(item));
            if (pushedOperator) {
                continue;
            }
            reduceUnary(stack);
            uint8_t next = index + 1 < tokens.size() && tokens[index + 1].kind == Token::Kind::Operator ? tokens[index + 1].op : OpNone;
            if (stack.size() > 1) {
                uint8_t top = stack[stack.size() - 2].op();
                if (top != OpNone && precedence(next) <= precedence(top)) {
                    reduceBinary(next, stack);
                }
            }
        }
        if (stack.size() == 1) {
            return stack.front().operand();
        }
        return {};
    }

private:
    /**
     * A group's result as the JSON value the game turns it into before
     * reading it as a token again: untyped results become integers.
     */
    static Operand settle(Operand operand)
    {
        switch (operand.kind) {
        case Operand::Kind::Bool:
        case Operand::Kind::Float:
        case Operand::Kind::Null:
        case Operand::Kind::Int:
            return operand;
        case Operand::Kind::Str:
            return Operand::literalText(std::move(operand.text));
        default:
            return Operand::ofInt(operand.asInt());
        }
    }

    /**
     * A variable token: one holding "#name" reads that binding, one holding a
     * parenthesised expression evaluates it.
     */
    Operand variable(const std::string& token) const
    {
        std::string name = token.substr(0, token.find('|'));
        UiValue value = lookup(name);
        if (value.kind == UiValue::Kind::String && !value.text.empty()) {
            if (value.text.front() == '#') {
                return fromUi(lookup(value.text));
            }
            if (value.text.front() == '(' && depth < MaxVariableDepth) {
                const std::optional<std::vector<Token>>& tokens = compiled(value.text);
                if (!tokens) {
                    return {};
                }
                return Evaluator(lookup, depth + 1).resolveFinal(Evaluator(lookup, depth + 1).run(*tokens));
            }
        }
        return fromUi(value);
    }

    const UiLookup& lookup;
    int depth = 0;
};

}

UiValue evaluate(std::string_view source, const UiLookup& lookup)
{
    const std::optional<std::vector<Token>>& tokens = compiled(source);
    if (!tokens) {
        return {};
    }
    Evaluator evaluator(lookup, 0);
    return toUi(evaluator.resolveFinal(evaluator.run(*tokens)));
}

namespace {

/**
 * The longest leading float of a run of layout characters, else zero.
 */
float layoutNumber(const std::string& text)
{
    for (size_t length = text.size(); length > 0; --length) {
        std::string part = text.substr(0, length);
        char* end = nullptr;
        double value = std::strtod(part.c_str(), &end);
        if (end != part.c_str() && *end == '\0') {
            return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
        }
    }
    return 0.0f;
}

/**
 * Parses a size or offset the way the game's parseLayoutAxis does: exactly
 * "fill" or "default" are keywords, anything else a lower cased stream of
 * numbers, px and % units and signs, where a sign holds until the next one, a
 * number without a unit is dropped, and a %cm or %sm term is the biggest
 * child or sibling itself whatever its number, unless that number is zero.
 */
Extent parseLayoutAxis(std::string_view input)
{
    if (input == "fill") {
        return { { TermKind::Fill, 1.0f } };
    }
    if (input == "default") {
        return { { TermKind::Default, 1.0f } };
    }
    enum class Kind {
        Number,
        Plus,
        Minus,
        Px,
        Percent,
        Modifier,
    };
    struct Piece {
        Kind kind;
        float number = 0.0f;
        char modifier = 0;
    };
    std::vector<Piece> pieces;
    std::string number;
    auto flush = [&]() {
        if (!number.empty()) {
            pieces.push_back({ Kind::Number, layoutNumber(number), 0 });
            number.clear();
        }
    };
    for (size_t i = 0; i < input.size(); ++i) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(input[i])));
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        if (c == '+' || c == '-' || c == '%' || c == 'c' || c == 'm' || c == 's' || c == 'x' || c == 'y') {
            flush();
            Kind kind = c == '+' ? Kind::Plus : c == '-' ? Kind::Minus : c == '%' ? Kind::Percent : Kind::Modifier;
            pieces.push_back({ kind, 0.0f, c });
            continue;
        }
        if (c == 'p' && i + 1 < input.size() && std::tolower(static_cast<unsigned char>(input[i + 1])) == 'x') {
            flush();
            pieces.push_back({ Kind::Px, 0.0f, 0 });
            ++i;
            continue;
        }
        number.push_back(c);
    }
    flush();
    Extent terms;
    float value = 0.0f;
    float sign = 1.0f;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        switch (piece.kind) {
        case Kind::Number:
            value = piece.number;
            break;
        case Kind::Plus:
            sign = 1.0f;
            break;
        case Kind::Minus:
            sign = -1.0f;
            break;
        case Kind::Px:
            terms.push_back({ TermKind::Pixel, sign * value });
            break;
        case Kind::Percent: {
            auto modifier = [&](size_t offset) -> char {
                return i + offset < pieces.size() && pieces[i + offset].kind == Kind::Modifier ? pieces[i + offset].modifier : 0;
            };
            float coefficient = sign * value;
            float gate = coefficient == 0.0f ? 0.0f : 1.0f;
            if (modifier(1) == 'c' && modifier(2) == 'm') {
                terms.push_back({ TermKind::ChildrenMax, gate });
                i += 2;
            } else if (modifier(1) == 'c') {
                terms.push_back({ TermKind::Children, coefficient / 100.0f });
                i += 1;
            } else if (modifier(1) == 's' && modifier(2) == 'm') {
                terms.push_back({ TermKind::SiblingMax, gate });
                i += 2;
            } else if (modifier(1) == 'x') {
                terms.push_back({ TermKind::OwnX, coefficient / 100.0f });
                i += 1;
            } else if (modifier(1) == 'y') {
                terms.push_back({ TermKind::OwnY, coefficient / 100.0f });
                i += 1;
            } else {
                terms.push_back({ TermKind::Parent, coefficient / 100.0f });
            }
            break;
        }
        case Kind::Modifier:
            break;
        }
    }
    if (terms.empty()) {
        terms.push_back({ TermKind::Pixel, 0.0f });
    }
    return terms;
}

}

/**
 * A size or offset term list. Strings are parsed once and kept, since the
 * layout reads every control's size and offset each frame. A missing value
 * takes the fallback, and one that is neither a number nor text is default.
 */
Extent parseExtent(const json::Value* value, TermKind fallback)
{
    if (!value) {
        return { { fallback, 1.0f } };
    }
    if (value->isNumber()) {
        return { { TermKind::Pixel, static_cast<float>(value->number()) } };
    }
    if (!value->isString()) {
        return { { TermKind::Default, 1.0f } };
    }
    thread_local std::unordered_map<std::string, Extent> cache;
    if (auto found = cache.find(value->mString); found != cache.end()) {
        return found->second;
    }
    if (cache.size() >= MaxCachedExpressions) {
        cache.clear();
    }
    Extent terms = parseLayoutAxis(value->mString);
    cache.emplace(value->mString, terms);
    return terms;
}

bool uses(const Extent& extent, TermKind kind)
{
    return std::any_of(extent.begin(), extent.end(), [&](const Term& term) { return term.kind == kind; });
}

}
