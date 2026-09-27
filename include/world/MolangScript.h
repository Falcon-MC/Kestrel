#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::world::molang {

constexpr size_t MaxArguments = 16;

/**
 * Answers the query.* functions of one evaluation. Unknown queries return 0.
 */
class QuerySource {
public:
    virtual ~QuerySource() = default;
    virtual double query(const std::string& name, std::span<const double> arguments) = 0;
};

/**
 * The storage an expression reads and writes: variable.* lives as long as the
 * entity, temp.* only for one script, context.* is set by the caller.
 */
struct EntityContext {
    QuerySource* queries = nullptr;
    std::unordered_map<std::string, double>* variables = nullptr;
};

/**
 * arrays holds array.* lists by lowercase name; resolveEntity maps the left
 * side of `->` to another entity, and an unresolved target evaluates to 0.
 */
struct Scope {
    std::unordered_map<std::string, double>* variables = nullptr;
    std::unordered_map<std::string, double> temps;
    std::unordered_map<std::string, double> context;
    QuerySource* queries = nullptr;
    double random = 0.0;
    double thisValue = 0.0;
    std::unordered_map<std::string, std::vector<double>> arrays;
    std::function<bool(double target, EntityContext& entity)> resolveEntity;
};

struct Node;

/**
 * A compiled Molang expression or statement list. Numbers are doubles and
 * string literals compare through a stable hash, so equality tests against
 * string queries work; an empty or unparsable source evaluates to its
 * fallback.
 */
class Script {
public:
    Script() = default;
    explicit Script(double constant);

    static Script compile(const std::string& source, double fallback = 0.0);

    double run(Scope& scope) const;

    bool empty() const
    {
        return !root && !hasConstant;
    }

    bool isConstant() const
    {
        return !root;
    }

    double constantValue() const
    {
        return constant;
    }

private:
    std::shared_ptr<const Node> root;
    double constant = 0.0;
    bool hasConstant = false;
};

/**
 * Strings are doubles: stringHash is a case-insensitive FNV value (0 for the
 * empty string) and every hashed string is interned, so stringOf turns the
 * value back into the first text seen with that hash. Queries return strings
 * through internString and read string arguments through stringOf.
 */
double stringHash(const std::string& text);
double internString(const std::string& text);
std::string stringOf(double value);
bool isString(double value);

}
