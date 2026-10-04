#include "world/MolangScript.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <random>
#include <shared_mutex>

namespace kestrel::world::molang {

enum class Kind {
    Number,
    Variable,
    Temp,
    Context,
    Query,
    Math,
    Negate,
    Not,
    Binary,
    Ternary,
    Conditional,
    Coalesce,
    Assign,
    Sequence,
    Return,
    Loop,
    Break,
    Continue,
    ForEach,
    Array,
    Arrow,
    This,
};

enum class Op {
    Add,
    Subtract,
    Multiply,
    Divide,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or,
};

enum class Target {
    Variable,
    Temp,
    Context,
};

struct Node {
    Kind kind = Kind::Number;
    Op op = Op::Add;
    Target target = Target::Variable;
    double number = 0.0;
    std::string name;
    std::vector<std::shared_ptr<const Node>> children;
};

namespace {

using NodePtr = std::shared_ptr<const Node>;

constexpr double Pi = 3.14159265358979323846;
constexpr double Radians = Pi / 180.0;

enum class Flow {
    Normal,
    Return,
    Break,
    Continue,
};

struct Token {
    enum class Type {
        End,
        Number,
        Name,
        String,
        Symbol,
    };

    Type type = Type::End;
    std::string text;
    double number = 0.0;
};

std::vector<Token> tokenize(const std::string& source)
{
    std::vector<Token> tokens;
    size_t index = 0;
    while (index < source.size()) {
        unsigned char c = static_cast<unsigned char>(source[index]);
        if (std::isspace(c)) {
            ++index;
            continue;
        }
        if (std::isdigit(c) || (c == '.' && index + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[index + 1])))) {
            char* end = nullptr;
            double value = std::strtod(source.c_str() + index, &end);
            index = static_cast<size_t>(end - source.c_str());
            if (index < source.size() && (source[index] == 'f' || source[index] == 'F')) {
                ++index;
            }
            Token token;
            token.type = Token::Type::Number;
            token.number = value;
            tokens.push_back(token);
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = index;
            while (index < source.size()) {
                unsigned char next = static_cast<unsigned char>(source[index]);
                if (!std::isalnum(next) && next != '_' && next != '.') {
                    break;
                }
                ++index;
            }
            Token token;
            token.type = Token::Type::Name;
            token.text = source.substr(start, index - start);
            std::transform(token.text.begin(), token.text.end(), token.text.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            tokens.push_back(token);
            continue;
        }
        if (c == '\'' || c == '"') {
            size_t end = source.find(static_cast<char>(c), index + 1);
            if (end == std::string::npos) {
                end = source.size();
            }
            Token token;
            token.type = Token::Type::String;
            token.text = source.substr(index + 1, end - index - 1);
            tokens.push_back(token);
            index = std::min(end + 1, source.size());
            continue;
        }
        static const char* Pairs[] = { "==", "!=", "<=", ">=", "&&", "||", "??", "->" };
        Token token;
        token.type = Token::Type::Symbol;
        token.text = std::string(1, static_cast<char>(c));
        for (const char* pair : Pairs) {
            if (source.compare(index, 2, pair) == 0) {
                token.text = pair;
                break;
            }
        }
        index += token.text.size();
        tokens.push_back(token);
    }
    tokens.push_back(Token {});
    return tokens;
}

NodePtr makeNumber(double value)
{
    auto node = std::make_shared<Node>();
    node->kind = Kind::Number;
    node->number = value;
    return node;
}

class Parser {
public:
    explicit Parser(std::vector<Token> source)
        : tokens(std::move(source))
    {
    }

    NodePtr program(bool& complex)
    {
        auto sequence = std::make_shared<Node>();
        sequence->kind = Kind::Sequence;
        complex = false;
        while (!atEnd() && !isSymbol("}")) {
            if (accept(";")) {
                complex = true;
                continue;
            }
            sequence->children.push_back(statement());
            if (accept(";")) {
                complex = true;
            } else if (!atEnd() && !isSymbol("}")) {
                failed = true;
                break;
            }
        }
        if (!complex && sequence->children.size() == 1) {
            return sequence->children.front();
        }
        return sequence;
    }

