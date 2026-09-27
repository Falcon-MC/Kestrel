#include "ui/JsonUi.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "util/JsonText.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>

namespace kestrel::ui {

UiValue UiValue::of(bool value)
{
    UiValue result;
    result.kind = Kind::Bool;
    result.flag = value;
    return result;
}

UiValue UiValue::of(double value)
{
    UiValue result;
    result.kind = Kind::Number;
    result.number = value;
    return result;
}

UiValue UiValue::of(std::string value)
{
    UiValue result;
    result.kind = Kind::String;
    result.text = std::move(value);
    return result;
}

bool UiValue::truthy() const
{
    switch (kind) {
    case Kind::Bool:
        return flag;
    case Kind::Number:
        return number != 0.0;
    case Kind::String:
        return !text.empty() && text != "false" && text != "0";
    case Kind::None:
        break;
    }
    return false;
}

double UiValue::toNumber() const
{
    switch (kind) {
    case Kind::Bool:
        return flag ? 1.0 : 0.0;
    case Kind::Number:
        return number;
    case Kind::String:
        return std::strtod(text.c_str(), nullptr);
    case Kind::None:
        break;
    }
    return 0.0;
}

std::string UiValue::toText() const
{
    switch (kind) {
    case Kind::Bool:
        return flag ? "true" : "false";
    case Kind::Number: {
        if (number == std::floor(number) && std::abs(number) < 1e15) {
            return std::to_string(static_cast<long long>(number));
        }
        return std::to_string(number);
    }
    case Kind::String:
        return text;
    case Kind::None:
        break;
    }
    return {};
}

namespace {

constexpr int MaxDepth = 32;
constexpr size_t MaxFactoryItems = 100;
constexpr float LabelLineHeight = 10.0f;

std::string_view controlName(std::string_view key)
{
    return key.substr(0, key.find('@'));
}

std::string_view controlBase(std::string_view key)
{
    size_t at = key.find('@');
    return at == std::string_view::npos ? std::string_view() : key.substr(at + 1);
}

/**
 * Sets key on an object, keeping its place when it already exists.
 */
void setKey(json::Value& object, const std::string& key, std::unique_ptr<json::Value> value)
{
    object.set(key, std::move(value));
}

void renameKey(json::Value& object, const std::string& from, const std::string& to)
{
    if (from == to) {
        return;
    }
    auto found = object.mObject.find(from);
    if (found == object.mObject.end()) {
        return;
    }
    std::unique_ptr<json::Value> value = std::move(found->second);
    object.mObject.erase(found);
    object.mObject[to] = std::move(value);
    std::replace(object.mKeys.begin(), object.mKeys.end(), from, to);
}

bool matchesWhere(const json::Value& item, const json::Value* where)
{
    if (!where || !where->isObject() || !item.isObject()) {
        return false;
    }
    for (const std::string& key : where->mKeys) {
        const json::Value* expected = where->get(key);
        const json::Value* actual = item.get(key);
        if (!actual || actual->mType != expected->mType || actual->mString != expected->mString || actual->mNumber != expected->mNumber || actual->mBoolean != expected->mBoolean) {
            return false;
        }
    }
    return true;
}

/**
 * The index of the array item a modification points at: a control by name
 * (controls hold one key each) or any item matching the "where" object.
 */
std::optional<size_t> findItem(const json::Value& array, const json::Value& modification)
{
    const json::Value* name = modification.get("control_name");
    const json::Value* where = modification.get("where");
    for (size_t i = 0; i < array.mArray.size(); ++i) {
        const json::Value& item = *array.mArray[i];
        if (name && item.isObject() && !item.mKeys.empty() && controlName(item.mKeys.front()) == controlName(name->string())) {
            return i;
        }
        if (where && matchesWhere(item, where)) {
            return i;
        }
    }
    return std::nullopt;
}

void applyModifications(json::Value& control, const json::Value& modifications)
{
    for (const std::unique_ptr<json::Value>& modification : modifications.mArray) {
        if (!modification->isObject()) {
            continue;
        }
        std::string arrayName = modification->get("array_name") ? modification->get("array_name")->string() : "controls";
        std::string operation = modification->get("operation") ? modification->get("operation")->string() : "";
        if (!control.get(arrayName)) {
            setKey(control, arrayName, json::Value::ofArray());
        }
        json::Value& array = *control.mObject[arrayName];
        if (!array.isArray()) {
            continue;
        }
        const json::Value* value = modification->get("value");
        std::vector<std::unique_ptr<json::Value>> items;
        if (value && value->isArray()) {
            for (const std::unique_ptr<json::Value>& item : value->mArray) {
                items.push_back(item->clone());
            }
        } else if (value) {
            items.push_back(value->clone());
        }
        auto insertAt = [&](size_t index) {
            for (std::unique_ptr<json::Value>& item : items) {
                array.mArray.insert(array.mArray.begin() + static_cast<std::ptrdiff_t>(index++), std::move(item));
            }
        };
        std::optional<size_t> target = findItem(array, *modification);
        if (operation == "insert_back") {
            insertAt(array.mArray.size());
        } else if (operation == "insert_front") {
            insertAt(0);
        } else if (operation == "insert_after" && target) {
            insertAt(*target + 1);
        } else if (operation == "insert_before" && target) {
            insertAt(*target);
        } else if (operation == "remove" && target) {
            array.mArray.erase(array.mArray.begin() + static_cast<std::ptrdiff_t>(*target));
        } else if (operation == "replace" && target) {
            array.mArray.erase(array.mArray.begin() + static_cast<std::ptrdiff_t>(*target));
            insertAt(*target);
        }
    }
}

/**
 * Evaluates the binding expressions of the JSON UI: not, and, or,
 * comparisons, arithmetic, string subtraction (removes every occurrence) and
 * '%.Ns' * text (keeps the first N characters), over #bindings, $variables,
 * 'strings' and numbers.
 */
class Expression {
public:
    using Lookup = std::function<UiValue(const std::string&)>;

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
};

enum class TermKind {
    Pixel,
    Parent,
    Children,
    ChildrenMax,
    SiblingMax,
    OwnX,
    OwnY,
    Default,
    Fill,
};

struct Term {
    TermKind kind = TermKind::Pixel;
    float amount = 0.0f;
};

using Extent = std::vector<Term>;

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

struct AxisInputs {
    float parent = 0.0f;
    float children = 0.0f;
    float childrenMax = 0.0f;
    float siblingMax = 0.0f;
    float own = 0.0f;
    float natural = 0.0f;
    float fill = 0.0f;
};

float extentSize(const Extent& extent, const AxisInputs& in)
{
    float total = 0.0f;
    for (const Term& term : extent) {
        switch (term.kind) {
        case TermKind::Pixel:
            total += term.amount;
            break;
        case TermKind::Parent:
            total += term.amount * in.parent;
            break;
        case TermKind::Children:
            total += term.amount * in.children;
            break;
        case TermKind::ChildrenMax:
            total += term.amount * in.childrenMax;
            break;
        case TermKind::SiblingMax:
            total += term.amount * in.siblingMax;
            break;
        case TermKind::OwnX:
        case TermKind::OwnY:
            total += term.amount * in.own;
            break;
        case TermKind::Default:
            total += term.amount * in.natural;
            break;
        case TermKind::Fill:
            total += term.amount * in.fill;
            break;
        }
    }
    return std::max(0.0f, total);
}

std::array<float, 2> anchorPoint(std::string_view name)
{
    if (name == "top_left") {
        return { 0.0f, 0.0f };
    }
    if (name == "top_middle") {
        return { 0.5f, 0.0f };
    }
    if (name == "top_right") {
        return { 1.0f, 0.0f };
    }
    if (name == "left_middle") {
        return { 0.0f, 0.5f };
    }
    if (name == "right_middle") {
        return { 1.0f, 0.5f };
    }
    if (name == "bottom_left") {
        return { 0.0f, 1.0f };
    }
    if (name == "bottom_middle") {
        return { 0.5f, 1.0f };
    }
    if (name == "bottom_right") {
        return { 1.0f, 1.0f };
    }
    return { 0.5f, 0.5f };
}

using Properties = std::unordered_map<std::string, const json::Value*>;

struct Node {
    std::string name;
    Properties props;
    Properties vars;
    UiRow bound;
    const UiRow* row = nullptr;
    std::vector<Node> children;
    std::string type;
    bool visible = true;
    bool stack = false;
    bool vertical = true;
    Extent width;
    Extent height;
    float w = 0.0f;
    float h = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float alpha = 1.0f;
    std::string text;
};

}

/**
 * Builds the control tree for one draw: resolves inheritance, variables,
 * factories and bindings, then lays the tree out and draws it by layer.
 */
struct JsonUiLayout {
    const JsonUi& owner;
    Context& ui;
    const UiData& data;

