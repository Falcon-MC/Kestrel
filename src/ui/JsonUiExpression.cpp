#include "JsonUiInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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
    default:
        return {};
    }
}

namespace {

class Expression {
public:
    using Lookup = UiLookup;

    Expression(std::string_view source, const Lookup& lookup)
        : text(source)
        , lookup(lookup)
    {
    }

    UiValue evaluate()
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

    UiValue orExpression()
    {
        UiValue left = andExpression();
        while (word("or")) {
            UiValue right = andExpression();
            left = UiValue::of(left.truthy() || right.truthy());
        }
        return left;
    }

    UiValue andExpression()
    {
        UiValue left = comparison();
        while (word("and")) {
            UiValue right = comparison();
            left = UiValue::of(left.truthy() && right.truthy());
        }
        return left;
    }

    static bool equal(const UiValue& a, const UiValue& b)
    {
        if (a.kind == UiValue::Kind::String || b.kind == UiValue::Kind::String) {
            return a.toText() == b.toText();
        }
        return a.toNumber() == b.toNumber();
    }

    UiValue comparison()
    {
        UiValue left = sum();
        skip();
        if (symbol(">=")) {
            return UiValue::of(left.toNumber() >= sum().toNumber());
        }
        if (symbol("<=")) {
            return UiValue::of(left.toNumber() <= sum().toNumber());
        }
        if (symbol("!=")) {
            return UiValue::of(!equal(left, sum()));
        }
        if (symbol("=")) {
            return UiValue::of(equal(left, sum()));
        }
        if (symbol(">")) {
            return UiValue::of(left.toNumber() > sum().toNumber());
        }
        if (symbol("<")) {
            return UiValue::of(left.toNumber() < sum().toNumber());
        }
        return left;
    }

    UiValue sum()
    {
        UiValue left = product();
        while (true) {
            if (symbol("+")) {
                UiValue right = product();
                left = left.kind == UiValue::Kind::String || right.kind == UiValue::Kind::String ? UiValue::of(left.toText() + right.toText()) : UiValue::of(left.toNumber() + right.toNumber());
            } else if (symbol("-")) {
                UiValue right = product();
                if (left.kind == UiValue::Kind::String || right.kind == UiValue::Kind::String) {
                    std::string result = left.toText();
                    std::string removed = right.toText();
                    for (size_t found; !removed.empty() && (found = result.find(removed)) != std::string::npos;) {
                        result.erase(found, removed.size());
                    }
                    left = UiValue::of(std::move(result));
                } else {
                    left = UiValue::of(left.toNumber() - right.toNumber());
                }
            } else {
                return left;
            }
        }
    }

    UiValue product()
    {
        UiValue left = unary();
        while (true) {
            if (symbol("*")) {
                UiValue right = unary();
                std::string pattern = left.toText();
                if (left.kind == UiValue::Kind::String && pattern.size() > 3 && pattern.compare(0, 2, "%.") == 0 && pattern.back() == 's') {
                    size_t count = static_cast<size_t>(std::strtoul(pattern.c_str() + 2, nullptr, 10));
                    left = UiValue::of(right.toText().substr(0, count));
                } else {
                    left = UiValue::of(left.toNumber() * right.toNumber());
                }
            } else if (symbol("/")) {
                double right = unary().toNumber();
                left = UiValue::of(right != 0.0 ? left.toNumber() / right : 0.0);
            } else if (symbol("%")) {
                double right = unary().toNumber();
                left = UiValue::of(right != 0.0 ? std::fmod(left.toNumber(), right) : 0.0);
            } else {
                return left;
            }
        }
    }

    UiValue unary()
    {
        // Packs are untrusted, and deep enough nesting would run the stack out.
        if (++nesting > MaxDepth) {
            at = text.size();
            return {};
        }
        UiValue value = prefixed();
        --nesting;
        return value;
    }

    UiValue prefixed()
    {
        if (word("not")) {
            return UiValue::of(!unary().truthy());
        }
        if (symbol("-")) {
            return UiValue::of(-unary().toNumber());
        }
        return primary();
    }

    UiValue primary()
    {
        skip();
        if (at >= text.size()) {
            return {};
        }
        char c = text[at];
        if (c == '(') {
            ++at;
            UiValue inner = orExpression();
            symbol(")");
            return inner;
        }
        if (c == '\'') {
            size_t end = text.find('\'', at + 1);
            std::string value(text.substr(at + 1, end == std::string_view::npos ? std::string_view::npos : end - at - 1));
            at = end == std::string_view::npos ? text.size() : end + 1;
            return UiValue::of(std::move(value));
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t end = at;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) {
                ++end;
            }
            double value = std::strtod(std::string(text.substr(at, end - at)).c_str(), nullptr);
            at = end;
            return UiValue::of(value);
        }
        if (word("true")) {
            return UiValue::of(true);
        }
        if (word("false")) {
            return UiValue::of(false);
        }
        if (c == '#' || c == '$') {
            size_t start = at++;
            while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_' || text[at] == '.' || text[at] == '|')) {
                ++at;
            }
            return lookup(std::string(text.substr(start, at - start)));
        }
        ++at;
        return {};
    }

    std::string_view text;
    const Lookup& lookup;
    size_t at = 0;
    int nesting = 0;
};

}

UiValue evaluate(std::string_view source, const UiLookup& lookup)
{
    Expression expression(source, lookup);
    return expression.evaluate();
}

Extent parseExtent(const json::Value* value, TermKind fallback)
{
    if (!value) {
        return { { fallback, 1.0f } };
    }
    if (value->isNumber()) {
        return { { TermKind::Pixel, static_cast<float>(value->number()) } };
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
    return terms;
}

bool uses(const Extent& extent, TermKind kind)
{
    return std::any_of(extent.begin(), extent.end(), [&](const Term& term) { return term.kind == kind; });
}

}