    bool ok() const
    {
        return !failed;
    }

private:
    const Token& peek(size_t ahead = 0) const
    {
        return tokens[std::min(position + ahead, tokens.size() - 1)];
    }

    bool atEnd() const
    {
        return peek().type == Token::Type::End || failed;
    }

    bool isSymbol(const char* symbol, size_t ahead = 0) const
    {
        const Token& token = peek(ahead);
        return token.type == Token::Type::Symbol && token.text == symbol;
    }

    bool accept(const char* symbol)
    {
        if (isSymbol(symbol)) {
            ++position;
            return true;
        }
        return false;
    }

    void expect(const char* symbol)
    {
        if (!accept(symbol)) {
            failed = true;
        }
    }

    static bool splitTarget(const std::string& name, Target& target, std::string& key)
    {
        static const std::pair<const char*, Target> Prefixes[] = {
            { "variable.", Target::Variable },
            { "v.", Target::Variable },
            { "temp.", Target::Temp },
            { "t.", Target::Temp },
            { "context.", Target::Context },
            { "c.", Target::Context },
        };
        for (const auto& [prefix, kind] : Prefixes) {
            size_t length = std::char_traits<char>::length(prefix);
            if (name.size() > length && name.compare(0, length, prefix) == 0) {
                target = kind;
                key = name.substr(length);
                return true;
            }
        }
        return false;
    }

    NodePtr statement()
    {
        const Token& token = peek();
        if (token.type == Token::Type::Name) {
            if (token.text == "return") {
                ++position;
                auto node = std::make_shared<Node>();
                node->kind = Kind::Return;
                node->children.push_back(expression());
                return node;
            }
            if (token.text == "break" || token.text == "continue") {
                ++position;
                auto node = std::make_shared<Node>();
                node->kind = token.text == "break" ? Kind::Break : Kind::Continue;
                return node;
            }
            Target target {};
            std::string key;
            if (isSymbol("=", 1) && splitTarget(token.text, target, key)) {
                position += 2;
                auto node = std::make_shared<Node>();
                node->kind = Kind::Assign;
                node->target = target;
                node->name = key;
                node->children.push_back(expression());
                return node;
            }
        }
        return expression();
    }

    NodePtr expression()
    {
        NodePtr left = conditional();
        if (accept("??")) {
            auto node = std::make_shared<Node>();
            node->kind = Kind::Coalesce;
            node->children = { left, expression() };
            return node;
        }
        return left;
    }

    NodePtr conditional()
    {
        NodePtr condition = logicalOr();
        if (accept("?")) {
            NodePtr yes = conditional();
            auto node = std::make_shared<Node>();
            if (accept(":")) {
                node->kind = Kind::Ternary;
                node->children = { condition, yes, conditional() };
            } else {
                node->kind = Kind::Conditional;
                node->children = { condition, yes };
            }
            return node;
        }
        return condition;
    }

    NodePtr binary(NodePtr left, Op op, NodePtr right)
    {
        auto node = std::make_shared<Node>();
        node->kind = Kind::Binary;
        node->op = op;
        node->children = { std::move(left), std::move(right) };
        return node;
    }

    NodePtr logicalOr()
    {
        NodePtr left = logicalAnd();
        while (accept("||")) {
            left = binary(left, Op::Or, logicalAnd());
        }
        return left;
    }

    NodePtr logicalAnd()
    {
        NodePtr left = equality();
        while (accept("&&")) {
            left = binary(left, Op::And, equality());
        }
        return left;
    }

    NodePtr equality()
    {
        NodePtr left = relational();
        while (true) {
            if (accept("==")) {
                left = binary(left, Op::Equal, relational());
            } else if (accept("!=")) {
                left = binary(left, Op::NotEqual, relational());
            } else {
                return left;
            }
        }
    }