    std::pair<std::string, const json::Value*> find(std::string_view space, std::string_view name) const
    {
        for (const auto& [path, file] : owner.files) {
            const json::Value* ns = file->get("namespace");
            if (!ns || ns->string() != space) {
                continue;
            }
            for (const std::string& key : file->mKeys) {
                if (controlName(key) == name) {
                    return { key, file->get(key) };
                }
            }
        }
        return { {}, nullptr };
    }

    const json::Value* globalVariable(const std::string& name) const
    {
        for (const auto& [path, file] : owner.files) {
            if (path.find("_global_variables") != std::string::npos) {
                if (const json::Value* value = file->get(name)) {
                    return value;
                }
            }
        }
        return nullptr;
    }

    /**
     * Gathers the properties of namespace.control, those of the controls it
     * derives from first.
     */
    void collect(std::string_view reference, std::string_view space, Properties& out, int depth) const
    {
        if (depth > MaxDepth) {
            return;
        }
        size_t dot = reference.find('.');
        std::string_view ns = dot == std::string_view::npos ? space : reference.substr(0, dot);
        std::string_view name = dot == std::string_view::npos ? reference : reference.substr(dot + 1);
        auto [key, control] = find(ns, name);
        if (!control || !control->isObject()) {
            return;
        }
        if (std::string_view base = controlBase(key); !base.empty()) {
            collect(base, ns, out, depth + 1);
        }
        for (const std::string& property : control->mKeys) {
            out[property] = control->get(property);
        }
    }

