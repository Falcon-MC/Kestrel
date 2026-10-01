#include "JsonUiInternal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

enum class Op {
    Constant,
    Lookup,
    Not,
    Negate,
    Or,
    And,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
};

/**
 * One step of a parsed binding expression: a constant, a binding or variable
 * to look up, or an operator over one or two operands.
 */
struct ExprNode {
    Op op = Op::Constant;
    UiValue constant;
    std::string name;
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
};

using NodePtr = std::unique_ptr<ExprNode>;

NodePtr leaf(UiValue value)
{
    auto node = std::make_unique<ExprNode>();
    node->constant = std::move(value);
    return node;
}

NodePtr binary(Op op, NodePtr left, NodePtr right)
{
    auto node = std::make_unique<ExprNode>();
    node->op = op;
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}

/**
 * Parses the game's binding expressions once into a tree: or, and,
 * comparisons, sums, products, not and minus over numbers, quoted strings,
 * true, false, parentheses and #binding or $variable names.
 */
class Parser {
public:
    explicit Parser(std::string_view source)
        : text(source)
    {
    }

    NodePtr parse()
    {
        return orExpression();
    }

private:
    void skip()
    {
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) {
            ++at;
        }
    }

    bool word(std::string_view keyword)
    {
        skip();
        if (text.compare(at, keyword.size(), keyword) != 0) {
            return false;
        }
        size_t end = at + keyword.size();
        if (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
            return false;
        }
        at = end;
        return true;
    }

    bool symbol(std::string_view token)
    {
        skip();
        if (text.compare(at, token.size(), token) != 0) {
            return false;
        }
        at += token.size();
        return true;
    }

    NodePtr orExpression()
    {
        NodePtr left = andExpression();
        while (word("or")) {
            left = binary(Op::Or, std::move(left), andExpression());
        }
        return left;
    }

    NodePtr andExpression()
    {
        NodePtr left = comparison();
        while (word("and")) {
            left = binary(Op::And, std::move(left), comparison());
        }
        return left;
    }

    NodePtr comparison()
    {
        NodePtr left = sum();
        skip();
        static constexpr std::pair<std::string_view, Op> Comparisons[] = {
            { ">=", Op::GreaterEqual }, { "<=", Op::LessEqual }, { "!=", Op::NotEqual },
            { "=", Op::Equal }, { ">", Op::Greater }, { "<", Op::Less },
        };
        for (const auto& [token, op] : Comparisons) {
            if (symbol(token)) {
                return binary(op, std::move(left), sum());
            }
        }
        return left;
    }

    NodePtr sum()
    {
        NodePtr left = product();
        while (true) {
            if (symbol("+")) {
                left = binary(Op::Add, std::move(left), product());
            } else if (symbol("-")) {
                left = binary(Op::Subtract, std::move(left), product());
            } else {
                return left;
            }
        }
    }

    NodePtr product()
    {
        NodePtr left = unary();
        while (true) {
            if (symbol("*")) {
                left = binary(Op::Multiply, std::move(left), unary());
            } else if (symbol("/")) {
                left = binary(Op::Divide, std::move(left), unary());
            } else if (symbol("%")) {
                left = binary(Op::Modulo, std::move(left), unary());
            } else {
                return left;
            }
        }
    }

    NodePtr unary()
    {
        // Packs are untrusted, and deep enough nesting would run the stack out.
        if (++nesting > MaxDepth) {
            at = text.size();
            return leaf({});
        }
        NodePtr value = prefixed();
        --nesting;
        return value;
    }

    NodePtr prefixed()
    {
        if (word("not")) {
            return binary(Op::Not, unary(), nullptr);
        }
        if (symbol("-")) {
            return binary(Op::Negate, unary(), nullptr);
        }
        return primary();
    }

    NodePtr primary()
    {
        skip();
        if (at >= text.size()) {
            return leaf({});
        }
        char c = text[at];
        if (c == '(') {
            ++at;
            NodePtr inner = orExpression();
            symbol(")");
            return inner;
        }
        if (c == '\'') {
            size_t end = text.find('\'', at + 1);
            std::string value(text.substr(at + 1, end == std::string_view::npos ? std::string_view::npos : end - at - 1));
            at = end == std::string_view::npos ? text.size() : end + 1;
            return leaf(UiValue::of(std::move(value)));
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t end = at;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) {
                ++end;
            }
            double value = std::strtod(std::string(text.substr(at, end - at)).c_str(), nullptr);
            at = end;
            return leaf(UiValue::of(value));
        }
        if (word("true")) {
            return leaf(UiValue::of(true));
        }
        if (word("false")) {
            return leaf(UiValue::of(false));
        }
        if (c == '#' || c == '$') {
            size_t start = at++;
            while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_' || text[at] == '.' || text[at] == '|')) {
                ++at;
            }
            auto node = std::make_unique<ExprNode>();
            node->op = Op::Lookup;
            node->name = std::string(text.substr(start, at - start));
            return node;
        }
        ++at;
        return leaf({});
    }

    std::string_view text;
    size_t at = 0;
    int nesting = 0;
};