    NodePtr relational()
    {
        NodePtr left = additive();
        while (true) {
            static const std::pair<const char*, Op> Operators[] = {
                { "<=", Op::LessEqual },
                { ">=", Op::GreaterEqual },
                { "<", Op::Less },
                { ">", Op::Greater },
            };
            bool matched = false;
            for (const auto& [symbol, op] : Operators) {
                if (accept(symbol)) {
                    left = binary(left, op, additive());
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return left;
            }
        }
    }

    NodePtr additive()
    {
        NodePtr left = multiplicative();
        while (true) {
            if (accept("+")) {
                left = binary(left, Op::Add, multiplicative());
            } else if (accept("-")) {
                left = binary(left, Op::Subtract, multiplicative());
            } else {
                return left;
            }
        }
    }

    NodePtr multiplicative()
    {
        NodePtr left = unary();
        while (true) {
            if (accept("*")) {
                left = binary(left, Op::Multiply, unary());
            } else if (accept("/")) {
                left = binary(left, Op::Divide, unary());
            } else {
                return left;
            }
        }
    }

    NodePtr unary()
    {
        if (accept("-")) {
            auto node = std::make_shared<Node>();
            node->kind = Kind::Negate;
            node->children.push_back(unary());
            return node;
        }
        if (accept("+")) {
            return unary();
        }
        if (accept("!")) {
            auto node = std::make_shared<Node>();
            node->kind = Kind::Not;
            node->children.push_back(unary());
            return node;
        }
        return postfix(primary());
    }

    NodePtr postfix(NodePtr value)
    {
        while (true) {
            if (accept("[")) {
                NodePtr index = expression();
                expect("]");
                if (value->kind == Kind::Array && value->children.empty()) {
                    auto node = std::make_shared<Node>();
                    node->kind = Kind::Array;
                    node->name = value->name;
                    node->children.push_back(index);
                    value = node;
                } else {
                    failed = true;
                }
            } else if (accept("->")) {
                auto node = std::make_shared<Node>();
                node->kind = Kind::Arrow;
                node->children = { value, postfix(primary()) };
                return node;
            } else {
                return value;
            }
        }
    }

    std::vector<NodePtr> arguments()
    {
        std::vector<NodePtr> list;
        if (!accept("(")) {
            return list;
        }
        if (accept(")")) {
            return list;
        }
        do {
            list.push_back(expression());
        } while (accept(","));
        expect(")");
        return list;
    }

    NodePtr block()
    {
        if (!accept("{")) {
            return expression();
        }
        bool complex = false;
        NodePtr body = program(complex);
        expect("}");
        if (body->kind != Kind::Sequence) {
            auto sequence = std::make_shared<Node>();
            sequence->kind = Kind::Sequence;
            sequence->children.push_back(body);
            return sequence;
        }
        return body;
    }

    NodePtr primary()
    {
        const Token token = peek();
        if (failed) {
            return makeNumber(0.0);
        }
        switch (token.type) {
        case Token::Type::Number:
            ++position;
            return makeNumber(token.number);
        case Token::Type::String:
            ++position;
            return makeNumber(stringHash(token.text));
        case Token::Type::Symbol:
            if (accept("(")) {
                NodePtr inner = expression();
                expect(")");
                return inner;
            }
            if (isSymbol("{")) {
                return block();
            }
            failed = true;
            return makeNumber(0.0);
        case Token::Type::Name:
            break;
        default:
            failed = true;
            return makeNumber(0.0);
        }
        ++position;
        const std::string& name = token.text;
        if (name == "true") {
            return makeNumber(1.0);
        }
        if (name == "false") {
            return makeNumber(0.0);
        }
        if (name == "this") {
            auto node = std::make_shared<Node>();
            node->kind = Kind::This;
            return node;
        }
        if (name == "for_each") {
            expect("(");
            Target target {};
            std::string key;
            const Token variable = peek();
            if (variable.type != Token::Type::Name || !splitTarget(variable.text, target, key)) {
                failed = true;
                return makeNumber(0.0);
            }
            ++position;
            expect(",");
            auto node = std::make_shared<Node>();
            node->kind = Kind::ForEach;
            node->target = target;
            node->name = key;
            node->children.push_back(expression());
            expect(",");
            node->children.push_back(block());
            expect(")");
            return node;
        }
        if (name.rfind("array.", 0) == 0 && name.size() > 6) {
            auto node = std::make_shared<Node>();
            node->kind = Kind::Array;
            node->name = name.substr(6);
            return node;
        }
        if (name == "loop") {
            expect("(");
            auto node = std::make_shared<Node>();
            node->kind = Kind::Loop;
            node->children.push_back(expression());
            expect(",");
            node->children.push_back(block());
            expect(")");
            return node;
        }
        Target target {};
        std::string key;
        if (splitTarget(name, target, key)) {
            auto node = std::make_shared<Node>();
            node->kind = target == Target::Variable ? Kind::Variable : target == Target::Temp ? Kind::Temp : Kind::Context;
            node->name = key;
            return node;
        }
        auto node = std::make_shared<Node>();
        if (name.rfind("query.", 0) == 0 || name.rfind("q.", 0) == 0) {
            node->kind = Kind::Query;
            node->name = name.substr(name[1] == '.' ? 2 : 6);
            node->children = arguments();
            return node;
        }
        if (name.rfind("math.", 0) == 0) {
            node->kind = Kind::Math;
            node->name = name.substr(5);
            node->children = arguments();
            return node;
        }
        arguments();
        return makeNumber(stringHash(name));
    }

    std::vector<Token> tokens;
    size_t position = 0;
    bool failed = false;
};

double randomBetween(double low, double high)
{
    thread_local std::mt19937 engine { std::random_device {}() };
    if (high < low) {
        std::swap(low, high);
    }
    std::uniform_real_distribution<double> distribution(low, high);
    return low == high ? low : distribution(engine);
}

double wrapAngle(double degrees)
{
    double value = std::fmod(degrees + 180.0, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value - 180.0;
}

double easeOutBounce(double t)
{
    const double n = 7.5625;
    const double d = 2.75;
    if (t < 1.0 / d) {
        return n * t * t;
    }
    if (t < 2.0 / d) {
        t -= 1.5 / d;
        return n * t * t + 0.75;
    }
    if (t < 2.5 / d) {
        t -= 2.25 / d;
        return n * t * t + 0.9375;
    }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}

double easeIn(const std::string& curve, double t)
{
    if (curve == "quad") {
        return t * t;
    }
    if (curve == "cubic") {
        return t * t * t;
    }
    if (curve == "quart") {
        return t * t * t * t;
    }
    if (curve == "quint") {
        return t * t * t * t * t;
    }
    if (curve == "sine") {
        return 1.0 - std::cos(t * Pi / 2.0);
    }
    if (curve == "expo") {
        return t <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
    }
    if (curve == "circ") {
        return 1.0 - std::sqrt(std::max(0.0, 1.0 - t * t));
    }
    if (curve == "back") {
        const double c1 = 1.70158;
        return (c1 + 1.0) * t * t * t - c1 * t * t;
    }
    if (curve == "elastic") {
        if (t <= 0.0 || t >= 1.0) {
            return t <= 0.0 ? 0.0 : 1.0;
        }
        return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((t * 10.0 - 10.75) * (2.0 * Pi / 3.0));
    }
    if (curve == "bounce") {
        return 1.0 - easeOutBounce(1.0 - t);
    }
    return t;
}

double easeCurve(const std::string& mode, const std::string& curve, double t)
{
    if (mode == "in") {
        return easeIn(curve, t);
    }
    if (mode == "out") {
        return 1.0 - easeIn(curve, 1.0 - t);
    }
    if (t < 0.5) {
        return easeIn(curve, 2.0 * t) / 2.0;
    }
    return 1.0 - easeIn(curve, 2.0 - 2.0 * t) / 2.0;
}

double callMath(const std::string& name, std::span<const double> a)
{
    auto at = [&](size_t index) {
        return index < a.size() ? a[index] : 0.0;
    };
    if (name.rfind("ease_", 0) == 0) {
        std::string rest = name.substr(5);
        std::string mode;
        if (rest.rfind("in_out_", 0) == 0) {
            mode = "in_out";
            rest = rest.substr(7);
        } else if (rest.rfind("in_", 0) == 0) {
            mode = "in";
            rest = rest.substr(3);
        } else if (rest.rfind("out_", 0) == 0) {
            mode = "out";
            rest = rest.substr(4);
        } else {
            return 0.0;
        }
        return at(0) + (at(1) - at(0)) * easeCurve(mode, rest, at(2));
    }
    if (name == "abs") {
        return std::abs(at(0));
    }
    if (name == "sin") {
        return std::sin(at(0) * Radians);
    }
    if (name == "cos") {
        return std::cos(at(0) * Radians);
    }
    if (name == "asin") {
        return std::asin(std::clamp(at(0), -1.0, 1.0)) / Radians;
    }
    if (name == "acos") {
        return std::acos(std::clamp(at(0), -1.0, 1.0)) / Radians;
    }
    if (name == "atan") {
        return std::atan(at(0)) / Radians;
    }
    if (name == "atan2") {
        return std::atan2(at(0), at(1)) / Radians;
    }
    if (name == "ceil") {
        return std::ceil(at(0));
    }
    if (name == "floor") {
        return std::floor(at(0));
    }
    if (name == "round") {
        return std::round(at(0));
    }
    if (name == "trunc") {
        return std::trunc(at(0));
    }
    if (name == "sqrt") {
        return at(0) > 0.0 ? std::sqrt(at(0)) : 0.0;
    }
    if (name == "exp") {
        return std::exp(at(0));
    }
    if (name == "ln") {
        return at(0) > 0.0 ? std::log(at(0)) : 0.0;
    }
    if (name == "pow") {
        return std::pow(at(0), at(1));
    }
    if (name == "mod") {
        return at(1) != 0.0 ? std::fmod(at(0), at(1)) : 0.0;
    }
    if (name == "min") {
        return std::min(at(0), at(1));
    }
    if (name == "max") {
        return std::max(at(0), at(1));
    }
    if (name == "clamp") {
        return std::clamp(at(0), std::min(at(1), at(2)), std::max(at(1), at(2)));
    }
    if (name == "lerp") {
        return at(0) + (at(1) - at(0)) * at(2);
    }
    if (name == "inverse_lerp") {
        return at(1) != at(0) ? (at(2) - at(0)) / (at(1) - at(0)) : 0.0;
    }
    if (name == "lerprotate") {
        return at(0) + wrapAngle(at(1) - at(0)) * at(2);
    }
    if (name == "min_angle") {
        return wrapAngle(at(0));
    }
    if (name == "hermite_blend") {
        double t = at(0);
        return 3.0 * t * t - 2.0 * t * t * t;
    }
    if (name == "sign") {
        return at(0) < 0.0 ? -1.0 : 1.0;
    }
    if (name == "copy_sign") {
        return std::copysign(at(0), at(1));
    }
    if (name == "pi") {
        return Pi;
    }
    if (name == "random") {
        return randomBetween(at(0), at(1));
    }
    if (name == "random_integer") {
        return std::floor(randomBetween(std::round(at(0)), std::round(at(1)) + 0.999999));
    }
    if (name == "die_roll" || name == "die_roll_integer") {
        double total = 0.0;
        int count = static_cast<int>(std::clamp(at(0), 0.0, 1024.0));
        for (int roll = 0; roll < count; ++roll) {
            if (name == "die_roll") {
                total += randomBetween(at(1), at(2));
            } else {
                total += std::floor(randomBetween(std::round(at(1)), std::round(at(2)) + 0.999999));
            }
        }
        return total;
    }
    return 0.0;
}

double* slot(const Node& node, Scope& scope, Target target)
{
    switch (target) {
    case Target::Variable:
        return scope.variables ? &(*scope.variables)[node.name] : nullptr;
    case Target::Temp:
        return &scope.temps[node.name];
    case Target::Context:
        return &scope.context[node.name];
    }
    return nullptr;
}

const double* find(const Node& node, Scope& scope)
{
    const std::unordered_map<std::string, double>* map = nullptr;
    if (node.kind == Kind::Variable) {
        map = scope.variables;
    } else if (node.kind == Kind::Temp) {
        map = &scope.temps;
    } else {
        map = &scope.context;
    }
    if (!map) {
        return nullptr;
    }
    auto found = map->find(node.name);
    return found == map->end() ? nullptr : &found->second;
}

/**
 * Whether a variable holds a value, or is a struct with a member set, the
 * way ?? tests it.
 */
bool defined(const Node& node, Scope& scope)
{
    if (find(node, scope)) {
        return true;
    }
    const std::unordered_map<std::string, double>* map = node.kind == Kind::Variable ? scope.variables : node.kind == Kind::Temp ? &scope.temps : &scope.context;
    if (!map) {
        return false;
    }
    std::string member = node.name + ".";
    for (const auto& [name, value] : *map) {
        if (name.compare(0, member.size(), member) == 0) {
            return true;
        }
    }
    return false;
}

double evaluate(const Node& node, Scope& scope, Flow& flow);

double evaluateSequence(const Node& node, Scope& scope, Flow& flow)
{
    for (const NodePtr& child : node.children) {
        double value = evaluate(*child, scope, flow);
        if (flow != Flow::Normal) {
            return value;
        }
    }
    return 0.0;
}

double evaluate(const Node& node, Scope& scope, Flow& flow)
{
    switch (node.kind) {
    case Kind::Number:
        return node.number;
    case Kind::Variable:
    case Kind::Temp:
    case Kind::Context: {
        const double* value = find(node, scope);
        return value ? *value : 0.0;
    }
    case Kind::Query:
    case Kind::Math: {
        std::array<double, MaxArguments> values {};
        size_t count = std::min(node.children.size(), MaxArguments);
        for (size_t index = 0; index < count; ++index) {
            values[index] = evaluate(*node.children[index], scope, flow);
        }
        std::span<const double> arguments(values.data(), count);
        if (node.kind == Kind::Math) {
            return callMath(node.name, arguments);
        }
        return scope.queries ? scope.queries->query(node.name, arguments) : 0.0;
    }
    case Kind::Negate:
        return -evaluate(*node.children[0], scope, flow);
    case Kind::Not:
        return evaluate(*node.children[0], scope, flow) == 0.0 ? 1.0 : 0.0;
    case Kind::Binary: {
        double left = evaluate(*node.children[0], scope, flow);
        if (node.op == Op::And && left == 0.0) {
            return 0.0;
        }
        if (node.op == Op::Or && left != 0.0) {
            return 1.0;
        }
        double right = evaluate(*node.children[1], scope, flow);
        switch (node.op) {
        case Op::Add:
            return left + right;
        case Op::Subtract:
            return left - right;
        case Op::Multiply:
            return left * right;
        case Op::Divide:
            return right != 0.0 ? left / right : 0.0;
        case Op::Less:
            return left < right ? 1.0 : 0.0;
        case Op::LessEqual:
            return left <= right ? 1.0 : 0.0;
        case Op::Greater:
            return left > right ? 1.0 : 0.0;
        case Op::GreaterEqual:
            return left >= right ? 1.0 : 0.0;
        case Op::Equal:
            return left == right ? 1.0 : 0.0;
        case Op::NotEqual:
            return left != right ? 1.0 : 0.0;
        case Op::And:
        case Op::Or:
            return right != 0.0 ? 1.0 : 0.0;
        }
        return 0.0;
    }
    case Kind::Ternary:
        return evaluate(*node.children[0], scope, flow) != 0.0 ? evaluate(*node.children[1], scope, flow) : evaluate(*node.children[2], scope, flow);
    case Kind::Conditional:
        return evaluate(*node.children[0], scope, flow) != 0.0 ? evaluate(*node.children[1], scope, flow) : 0.0;
    case Kind::Coalesce: {
        const Node& left = *node.children[0];
        if (left.kind == Kind::Variable || left.kind == Kind::Temp || left.kind == Kind::Context) {
            if (!defined(left, scope)) {
                return evaluate(*node.children[1], scope, flow);
            }
            const double* value = find(left, scope);
            return value ? *value : 0.0;
        }
        return evaluate(left, scope, flow);
    }
    case Kind::Assign: {
        double value = evaluate(*node.children[0], scope, flow);
        if (double* target = slot(node, scope, node.target)) {
            *target = value;
        }
        return value;
    }
    case Kind::Sequence:
        return evaluateSequence(node, scope, flow);
    case Kind::Return: {
        double value = evaluate(*node.children[0], scope, flow);
        flow = Flow::Return;
        return value;
    }
    case Kind::Loop: {
        int count = static_cast<int>(std::clamp(evaluate(*node.children[0], scope, flow), 0.0, 1024.0));
        for (int iteration = 0; iteration < count; ++iteration) {
            double value = evaluate(*node.children[1], scope, flow);
            if (flow == Flow::Return) {
                return value;
            }
            if (flow == Flow::Break) {
                flow = Flow::Normal;
                break;
            }
            flow = Flow::Normal;
        }
        return 0.0;
    }
    case Kind::ForEach: {
        const Node& source = *node.children[0];
        std::vector<double> items;
        if (source.kind == Kind::Array && source.children.empty()) {
            auto found = scope.arrays.find(source.name);
            if (found != scope.arrays.end()) {
                items = found->second;
            }
        } else {
            items.push_back(evaluate(source, scope, flow));
        }
        for (double item : items) {
            if (double* target = slot(node, scope, node.target)) {
                *target = item;
            }
            double value = evaluate(*node.children[1], scope, flow);
            if (flow == Flow::Return) {
                return value;
            }
            if (flow == Flow::Break) {
                flow = Flow::Normal;
                break;
            }
            flow = Flow::Normal;
        }
        return 0.0;
    }
    case Kind::Array: {
        auto found = scope.arrays.find(node.name);
        if (found == scope.arrays.end() || found->second.empty()) {
            return 0.0;
        }
        const std::vector<double>& items = found->second;
        double raw = node.children.empty() ? 0.0 : evaluate(*node.children[0], scope, flow);
        if (!std::isfinite(raw) || raw < 0.0) {
            return items.front();
        }
        size_t index = static_cast<size_t>(std::floor(raw)) % items.size();
        return items[index];
    }
    case Kind::Arrow: {
        double target = evaluate(*node.children[0], scope, flow);
        EntityContext entity;
        if (!scope.resolveEntity || !scope.resolveEntity(target, entity)) {
            return 0.0;
        }
        Scope other;
        other.variables = entity.variables;
        other.queries = entity.queries;
        other.random = scope.random;
        other.thisValue = scope.thisValue;
        other.context = scope.context;
        other.arrays = scope.arrays;
        other.resolveEntity = scope.resolveEntity;
        Flow inner = Flow::Normal;
        return evaluate(*node.children[1], other, inner);
    }
    case Kind::This:
        return scope.thisValue;
    case Kind::Break:
        flow = Flow::Break;
        return 0.0;
    case Kind::Continue:
        flow = Flow::Continue;
        return 0.0;
    }
    return 0.0;
}

}

namespace {

struct InternTable {
    std::shared_mutex mutex;
    std::unordered_map<double, std::string> strings;
};

InternTable& internTable()
{
    static InternTable table;
    return table;
}

}

double stringHash(const std::string& text)
{
    if (text.empty()) {
        return 0.0;
    }
    uint32_t hash = 2166136261u;
    for (char c : text) {
        hash ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c)));
        hash *= 16777619u;
    }
    double value = static_cast<double>(hash | 1u) + 1.0e6;
    InternTable& interned = internTable();
    {
        std::shared_lock lock(interned.mutex);
        if (interned.strings.count(value)) {
            return value;
        }
    }
    std::unique_lock lock(interned.mutex);
    interned.strings.emplace(value, text);
    return value;
}