    const json::Value* resolve(const Node& node, const json::Value* value, int depth = 0) const
    {
        while (value && value->isString() && !value->mString.empty() && value->mString.front() == '$' && depth++ < MaxDepth) {
            auto found = node.vars.find(value->mString);
            value = found != node.vars.end() ? found->second : globalVariable(value->mString);
        }
        return value;
    }

    const json::Value* property(const Node& node, const std::string& name) const
    {
        auto found = node.props.find(name);
        return found == node.props.end() ? nullptr : resolve(node, found->second);
    }

    static UiValue toValue(const json::Value* value)
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

    UiValue lookup(const Node& node, const std::string& name) const
    {
        if (!name.empty() && name.front() == '$') {
            auto found = node.vars.find(name);
            return toValue(resolve(node, found != node.vars.end() ? found->second : globalVariable(name)));
        }
        if (auto found = node.bound.find(name); found != node.bound.end()) {
            return found->second;
        }
        if (node.row) {
            if (auto found = node.row->find(name); found != node.row->end()) {
                return found->second;
            }
        }
        if (auto found = data.globals.find(name); found != data.globals.end()) {
            return found->second;
        }
        return {};
    }

    UiValue evaluate(const Node& node, std::string_view source) const
    {
        Expression::Lookup find = [&](const std::string& name) { return lookup(node, name); };
        Expression expression(source, find);
        return expression.evaluate();
    }

    void applyVariables(Node& node) const
    {
        std::vector<std::pair<std::string, const json::Value*>> defaults;
        for (const auto& [key, value] : node.props) {
            if (key.empty() || key.front() != '$') {
                continue;
            }
            size_t bar = key.find('|');
            if (bar == std::string::npos) {
                node.vars[key] = value;
            } else {
                defaults.emplace_back(key.substr(0, bar), value);
            }
        }
        for (auto& [key, value] : defaults) {
            node.vars.try_emplace(key, value);
        }
        const json::Value* variables = node.props.count("variables") ? node.props.at("variables") : nullptr;
        if (!variables || !variables->isArray()) {
            return;
        }
        for (const std::unique_ptr<json::Value>& entry : variables->mArray) {
            const json::Value* condition = entry->get("requires");
            if (!condition || !evaluate(node, condition->string()).truthy()) {
                continue;
            }
            for (const std::string& key : entry->mKeys) {
                if (!key.empty() && key.front() == '$') {
                    node.vars[key] = entry->get(key);
                }
            }
        }
    }

