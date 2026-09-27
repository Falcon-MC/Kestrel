#include "world/Molang.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace kestrel::world {

namespace {

struct Value {
    bool isString = false;
    double number = 0.0;
    std::string text;

    bool truthy() const
    {
        return isString ? !text.empty() : number != 0.0;
    }
};

Value numberValue(double number)
{
    Value value;
    value.number = number;
    return value;
}

Value stateValue(const Tag& states, const std::string& name)
{
    const Tag* entry = states.get(name);
    if (!entry) {
        return {};
    }
    switch (entry->getType()) {
    case Tag::Type::Byte:
        return numberValue(entry->asByte());
    case Tag::Type::Short:
        return numberValue(entry->asShort());
    case Tag::Type::Int:
        return numberValue(entry->asInt());
    case Tag::Type::Long:
        return numberValue(static_cast<double>(entry->asLong()));
    case Tag::Type::Float:
        return numberValue(entry->asFloat());
    case Tag::Type::Double:
        return numberValue(entry->asDouble());
    case Tag::Type::String: {
        Value value;
        value.isString = true;
        value.text = entry->asString();
        return value;
    }
    default:
        return {};
    }
}

bool equal(const Value& left, const Value& right)
{
    if (left.isString || right.isString) {
        return left.isString && right.isString && left.text == right.text;
    }
    return std::abs(left.number - right.number) < 1e-6;
}

class Parser {
public:
    Parser(const std::string& source, const Tag& states)
        : source(source)
        , states(states)
    {
    }

    Value parse()
    {
        Value value = orExpression();
        return value;
    }

private:
    void skipSpace()
    {
        while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) {
            ++position;
        }
    }

    bool match(const char* token)
    {
        skipSpace();
        size_t length = std::char_traits<char>::length(token);
        if (source.compare(position, length, token) == 0) {
            position += length;
            return true;
        }
        return false;
    }

    Value orExpression()
    {
        Value left = andExpression();
        while (match("||")) {
            Value right = andExpression();
            left = numberValue(left.truthy() || right.truthy());
        }
        return left;
    }

    Value andExpression()
    {
        Value left = equality();
        while (match("&&")) {
            Value right = equality();
            left = numberValue(left.truthy() && right.truthy());
        }
        return left;
    }

    Value equality()
    {
        Value left = comparison();
        while (true) {
            if (match("==")) {
                left = numberValue(equal(left, comparison()));
            } else if (match("!=")) {
                left = numberValue(!equal(left, comparison()));
            } else {
                return left;
            }
        }
    }

    Value comparison()
    {
        Value left = additive();
        while (true) {
            if (match("<=")) {
                left = numberValue(left.number <= additive().number);
            } else if (match(">=")) {
                left = numberValue(left.number >= additive().number);
            } else if (match("<")) {
                left = numberValue(left.number < additive().number);
            } else if (match(">")) {
                left = numberValue(left.number > additive().number);
            } else {
                return left;
            }
        }
    }

    Value additive()
    {
        Value left = unary();
        while (true) {
            if (match("+")) {
                left = numberValue(left.number + unary().number);
            } else if (match("-")) {
                left = numberValue(left.number - unary().number);
            } else {
                return left;
            }
        }
    }

    Value unary()
    {
        if (match("!")) {
            return numberValue(!unary().truthy());
        }
        if (match("-")) {
            return numberValue(-unary().number);
        }
        return primary();
    }

    std::string identifier()
    {
        skipSpace();
        size_t start = position;
        while (position < source.size() && (std::isalnum(static_cast<unsigned char>(source[position])) || source[position] == '_' || source[position] == '.')) {
            ++position;
        }
        std::string name = source.substr(start, position - start);
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return name;
    }

    Value primary()
    {
        skipSpace();
        if (position >= source.size()) {
            return {};
        }
        char c = source[position];
        if (c == '(') {
            ++position;
            Value value = orExpression();
            match(")");
            return value;
        }
        if (c == '\'' || c == '"') {
            ++position;
            size_t end = source.find(c, position);
            Value value;
            value.isString = true;
            value.text = source.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = end == std::string::npos ? source.size() : end + 1;
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            char* end = nullptr;
            double number = std::strtod(source.c_str() + position, &end);
            position = static_cast<size_t>(end - source.c_str());
            return numberValue(number);
        }
        std::string name = identifier();
        if (name == "true") {
            return numberValue(1.0);
        }
        if (name == "false") {
            return numberValue(0.0);
        }
        if (match("(")) {
            Value argument = orExpression();
            match(")");
            bool stateQuery = name == "q.block_state" || name == "query.block_state" || name == "q.block_property" || name == "query.block_property";
            if (stateQuery && argument.isString) {
                return stateValue(states, argument.text);
            }
            return {};
        }
        return {};
    }

    const std::string& source;
    const Tag& states;
    size_t position = 0;
};

}

bool evaluateCondition(const std::string& expression, const Tag& states)
{
    if (expression.empty()) {
        return true;
    }
    return Parser(expression, states).parse().truthy();
}

}