double internString(const std::string& text)
{
    return stringHash(text);
}

std::string stringOf(double value)
{
    InternTable& interned = internTable();
    std::shared_lock lock(interned.mutex);
    auto found = interned.strings.find(value);
    return found == interned.strings.end() ? std::string() : found->second;
}

bool isString(double value)
{
    InternTable& interned = internTable();
    std::shared_lock lock(interned.mutex);
    return interned.strings.count(value) != 0;
}

Script::Script(double value)
    : constant(value)
    , hasConstant(true)
{
}

Script Script::compile(const std::string& source, double fallback)
{
    Script script(fallback);
    Parser parser(tokenize(source));
    bool complex = false;
    NodePtr root = parser.program(complex);
    if (!parser.ok()) {
        return script;
    }
    if (root->kind == Kind::Number) {
        script.constant = root->number;
        return script;
    }
    if (root->kind == Kind::Sequence && root->children.empty()) {
        return script;
    }
    script.root = root;
    return script;
}

double Script::run(Scope& scope) const
{
    if (!root) {
        return constant;
    }
    Flow flow = Flow::Normal;
    double value = evaluate(*root, scope, flow);
    if (root->kind == Kind::Sequence && flow != Flow::Return) {
        return 0.0;
    }
    return std::isfinite(value) ? value : 0.0;
}

}