    void applyBindings(Node& node) const
    {
        const json::Value* bindings = property(node, "bindings");
        if (!bindings || !bindings->isArray()) {
            return;
        }
        for (const std::unique_ptr<json::Value>& binding : bindings->mArray) {
            if (!binding->isObject()) {
                continue;
            }
            const json::Value* typeValue = resolve(node, binding->get("binding_type"));
            std::string type = typeValue ? typeValue->string() : "global";
            if (type == "view") {
                const json::Value* source = binding->get("source_property_name");
                const json::Value* target = binding->get("target_property_name");
                if (source && target) {
                    node.bound[target->string()] = evaluate(node, source->string());
                }
                continue;
            }
            if (type == "collection_details") {
                continue;
            }
            const json::Value* nameValue = resolve(node, binding->get("binding_name"));
            if (!nameValue || !nameValue->isString()) {
                continue;
            }
            std::string name = nameValue->string();
            const json::Value* overrideValue = resolve(node, binding->get("binding_name_override"));
            std::string target = overrideValue && overrideValue->isString() ? overrideValue->string() : name;
            UiValue value = !name.empty() && name.front() == '(' ? evaluate(node, name) : lookup(node, name);
            node.bound[target] = std::move(value);
        }
    }

    UiValue valueOf(const Node& node, const std::string& key) const
    {
        const json::Value* value = property(node, key);
        if (value && value->isString() && !value->mString.empty() && value->mString.front() == '#') {
            return lookup(node, value->mString);
        }
        return toValue(value);
    }

    /**
     * One control and its subtree, or nothing when it is ignored.
     */
    std::optional<Node> build(Properties props, const Properties& inherited, const UiRow* row, const std::string& name, int depth) const
    {
        Node node;
        node.name = name;
        node.props = std::move(props);
        node.vars = inherited;
        node.row = row;
        applyVariables(node);
        if (const json::Value* ignored = property(node, "ignored"); ignored && ignored->boolean(false)) {
            return std::nullopt;
        }
        if (const json::Value* ignored = node.props.count("ignored") ? node.props.at("ignored") : nullptr; ignored && ignored->isString() && evaluate(node, ignored->string()).truthy()) {
            return std::nullopt;
        }
        applyBindings(node);

        const json::Value* type = property(node, "type");
        node.type = type ? type->string() : "panel";
        node.stack = node.type == "stack_panel";
        const json::Value* orientation = property(node, "orientation");
        node.vertical = !orientation || orientation->string() != "horizontal";
        if (auto visible = node.bound.find("#visible"); visible != node.bound.end()) {
            node.visible = visible->second.truthy();
        } else if (const json::Value* visible = property(node, "visible")) {
            node.visible = visible->isString() ? lookup(node, visible->mString).truthy() : visible->boolean(true);
        }
        if (auto alpha = node.bound.find("#alpha"); alpha != node.bound.end()) {
            node.alpha = static_cast<float>(alpha->second.toNumber());
        } else if (node.props.count("alpha")) {
            node.alpha = static_cast<float>(valueOf(node, "alpha").toNumber());
        }
        if (node.type == "label") {
            UiValue text = valueOf(node, "text");
            node.text = text.toText();
            const json::Value* localize = property(node, "localize");
            if ((!localize || localize->boolean(true)) && !node.text.empty()) {
                node.text = tr(node.text, node.text);
            }
        }
        TermKind fallback = node.type == "label" ? TermKind::Default : TermKind::Parent;
        const json::Value* size = property(node, "size");
        node.width = parseExtent(size && size->isArray() && size->mArray.size() == 2 ? resolve(node, size->mArray[0].get()) : nullptr, fallback);
        node.height = parseExtent(size && size->isArray() && size->mArray.size() == 2 ? resolve(node, size->mArray[1].get()) : nullptr, fallback);

        if (depth < MaxDepth) {
            addChildren(node, depth);
        }
        return node;
    }

    void addChildren(Node& node, int depth) const
    {
        std::string space = currentNamespace;
        if (const json::Value* controls = property(node, "controls"); controls && controls->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : controls->mArray) {
                if (!entry->isObject() || entry->mKeys.empty()) {
                    continue;
                }
                const std::string& key = entry->mKeys.front();
                Properties props;
                if (std::string_view base = controlBase(key); !base.empty()) {
                    collect(base, space, props, 0);
                }
                if (const json::Value* own = entry->get(key); own && own->isObject()) {
                    for (const std::string& property : own->mKeys) {
                        props[property] = own->get(property);
                    }
                }
                if (std::optional<Node> child = build(std::move(props), node.vars, node.row, std::string(controlName(key)), depth + 1)) {
                    node.children.push_back(std::move(*child));
                }
            }
        }