bool equal(const UiValue& a, const UiValue& b)
{
    if (a.kind == UiValue::Kind::String || b.kind == UiValue::Kind::String) {
        return a.toText() == b.toText();
    }
    return a.toNumber() == b.toNumber();
}

UiValue run(const ExprNode& node, const UiLookup& lookup)
{
    auto left = [&]() { return run(*node.left, lookup); };
    auto right = [&]() { return run(*node.right, lookup); };
    switch (node.op) {
    case Op::Constant:
        return node.constant;
    case Op::Lookup:
        return lookup(node.name);
    case Op::Not:
        return UiValue::of(!left().truthy());
    case Op::Negate:
        return UiValue::of(-left().toNumber());
    case Op::Or: {
        UiValue a = left();
        UiValue b = right();
        return UiValue::of(a.truthy() || b.truthy());
    }
    case Op::And: {
        UiValue a = left();
        UiValue b = right();
        return UiValue::of(a.truthy() && b.truthy());
    }
    case Op::Equal: {
        UiValue a = left();
        return UiValue::of(equal(a, right()));
    }
    case Op::NotEqual: {
        UiValue a = left();
        return UiValue::of(!equal(a, right()));
    }
    case Op::Less: {
        double a = left().toNumber();
        return UiValue::of(a < right().toNumber());
    }
    case Op::LessEqual: {
        double a = left().toNumber();
        return UiValue::of(a <= right().toNumber());
    }
    case Op::Greater: {
        double a = left().toNumber();
        return UiValue::of(a > right().toNumber());
    }
    case Op::GreaterEqual: {
        double a = left().toNumber();
        return UiValue::of(a >= right().toNumber());
    }
    case Op::Add: {
        UiValue a = left();
        UiValue b = right();
        return a.kind == UiValue::Kind::String || b.kind == UiValue::Kind::String ? UiValue::of(a.toText() + b.toText()) : UiValue::of(a.toNumber() + b.toNumber());
    }
    case Op::Subtract: {
        UiValue a = left();
        UiValue b = right();
        if (a.kind == UiValue::Kind::String || b.kind == UiValue::Kind::String) {
            std::string result = a.toText();
            std::string removed = b.toText();
            for (size_t found; !removed.empty() && (found = result.find(removed)) != std::string::npos;) {
                result.erase(found, removed.size());
            }
            return UiValue::of(std::move(result));
        }
        return UiValue::of(a.toNumber() - b.toNumber());
    }
    case Op::Multiply: {
        UiValue a = left();
        UiValue b = right();
        std::string pattern = a.toText();
        if (a.kind == UiValue::Kind::String && pattern.size() > 3 && pattern.compare(0, 2, "%.") == 0 && pattern.back() == 's') {
            size_t count = static_cast<size_t>(std::strtoul(pattern.c_str() + 2, nullptr, 10));
            return UiValue::of(b.toText().substr(0, count));
        }
        return UiValue::of(a.toNumber() * b.toNumber());
    }
    case Op::Divide: {
        double a = left().toNumber();
        double b = right().toNumber();
        return UiValue::of(b != 0.0 ? a / b : 0.0);
    }
    case Op::Modulo: {
        double a = left().toNumber();
        double b = right().toNumber();
        return UiValue::of(b != 0.0 ? std::fmod(a, b) : 0.0);
    }
    }
    return {};
}