        const json::Value* factory = property(node, "factory");
        if (!factory || !factory->isObject()) {
            return;
        }
        std::string control;
        if (const json::Value* name = factory->get("control_name")) {
            control = name->string();
        } else if (const json::Value* ids = factory->get("control_ids"); ids && ids->isObject() && !ids->mKeys.empty()) {
            control = ids->get(ids->mKeys.front())->string();
            control = control.substr(control.find('@') == std::string::npos ? 0 : control.find('@') + 1);
        }
        if (!control.empty() && control.front() == '@') {
            control.erase(0, 1);
        }
        const json::Value* collectionName = property(node, "collection_name");
        const std::vector<UiRow>* rows = nullptr;
        if (collectionName) {
            if (auto found = data.collections.find(collectionName->string()); found != data.collections.end()) {
                rows = &found->second;
            }
        }
        size_t count = rows ? rows->size() : 0;
        if (auto length = node.bound.find("#collection_length"); length != node.bound.end()) {
            count = static_cast<size_t>(std::max(0.0, length->second.toNumber()));
        }
        count = std::min(count, MaxFactoryItems);
        for (size_t i = 0; i < count; ++i) {
            Properties props;
            collect(control, space, props, 0);
            const UiRow* row = rows && i < rows->size() ? &(*rows)[i] : nullptr;
            if (std::optional<Node> child = build(std::move(props), node.vars, row, control, depth + 1)) {
                node.children.push_back(std::move(*child));
            }
        }
    }

    AxisInputs children(const Node& node, bool horizontal) const
    {
        AxisInputs in;
        for (const Node& child : node.children) {
            if (!child.visible) {
                continue;
            }
            float size = horizontal ? child.w : child.h;
            in.childrenMax = std::max(in.childrenMax, size);
            // Panels add their children up like stacks do, which is what the sidebar relies on
            // for its title and lists; a stack only takes the widest across its orientation.
            if (node.stack && node.vertical == horizontal) {
                in.children = std::max(in.children, size);
            } else {
                in.children += size;
            }
        }
        return in;
    }

    float natural(const Node& node, bool horizontal) const
    {
        if (node.type != "label") {
            return 0.0f;
        }
        float scale = static_cast<float>(valueOf(node, "font_scale_factor").kind == UiValue::Kind::None ? 1.0 : valueOf(node, "font_scale_factor").toNumber());
        size_t lines = 1;
        float widest = 0.0f;
        size_t start = 0;
        while (true) {
            size_t end = node.text.find('\n', start);
            widest = std::max(widest, ui.measure(std::string_view(node.text).substr(start, end == std::string::npos ? std::string::npos : end - start), TextStyle::Pixel));
            if (end == std::string::npos) {
                break;
            }
            ++lines;
            start = end + 1;
        }
        return horizontal ? widest * scale : static_cast<float>(lines) * LabelLineHeight * scale;
    }

    void measure(Node& node, float parentW, float parentH) const
    {
        AxisInputs w;
        w.parent = parentW;
        w.natural = natural(node, true);
        w.fill = parentW;
        AxisInputs h;
        h.parent = parentH;
        h.natural = natural(node, false);
        h.fill = parentH;
        bool widthFromChildren = uses(node.width, TermKind::Children) || uses(node.width, TermKind::ChildrenMax);
        bool heightFromChildren = uses(node.height, TermKind::Children) || uses(node.height, TermKind::ChildrenMax);

        node.w = widthFromChildren ? parentW : extentSize(node.width, w);
        node.h = heightFromChildren ? parentH : extentSize(node.height, h);
        if (uses(node.width, TermKind::OwnY)) {
            w.own = node.h;
            node.w = extentSize(node.width, w);
        }
        if (uses(node.height, TermKind::OwnX)) {
            h.own = node.w;
            node.h = extentSize(node.height, h);
        }
        measureChildren(node);
        if (widthFromChildren || heightFromChildren) {
            AxisInputs across = children(node, true);
            AxisInputs down = children(node, false);
            if (widthFromChildren) {
                w.children = across.children;
                w.childrenMax = across.childrenMax;
                node.w = extentSize(node.width, w);
            }
            if (heightFromChildren) {
                h.children = down.children;
                h.childrenMax = down.childrenMax;
                node.h = extentSize(node.height, h);
            }
            measureChildren(node);
        }
        clamp(node, "max_size", parentW, parentH, true);
        clamp(node, "min_size", parentW, parentH, false);
    }

    void clamp(Node& node, const char* key, float parentW, float parentH, bool upper) const
    {
        const json::Value* limit = property(node, key);
        if (!limit || !limit->isArray() || limit->mArray.size() != 2) {
            return;
        }
        AxisInputs w;
        w.parent = parentW;
        w.natural = node.w;
        AxisInputs h;
        h.parent = parentH;
        h.natural = node.h;
        float lw = extentSize(parseExtent(resolve(node, limit->mArray[0].get()), TermKind::Parent), w);
        float lh = extentSize(parseExtent(resolve(node, limit->mArray[1].get()), TermKind::Parent), h);
        node.w = upper ? std::min(node.w, lw) : std::max(node.w, lw);
        node.h = upper ? std::min(node.h, lh) : std::max(node.h, lh);
    }

    void measureChildren(Node& node) const
    {
        for (Node& child : node.children) {
            if (child.visible) {
                measure(child, node.w, node.h);
            }
        }
        if (node.stack) {
            float used = 0.0f;
            size_t fills = 0;
            for (const Node& child : node.children) {
                if (!child.visible) {
                    continue;
                }
                const Extent& along = node.vertical ? child.height : child.width;
                if (uses(along, TermKind::Fill)) {
                    ++fills;
                } else {
                    used += node.vertical ? child.h : child.w;
                }
            }
            if (fills > 0) {
                float share = std::max(0.0f, (node.vertical ? node.h : node.w) - used) / static_cast<float>(fills);
                for (Node& child : node.children) {
                    const Extent& along = node.vertical ? child.height : child.width;
                    if (child.visible && uses(along, TermKind::Fill)) {
                        (node.vertical ? child.h : child.w) = share;
                        measureChildren(child);
                    }
                }
            }
        }
        for (Node& child : node.children) {
            if (!child.visible) {
                continue;
            }
            bool siblingW = uses(child.width, TermKind::SiblingMax);
            bool siblingH = uses(child.height, TermKind::SiblingMax);
            const json::Value* inherit = property(child, "inherit_max_sibling_width");
            bool inheritWidth = inherit && inherit->boolean(false);
            if (!siblingW && !siblingH && !inheritWidth) {
                continue;
            }
            float maxW = 0.0f;
            float maxH = 0.0f;
            for (const Node& other : node.children) {
                if (&other != &child && other.visible) {
                    maxW = std::max(maxW, other.w);
                    maxH = std::max(maxH, other.h);
                }
            }
            if (siblingW) {
                AxisInputs in;
                in.parent = node.w;
                in.siblingMax = maxW;
                child.w = extentSize(child.width, in);
            }
            if (siblingH) {
                AxisInputs in;
                in.parent = node.h;
                in.siblingMax = maxH;
                child.h = extentSize(child.height, in);
            }
            if (inheritWidth) {
                child.w = std::max(child.w, maxW);
            }
            measureChildren(child);
        }
    }

    float signedOffset(const Node& child, const Node& parent, size_t axis) const
    {
        const json::Value* value = property(child, "offset");
        if (!value || !value->isArray() || value->mArray.size() != 2) {
            return 0.0f;
        }
        const json::Value* part = resolve(child, value->mArray[axis].get());
        if (!part) {
            return 0.0f;
        }
        if (part->isNumber()) {
            return static_cast<float>(part->number());
        }
        AxisInputs in;
        in.parent = axis == 0 ? parent.w : parent.h;
        Extent extent = parseExtent(part, TermKind::Pixel);
        float total = 0.0f;
        for (const Term& term : extent) {
            total += term.kind == TermKind::Parent ? term.amount * in.parent : term.kind == TermKind::Pixel ? term.amount : 0.0f;
        }
        return total;
    }