/**
 * The parsed tree of an expression, parsed the first time it is met: the
 * same bindings are evaluated every frame for as long as a screen shows.
 */
const ExprNode& compiled(std::string_view source)
{
    thread_local std::unordered_map<std::string, NodePtr> cache;
    auto found = cache.find(std::string(source));
    if (found != cache.end()) {
        return *found->second;
    }
    if (cache.size() >= MaxCachedExpressions) {
        cache.clear();
    }
    return *cache.emplace(std::string(source), Parser(source).parse()).first->second;
}

}

UiValue evaluate(std::string_view source, const UiLookup& lookup)
{
    return run(compiled(source), lookup);
}

/**
 * A size or offset term list. Strings are parsed once and kept, since the
 * layout reads every control's size and offset each frame.
 */
Extent parseExtent(const json::Value* value, TermKind fallback)
{
    if (!value) {
        return { { fallback, 1.0f } };
    }
    if (value->isNumber()) {
        return { { TermKind::Pixel, static_cast<float>(value->number()) } };
    }
    thread_local std::unordered_map<std::string, Extent> cache;
    std::string key = value->string();
    key.push_back('\x1f');
    key.push_back(static_cast<char>('0' + static_cast<int>(fallback)));
    if (auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    if (cache.size() >= MaxCachedExpressions) {
        cache.clear();
    }
    std::string source;
    for (char c : value->string()) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            source.push_back(c);
        }
    }
    Extent terms;
    size_t i = 0;
    while (i < source.size()) {
        float sign = 1.0f;
        if (source[i] == '+' || source[i] == '-') {
            sign = source[i] == '-' ? -1.0f : 1.0f;
            ++i;
        }
        size_t end = source.find_first_of("+-", i + 1);
        std::string term = source.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = end == std::string::npos ? source.size() : end;
        if (term == "default") {
            terms.push_back({ TermKind::Default, sign });
            continue;
        }
        if (term == "fill") {
            terms.push_back({ TermKind::Fill, sign });
            continue;
        }
        char* rest = nullptr;
        float amount = std::strtof(term.c_str(), &rest) * sign;
        std::string suffix(rest ? rest : "");
        if (suffix == "%") {
            terms.push_back({ TermKind::Parent, amount / 100.0f });
        } else if (suffix == "%c") {
            terms.push_back({ TermKind::Children, amount / 100.0f });
        } else if (suffix == "%cm") {
            terms.push_back({ TermKind::ChildrenMax, amount / 100.0f });
        } else if (suffix == "%sm") {
            terms.push_back({ TermKind::SiblingMax, amount / 100.0f });
        } else if (suffix == "%x") {
            terms.push_back({ TermKind::OwnX, amount / 100.0f });
        } else if (suffix == "%y") {
            terms.push_back({ TermKind::OwnY, amount / 100.0f });
        } else {
            terms.push_back({ TermKind::Pixel, amount });
        }
    }
    if (terms.empty()) {
        terms.push_back({ fallback, 1.0f });
    }
    cache.emplace(std::move(key), terms);
    return terms;
}

bool uses(const Extent& extent, TermKind kind)
{
    return std::any_of(extent.begin(), extent.end(), [&](const Term& term) { return term.kind == kind; });
}

}