    void place(Node& node, float x, float y, float z) const
    {
        node.x = x;
        node.y = y;
        node.z = z;
        float cursor = 0.0f;
        const json::Value* childAnchors = property(node, "use_child_anchors");
        bool anchored = !node.stack || (childAnchors && childAnchors->boolean(false));
        for (Node& child : node.children) {
            if (!child.visible) {
                continue;
            }
            const json::Value* from = property(child, "anchor_from");
            const json::Value* to = property(child, "anchor_to");
            std::array<float, 2> parentAnchor { 0.0f, 0.0f };
            std::array<float, 2> childAnchor { 0.0f, 0.0f };
            if (anchored) {
                parentAnchor = anchorPoint(from ? from->string() : "center");
                childAnchor = anchorPoint(to ? to->string() : "center");
            }
            float cx = x + parentAnchor[0] * node.w - childAnchor[0] * child.w + signedOffset(child, node, 0);
            float cy = y + parentAnchor[1] * node.h - childAnchor[1] * child.h + signedOffset(child, node, 1);
            if (node.stack) {
                if (node.vertical) {
                    cy = y + cursor + signedOffset(child, node, 1);
                    cursor += child.h;
                } else {
                    cx = x + cursor + signedOffset(child, node, 0);
                    cursor += child.w;
                }
            }
            const json::Value* layer = property(child, "layer");
            place(child, cx, cy, z + static_cast<float>(layer ? layer->number() : 0.0));
        }
    }

    struct Paint {
        const Node* node = nullptr;
        float alpha = 1.0f;
        size_t order = 0;
    };

    void gather(const Node& node, float inherited, std::vector<Paint>& out) const
    {
        if (!node.visible) {
            return;
        }
        out.push_back({ &node, inherited, out.size() });
        const json::Value* propagate = property(node, "propagate_alpha");
        float passed = propagate && propagate->boolean(false) ? inherited * node.alpha : inherited;
        for (const Node& child : node.children) {
            gather(child, passed, out);
        }
    }

    Color color(const Node& node, float alpha) const
    {
        const json::Value* value = property(node, "color");
        float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (value && value->isArray()) {
            for (size_t i = 0; i < std::min<size_t>(4, value->mArray.size()); ++i) {
                rgba[i] = static_cast<float>(value->mArray[i]->number(1.0));
            }
        }
        auto channel = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return { channel(rgba[0]), channel(rgba[1]), channel(rgba[2]), channel(rgba[3] * alpha) };
    }

    void paint(const Paint& item) const
    {
        const Node& node = *item.node;
        Rect rect { node.x, node.y, node.w, node.h };
        float alpha = std::clamp(node.alpha * item.alpha, 0.0f, 1.0f);
        if (alpha <= 0.0f) {
            return;
        }
        if (node.type == "image") {
            std::string texture;
            if (auto bound = node.bound.find("#texture"); bound != node.bound.end()) {
                texture = bound->second.toText();
            } else {
                texture = valueOf(node, "texture").toText();
            }
            if (texture.empty() || rect.w <= 0.0f || rect.h <= 0.0f) {
                return;
            }
            Color tint = color(node, alpha);
            const Sprite& sprite = ui.skin().sprite(texture);
            if (sprite.slice.left > 0.0f || sprite.slice.top > 0.0f || sprite.slice.right > 0.0f || sprite.slice.bottom > 0.0f) {
                ui.nineSlice(rect, texture, tint);
            } else {
                ui.sprite(rect, texture, tint);
            }
            return;
        }
        if (node.type != "label" || node.text.empty()) {
            return;
        }
        UiValue factor = valueOf(node, "font_scale_factor");
        float scale = factor.kind == UiValue::Kind::None ? 1.0f : static_cast<float>(factor.toNumber());
        Color ink = color(node, alpha);
        const json::Value* shadow = property(node, "shadow");
        const json::Value* alignment = property(node, "text_alignment");
        std::string align = alignment ? alignment->string() : "left";
        float lineY = rect.y;
        size_t start = 0;
        while (true) {
            size_t end = node.text.find('\n', start);
            std::string_view line = std::string_view(node.text).substr(start, end == std::string::npos ? std::string::npos : end - start);
            float width = ui.measure(line, TextStyle::Pixel) * scale;
            float lineX = align == "center" ? rect.x + (rect.w - width) * 0.5f : align == "right" ? rect.right() - width : rect.x;
            if (shadow && shadow->boolean(false)) {
                ui.pixelTextScaled(line, lineX + scale, lineY + scale, scale, ink, true);
            }
            ui.pixelTextScaled(line, lineX, lineY, scale, ink);
            if (end == std::string::npos) {
                break;
            }
            lineY += LabelLineHeight * scale;
            start = end + 1;
        }
    }

    void draw(std::string_view reference, const Rect& area)
    {
        size_t dot = reference.find('.');
        currentNamespace = std::string(reference.substr(0, dot));
        Properties props;
        collect(reference, currentNamespace, props, 0);
        if (props.empty()) {
            return;
        }
        std::optional<Node> root = build(std::move(props), {}, nullptr, std::string(reference), 0);
        if (!root || !root->visible) {
            return;
        }
        measure(*root, area.w, area.h);
        const json::Value* from = property(*root, "anchor_from");
        const json::Value* to = property(*root, "anchor_to");
        std::array<float, 2> parentAnchor = anchorPoint(from ? from->string() : "center");
        std::array<float, 2> childAnchor = anchorPoint(to ? to->string() : "center");
        Node frame;
        frame.w = area.w;
        frame.h = area.h;
        place(*root, area.x + parentAnchor[0] * area.w - childAnchor[0] * root->w + signedOffset(*root, frame, 0), area.y + parentAnchor[1] * area.h - childAnchor[1] * root->h + signedOffset(*root, frame, 1), 0.0f);
        std::vector<Paint> order;
        gather(*root, 1.0f, order);
        std::stable_sort(order.begin(), order.end(), [](const Paint& a, const Paint& b) { return a.node->z < b.node->z; });
        for (const Paint& item : order) {
            paint(item);
        }
    }

    std::string currentNamespace;
};

void JsonUi::clear()
{
    files.clear();
}

void JsonUi::addFile(const std::string& path, const std::string& text)
{
    std::unique_ptr<json::Value> incoming = util::parseJsonObject(text);
    if (!incoming) {
        return;
    }
    auto existing = files.find(path);
    if (existing == files.end()) {
        for (const std::string& key : incoming->mKeys) {
            json::Value* control = incoming->mObject[key].get();
            if (control && control->isObject() && control->get("modifications")) {
                control->mObject.erase("modifications");
                control->mKeys.erase(std::remove(control->mKeys.begin(), control->mKeys.end(), "modifications"), control->mKeys.end());
            }
        }
        files.emplace(path, std::move(incoming));
        return;
    }
    json::Value& base = *existing->second;
    for (const std::string& key : incoming->mKeys) {
        std::unique_ptr<json::Value>& value = incoming->mObject[key];
        if (key == "namespace") {
            if (!base.get("namespace")) {
                setKey(base, key, std::move(value));
            }
            continue;
        }
        std::string match;
        for (const std::string& known : base.mKeys) {
            if (controlName(known) == controlName(key)) {
                match = known;
                break;
            }
        }
        if (match.empty() || !value->isObject() || !base.get(match)->isObject()) {
            setKey(base, key, std::move(value));
            continue;
        }
        if (key.find('@') != std::string::npos) {
            renameKey(base, match, key);
            match = key;
        }
        json::Value& target = *base.mObject[match];
        for (const std::string& property : value->mKeys) {
            if (property == "modifications") {
                applyModifications(target, *value->get(property));
            } else {
                setKey(target, property, value->get(property)->clone());
            }
        }
    }
}

std::vector<std::string> JsonUi::texturePaths() const
{
    std::vector<std::string> paths;
    std::function<void(const json::Value&)> walk = [&](const json::Value& value) {
        if (value.isObject()) {
            for (const std::string& key : value.mKeys) {
                const json::Value* child = value.get(key);
                if (key == "texture" && child->isString() && !child->mString.empty() && child->mString.front() != '$' && child->mString.front() != '#') {
                    paths.push_back(child->mString);
                } else if (key.find("texture") != std::string::npos && key.front() == '$' && child->isString()) {
                    paths.push_back(child->mString);
                }
                walk(*child);
            }
        } else if (value.isArray()) {
            for (const std::unique_ptr<json::Value>& item : value.mArray) {
                walk(*item);
            }
        }
    };
    for (const auto& [path, file] : files) {
        walk(*file);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

void JsonUi::draw(Context& ui, std::string_view control, const Rect& area, const UiData& data) const
{
    JsonUiLayout layout { *this, ui, data, {} };
    layout.draw(control, area);
}

}
