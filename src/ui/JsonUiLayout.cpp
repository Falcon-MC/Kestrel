#include "ui/JsonUiInternal.h"

#include "ui/Context.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace kestrel::ui {

using namespace jsonui;

namespace {

constexpr float Unknown = std::numeric_limits<float>::quiet_NaN();

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

bool alongAxis(const Node& node, int axis)
{
    if (node.type != "stack_panel") {
        return false;
    }
    return (axis == 1) == node.vertical;
}

Rect intersect(const Rect& a, const Rect& b)
{
    float x = std::max(a.x, b.x);
    float y = std::max(a.y, b.y);
    float right = std::min(a.right(), b.right());
    float bottom = std::min(a.bottom(), b.bottom());
    return { x, y, std::max(0.0f, right - x), std::max(0.0f, bottom - y) };
}

constexpr float SinScale = 10430.378f;
constexpr float SinQuarter = 16384.0f;
constexpr float Tau = 6.2831855f;
constexpr float Pi = 3.14159265f;
constexpr float HalfPi = 1.57079633f;
constexpr float Back = 1.70158f;
constexpr float BackPlusOne = 2.70158f;
constexpr float BackInOut = 2.5949094f;
constexpr float BackInOutPlusOne = 3.5949094f;

/**
 * The game's sine, read from its 65536 entry table by a pre-scaled angle.
 */
float sinAt(float scaled)
{
    static const std::vector<float> table = [] {
        std::vector<float> values(65536);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<float>(std::sin(static_cast<double>(i) * 3.14159265358979323846 * 2.0 / 65536.0));
        }
        return values;
    }();
    return table[static_cast<size_t>(static_cast<int32_t>(scaled) & 0xffff)];
}

float bounce(float x)
{
    constexpr float K = 7.5625f;
    if (x < 0.36363637f) {
        return K * x * x;
    }
    if (x < 0.72727275f) {
        x += -0.54545456f;
        return K * x * x + 0.75f;
    }
    if (x < 0.90909094f) {
        x += -0.8181818f;
        return K * x * x + 0.9375f;
    }
    x += -0.95454544f;
    return K * x * x + 0.984375f;
}

float elasticSin(float x)
{
    return sinAt((((x + -0.075f) * Tau) / 0.3f) * SinScale);
}

float inOutPower(float t, int power)
{
    float x = t + t;
    auto raise = [&](float value) {
        float product = 1.0f;
        for (int i = 0; i < power; ++i) {
            product *= value;
        }
        return product;
    };
    return x < 1.0f ? 0.5f * raise(x) : 0.5f * (raise(x + -2.0f) + 2.0f);
}

/**
 * How far along an animation is after easing, by the 32 curves the game
 * names, worked in single precision with its own sine table; an unknown
 * name is linear.
 */
float easing(std::string_view name, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    std::string lower(name);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "spring") {
        float phase = (2.5f * t * t * t + 0.2f) * Pi * t * SinScale;
        float rest = 1.0f - t;
        return (std::pow(rest, 2.2f) * sinAt(phase) + t) * (rest * 1.2f + 1.0f);
    }
    if (lower == "in_quad") {
        return t * t;
    }
    if (lower == "out_quad") {
        return -(t + -2.0f) * t;
    }
    if (lower == "in_out_quad") {
        float x = t + t;
        return x < 1.0f ? 0.5f * x * x : -0.5f * ((-2.0f + x + -1.0f) * (x + -1.0f) + -1.0f);
    }
    if (lower == "in_cubic") {
        return t * t * t;
    }
    if (lower == "out_cubic") {
        float x = t + -1.0f;
        return x * x * x + 1.0f;
    }
    if (lower == "in_out_cubic") {
        return inOutPower(t, 3);
    }
    if (lower == "in_quart") {
        return t * t * t * t;
    }
    if (lower == "out_quart") {
        float x = t + -1.0f;
        return -(x * x * x * x + -1.0f);
    }
    if (lower == "in_out_quart") {
        float x = t + t;
        if (x < 1.0f) {
            return 0.5f * x * x * x * x;
        }
        x += -2.0f;
        return -0.5f * (x * x * x * x + -2.0f);
    }
    if (lower == "in_quint") {
        return t * t * t * t * t;
    }
    if (lower == "out_quint") {
        float x = t + -1.0f;
        return x * x * x * x * x + 1.0f;
    }
    if (lower == "in_out_quint") {
        return inOutPower(t, 5);
    }
    if (lower == "in_sine") {
        return 1.0f - sinAt(t * HalfPi * SinScale + SinQuarter);
    }
    if (lower == "out_sine") {
        return sinAt(t * HalfPi * SinScale);
    }
    if (lower == "in_out_sine") {
        return (sinAt(t * Pi * SinScale + SinQuarter) + -1.0f) * -0.5f;
    }
    if (lower == "in_expo") {
        return std::exp2((t + -1.0f) * 10.0f);
    }
    if (lower == "out_expo") {
        return 1.0f - std::exp2(t * -10.0f);
    }
    if (lower == "in_out_expo") {
        float x = t + t;
        float curve = 1.0f <= x ? 2.0f - std::exp2((x + -1.0f) * -10.0f) : std::exp2((x + -1.0f) * 10.0f);
        return curve * 0.5f;
    }
    if (lower == "in_circ") {
        return -(std::sqrt(1.0f - t * t) + -1.0f);
    }
    if (lower == "out_circ") {
        return std::sqrt(1.0f - (t + -1.0f) * (t + -1.0f));
    }
    if (lower == "in_out_circ") {
        float x = t + t;
        if (1.0f <= x) {
            return (std::sqrt(1.0f - (x + -2.0f) * (x + -2.0f)) + 1.0f) * 0.5f;
        }
        return (std::sqrt(1.0f - x * x) + -1.0f) * -0.5f;
    }
    if (lower == "in_bounce") {
        return 1.0f - bounce(1.0f - t);
    }
    if (lower == "out_bounce") {
        return bounce(t);
    }
    if (lower == "in_out_bounce") {
        return t < 0.5f ? (1.0f - bounce(1.0f - (t + t))) * 0.5f : 0.5f + bounce(t + t + -1.0f) * 0.5f;
    }
    if (lower == "in_back") {
        return (t * BackPlusOne + -Back) * t * t;
    }
    if (lower == "out_back") {
        float x = t + -1.0f;
        return x * x * (x * BackPlusOne + Back) + 1.0f;
    }
    if (lower == "in_out_back") {
        float x = t + t;
        float curve;
        if (1.0f <= x) {
            x += -2.0f;
            curve = (x * BackInOutPlusOne + BackInOut) * x * x + 2.0f;
        } else {
            curve = (x * BackInOutPlusOne + -BackInOut) * x * x;
        }
        return curve * 0.5f;
    }
    if (lower == "in_elastic") {
        if (t == 0.0f || t == 1.0f) {
            return t;
        }
        float x = t + -1.0f;
        return -std::exp2(10.0f * x) * elasticSin(x);
    }
    if (lower == "out_elastic") {
        if (t == 0.0f || t == 1.0f) {
            return t;
        }
        return std::exp2(-10.0f * t) * elasticSin(t) + 1.0f;
    }
    if (lower == "in_out_elastic") {
        if (t == 0.0f) {
            return 0.0f;
        }
        float x = t + t;
        if (x == 2.0f) {
            return 1.0f;
        }
        x += -1.0f;
        float wave = elasticSin(x);
        if (1.0f <= x + 1.0f) {
            return std::exp2(x * -10.0f) * wave * 0.5f + 1.0f;
        }
        return std::exp2(x * 10.0f) * wave * -0.5f;
    }
    return t;
}

}

/**
 * The value an animation gives target right now, as a point between its
 * from and to: nothing when no anim drives it or the one that does waits for
 * its play_event.
 */
std::optional<float> JsonUiRuntime::animated(const Node& node, const std::string& target, int axis, float parent) const
{
    // A playing anim wins, then the value a finished chain holds, then a chain waiting for
    // its event, which holds its "from" until it first plays.
    const AnimTrack* chosen = nullptr;
    int rank = -1;
    for (const AnimTrack& track : node.anims) {
        if (track.target != target) {
            continue;
        }
        int trackRank = track.start >= 0.0 && !track.finished ? 2 : track.holding ? 1 : 0;
        if (trackRank > rank || (trackRank == rank && track.start >= chosen->start)) {
            chosen = &track;
            rank = trackRank;
        }
    }
    if (!chosen) {
        return std::nullopt;
    }
    const AnimTrack& track = *chosen;
    Node probe;
    probe.vars = node.vars;
    probe.props = track.props;
    auto pick = [&](const json::Value* value) -> float {
        value = resolve(probe, value);
        if (value && value->isArray() && value->mArray.size() > static_cast<size_t>(axis)) {
            value = resolve(probe, value->mArray[static_cast<size_t>(axis)].get());
        }
        return value ? term(node, value, parent) : 0.0f;
    };
    auto from = [&]() {
        return pick(probe.props.count("from") ? probe.props.at("from").value : nullptr);
    };
    std::string type = text(probe, "anim_type");
    if (track.start < 0.0 || track.finished || type == "wait") {
        if (track.holding) {
            return pick(track.held);
        }
        if (type == "wait") {
            return std::nullopt;
        }
        return from();
    }
    double duration = std::max(0.0, number(probe, "duration", 1.0));
    float t = duration > 0.0 ? static_cast<float>((now - track.start) / duration) : 1.0f;
    t = easing(text(probe, "easing"), t);
    float to = pick(probe.props.count("to") ? probe.props.at("to").value : nullptr);
    float start = from();
    return start + (to - start) * t;
}

/**
 * A single offset or animated value in pixels, percentages taken of parent.
 */
float JsonUiRuntime::term(const Node& node, const json::Value* value, float parent) const
{
    value = resolve(node, value);
    if (!value) {
        return 0.0f;
    }
    if (value->isNumber()) {
        return static_cast<float>(value->mNumber);
    }
    if (value->mType == json::Value::Type::Boolean) {
        return value->mBoolean ? 1.0f : 0.0f;
    }
    if (value->isString() && !value->mString.empty() && value->mString.front() == '#') {
        return static_cast<float>(lookup(node, value->mString).toNumber());
    }
    float total = 0.0f;
    for (const Term& part : parseExtent(value, TermKind::Pixel)) {
        switch (part.kind) {
        case TermKind::Pixel:
            total += part.amount;
            break;
        case TermKind::Parent:
        case TermKind::Fill:
            total += part.amount * parent;
            break;
        case TermKind::OwnX:
            total += part.amount * node.w;
            break;
        case TermKind::OwnY:
            total += part.amount * node.h;
            break;
        default:
            break;
        }
    }
    return total;
}

/**
 * How many units one font pixel of a label takes: its font_size times its
 * font_scale_factor. The game keeps the font_size steps in code; large at one
 * and a half matches it on screen, and small is the same step down.
 */
float JsonUiRuntime::labelScale(const Node& node) const
{
    std::string size = text(node, "font_size");
    float step = size == "small" ? 2.0f / 3.0f : size == "large" ? 1.5f : size == "extra_large" ? 2.0f : 1.0f;
    return step * static_cast<float>(number(node, "font_scale_factor", 1.0));
}

/**
 * The face a label draws with: the classic bitmap font, Minecraft Ten for
 * font_type MinecraftTen or Noto Sans for font_type smooth. The type can be a
 * binding, like the chat font the settings pick.
 */
TextStyle JsonUiRuntime::labelStyle(const Node& node) const
{
    std::string type = text(node, "font_type");
    if (!type.empty() && (type.front() == '#' || type.front() == '(')) {
        type = valueOf(node, "font_type").toText();
    }
    if (type.rfind("MinecraftTen", 0) == 0) {
        return TextStyle::TenLabel;
    }
    if (type == "rune") {
        return TextStyle::Rune;
    }
    return type == "smooth" ? TextStyle::SmoothLabel : TextStyle::Pixel;
}

/**
 * How many columns and rows a grid lays its items out in: with
 * grid_rescaling_type as many as fit across (or down) its size and as many
 * rows (or columns) as its maximum_grid_items need, with grid_fill_direction
 * one row (or column) of as many as fit, else the ones it is given.
 */
std::array<int, 2> JsonUiRuntime::gridCells(Node& node)
{
    int items = 0;
    Node* cell = nullptr;
    for (std::unique_ptr<Node>& child : node.children) {
        if (child->shown) {
            cell = cell ? cell : child.get();
            ++items;
        }
    }
    std::string rescaling = text(node, "grid_rescaling_type");
    std::string filling = text(node, "grid_fill_direction");
    bool rescales = rescaling == "horizontal" || rescaling == "vertical";
    bool fills = filling == "horizontal" || filling == "vertical";
    items = rescales ? int(node.gridItemCount) : std::max(items, int(node.gridItemCount));
    auto fit = [&](bool across) {
        float room = across ? node.w : node.h;
        float size = cell ? (across ? cell->w : cell->h) : 0.0f;
        return size > 0.0f ? std::max(1, static_cast<int>(std::floor(room / size + 0.01f))) : 1;
    };
    if (rescales) {
        bool across = rescaling == "horizontal";
        int fitted = fit(across);
        int other = std::max(1, (items + fitted - 1) / fitted);
        return across ? std::array<int, 2> { fitted, other } : std::array<int, 2> { other, fitted };
    }
    if (fills) {
        return filling == "horizontal" ? std::array<int, 2> { fit(true), 1 } : std::array<int, 2> { 1, fit(false) };
    }
    std::array<int, 2> given = gridDimensions(node);
    if (given[0] > 0 || given[1] > 0 || !text(node, "grid_dimension_binding").empty() || property(node, "grid_dimensions")) {
        return { std::max(1, given[0]), std::max(1, given[1]) };
    }
    return { 1, std::max(1, items) };
}

const Font::TextLayout& JsonUiRuntime::labelLayout(Node& node, float width, size_t maxLines)
{
    if (!(width > 0.0f)) {
        width = std::numeric_limits<float>::infinity();
    }
    Node::LabelLayout& cached = node.labelLayouts[maxLines > 0 ? 2 : std::isfinite(width) ? 1 : 0];
    const Font& font = ui->textFont();
    TextStyle style = labelStyle(node);
    bool hideHyphen = flag(node, "hide_hyphen", false);
    if (cached.font != &font || cached.revision != font.revision() || cached.source != node.text || cached.style != style || cached.width != width
        || cached.maxLines != maxLines || cached.hideHyphen != hideHyphen) {
        Font::WrapOptions options;
        options.hideHyphen = hideHyphen;
        options.maxLines = maxLines;
        cached.layout = font.layout(node.text, style, width, options);
        cached.source = node.text;
        cached.font = &font;
        cached.revision = font.revision();
        cached.style = style;
        cached.width = width;
        cached.maxLines = maxLines;
        cached.hideHyphen = hideHyphen;
    }
    return cached.layout;
}

float JsonUiRuntime::natural(Node& node, int axis)
{
    if (node.type == "label") {
        float scale = labelScale(node);
        float padding = static_cast<float>(number(node, "line_padding", 0.0));
        if (axis == 0) {
            return std::ceil(labelLayout(node, std::numeric_limits<float>::infinity()).width * scale);
        }
        size_t lines = labelLayout(node, node.w > 0.0f ? node.w / scale + 0.01f : std::numeric_limits<float>::infinity()).lines.size();
        return static_cast<float>(lines) * (LabelLineHeight + padding) * scale - padding * scale;
    }
    if (node.type == "image" && !node.texture.empty()) {
        const Sprite& sprite = ui->skin().sprite(node.texture);
        return axis == 0 ? sprite.width : sprite.height;
    }
    if (node.type != "grid") {
        // A container sized "default" fits its children.
        float widest = 0.0f;
        float sum = 0.0f;
        for (std::unique_ptr<Node>& child : node.children) {
            if (!child->shown) {
                continue;
            }
            float size = intrinsic(*child, axis);
            if (!std::isnan(size)) {
                sum += size;
                widest = std::max(widest, size);
            }
        }
        return alongAxis(node, axis) ? sum : widest;
    }
    Node* cell = nullptr;
    for (std::unique_ptr<Node>& child : node.children) {
        if (child->shown) {
            cell = child.get();
            break;
        }
    }
    if (!cell) {
        return 0.0f;
    }
    float extent = intrinsic(*cell, axis);
    if (std::isnan(extent)) {
        extent = axis == 0 ? cell->w : cell->h;
    }
    std::array<int, 2> cells = gridCells(node);
    return extent * static_cast<float>(cells[static_cast<size_t>(axis)]);
    return 0.0f;
}

/**
 * How big node is on axis without looking at its parent, or NaN when its
 * size is a share of the parent. This is what "100%c" adds up, so children
 * sized after their parent never feed back into it.
 */
bool JsonUiRuntime::inheritsSiblingMax(const Node& node, int axis) const
{
    return flag(node, axis == 0 ? "inherit_max_sibling_width" : "inherit_max_sibling_height", false);
}

/**
 * A control's measured size, which with inherit_max_sibling_width (or
 * height) is the biggest one among itself and its shown siblings, each
 * measured before any takes another's.
 */
float JsonUiRuntime::intrinsic(Node& node, int axis)
{
    float own = measure(node, axis);
    if (!node.parent || std::isnan(own) || !inheritsSiblingMax(node, axis)) {
        return own;
    }
    float widest = own;
    for (std::unique_ptr<Node>& sibling : node.parent->children) {
        if (sibling.get() == &node || !sibling->shown) {
            continue;
        }
        float size = measure(*sibling, axis);
        if (!std::isnan(size)) {
            widest = std::max(widest, size);
        }
    }
    return widest;
}

float JsonUiRuntime::measure(Node& node, int axis)
{
    if (node.measured[axis]) {
        return node.intrinsic[axis];
    }
    node.measured[axis] = true;
    node.intrinsic[axis] = Unknown;
    const Extent& extent = axis == 0 ? node.width : node.height;
    if (axis == 0 && uses(extent, TermKind::OwnY)) {
        widthReadsHeight = true;
        if (!heightsKnown) {
            return Unknown;
        }
    }
    if (uses(extent, TermKind::Parent) || uses(extent, TermKind::Fill)) {
        return Unknown;
    }
    float total = 0.0f;
    bool along = alongAxis(node, axis);
    for (const Term& part : extent) {
        switch (part.kind) {
        case TermKind::Pixel:
            total += part.amount;
            break;
        case TermKind::Default:
            total += part.amount * natural(node, axis);
            break;
        case TermKind::OwnX:
            total += part.amount * node.w;
            break;
        case TermKind::OwnY:
            total += part.amount * node.h;
            break;
        case TermKind::Children:
        case TermKind::ChildrenMax: {
            if (node.type == "grid") {
                total += part.amount * natural(node, axis);
                break;
            }
            float sum = 0.0f;
            float widest = 0.0f;
            for (std::unique_ptr<Node>& child : node.children) {
                if (!child->shown) {
                    continue;
                }
                float size = intrinsic(*child, axis);
                if (std::isnan(size)) {
                    continue;
                }
                sum += size;
                widest = std::max(widest, size);
            }
            // "%c" adds the children up and "%cm" takes the biggest; only a stack panel
            // across its orientation, where its children sit side by side, takes the biggest
            // for "%c" too. The scoreboard of many packs sizes its frame by this sum.
            bool biggest = part.kind == TermKind::ChildrenMax || (node.type == "stack_panel" && !along);
            total += part.amount * (biggest ? widest : sum);
            break;
        }
        case TermKind::SiblingMax: {
            float widest = 0.0f;
            if (node.parent) {
                for (std::unique_ptr<Node>& sibling : node.parent->children) {
                    if (sibling.get() == &node || !sibling->shown || uses(axis == 0 ? sibling->width : sibling->height, TermKind::SiblingMax)) {
                        continue;
                    }
                    float size = intrinsic(*sibling, axis);
                    if (!std::isnan(size)) {
                        widest = std::max(widest, size);
                    }
                }
            }
            total += part.amount * widest;
            break;
        }
        default:
            break;
        }
    }
    auto limit = [&](const char* key, bool upper) {
        const json::Value* bounds = property(node, key);
        if (!bounds || !bounds->isArray() || bounds->mArray.size() != 2) {
            return;
        }
        const json::Value* value = resolve(node, bounds->mArray[static_cast<size_t>(axis)].get());
        Extent range = parseExtent(value, TermKind::Pixel);
        if (uses(range, TermKind::Parent) || uses(range, TermKind::Fill) || uses(range, TermKind::Children)) {
            return;
        }
        float amount = 0.0f;
        for (const Term& part : range) {
            amount += part.kind == TermKind::Default ? part.amount * natural(node, axis) : part.kind == TermKind::Pixel ? part.amount : 0.0f;
        }
        total = upper ? (amount > 0.0f ? std::min(total, amount) : total) : std::max(total, amount);
    };
    limit("max_size", true);
    limit("min_size", false);
    node.intrinsic[axis] = std::max(0.0f, total);
    return node.intrinsic[axis];
}

/**
 * Sizes node on axis inside a parent that size long, then its children. A
 * stack panel shares what its other children leave among the ones sized
 * "fill", and children sized after their siblings go last.
 */
void JsonUiRuntime::size(Node& node, int axis, float parent, std::optional<float> forced)
{
    const Extent& extent = axis == 0 ? node.width : node.height;
    float value = 0.0f;
    if (forced) {
        value = *forced;
    } else if (std::optional<float> moving = animated(node, "size", axis, parent)) {
        value = *moving;
    } else {
        bool along = alongAxis(node, axis);
        for (const Term& part : extent) {
            switch (part.kind) {
            case TermKind::Pixel:
                value += part.amount;
                break;
            case TermKind::Parent:
            case TermKind::Fill:
                value += part.amount * parent;
                break;
            case TermKind::Default:
                value += part.amount * natural(node, axis);
                break;
            case TermKind::OwnX:
                value += part.amount * node.w;
                break;
            case TermKind::OwnY: {
                if (axis == 0) {
                    widthReadsHeight = true;
                }
                float height = intrinsic(node, 1);
                value += part.amount * (std::isnan(height) ? node.h : height);
                break;
            }
            case TermKind::Children:
            case TermKind::ChildrenMax: {
                if (node.type == "grid") {
                    value += part.amount * natural(node, axis);
                    break;
                }
                float sum = 0.0f;
                float widest = 0.0f;
                for (std::unique_ptr<Node>& child : node.children) {
                    if (!child->shown) {
                        continue;
                    }
                    float size = intrinsic(*child, axis);
                    if (std::isnan(size)) {
                        continue;
                    }
                    sum += size;
                    widest = std::max(widest, size);
                }
                bool biggest = part.kind == TermKind::ChildrenMax || (node.type == "stack_panel" && !along);
                value += part.amount * (biggest ? widest : sum);
                break;
            }
            case TermKind::SiblingMax: {
                float widest = 0.0f;
                if (node.parent) {
                    for (std::unique_ptr<Node>& sibling : node.parent->children) {
                        if (sibling.get() != &node && sibling->shown && !uses(axis == 0 ? sibling->width : sibling->height, TermKind::SiblingMax)) {
                            widest = std::max(widest, axis == 0 ? sibling->w : sibling->h);
                        }
                    }
                }
                value += part.amount * widest;
                break;
            }
            }
        }
        auto limit = [&](const char* key, bool upper) {
            const json::Value* bounds = property(node, key);
            if (!bounds || !bounds->isArray() || bounds->mArray.size() != 2) {
                return;
            }
            const json::Value* entry = resolve(node, bounds->mArray[static_cast<size_t>(axis)].get());
            float amount = 0.0f;
            for (const Term& part : parseExtent(entry, TermKind::Pixel)) {
                amount += part.kind == TermKind::Pixel ? part.amount
                    : part.kind == TermKind::Parent || part.kind == TermKind::Fill ? part.amount * parent
                    : part.kind == TermKind::Default ? part.amount * natural(node, axis)
                    : part.kind == TermKind::Children || part.kind == TermKind::ChildrenMax ? part.amount * intrinsic(node, axis)
                                                                                           : 0.0f;
            }
            // A zero maximum leaves that axis unbounded; packs write [0, 1000] to cap only the height.
            value = upper ? (amount > 0.0f ? std::min(value, amount) : value) : std::max(value, amount);
        };
        limit("max_size", true);
        limit("min_size", false);
    }
    value = std::max(0.0f, value);
    (axis == 0 ? node.w : node.h) = value;

    std::vector<Node*> fills;
    std::vector<Node*> siblingSized;
    bool along = alongAxis(node, axis);
    float used = 0.0f;
    for (std::unique_ptr<Node>& child : node.children) {
        if (!child->shown) {
            continue;
        }
        const Extent& childExtent = axis == 0 ? child->width : child->height;
        if (along && uses(childExtent, TermKind::Fill)) {
            fills.push_back(child.get());
        } else if (uses(childExtent, TermKind::SiblingMax)) {
            siblingSized.push_back(child.get());
        } else {
            size(*child, axis, value);
            used += axis == 0 ? child->w : child->h;
        }
        if (along && node.type == "stack_panel") {
            used += stackedOffset(*child, axis, value);
        }
    }
    for (Node* child : siblingSized) {
        size(*child, axis, value);
        used += axis == 0 ? child->w : child->h;
    }
    if (!fills.empty()) {
        float share = std::max(0.0f, value - used) / static_cast<float>(fills.size());
        for (Node* child : fills) {
            size(*child, axis, value, share);
        }
    }
    float widest = 0.0f;
    bool inheriting = false;
    for (std::unique_ptr<Node>& child : node.children) {
        if (child->shown) {
            widest = std::max(widest, axis == 0 ? child->w : child->h);
            inheriting = inheriting || inheritsSiblingMax(*child, axis);
        }
    }
    if (inheriting) {
        for (std::unique_ptr<Node>& child : node.children) {
            if (child->shown && inheritsSiblingMax(*child, axis) && (axis == 0 ? child->w : child->h) < widest) {
                size(*child, axis, value, widest);
            }
        }
    }
}

/**
 * How far a stack panel's child is moved along the stack by its offset. The
 * game counts it in the room the panel's "fill" children share, so a last
 * child pulled back by its offset still leaves the row ending flush.
 */
float JsonUiRuntime::stackedOffset(Node& child, int axis, float parent)
{
    const json::Value* offset = property(child, "offset");
    if (!offset || !offset->isArray() || offset->mArray.size() != 2) {
        return 0.0f;
    }
    return term(child, offset->mArray[static_cast<size_t>(axis)].get(), parent);
}

void JsonUiRuntime::place(Node& node, float x, float y, float z, const Rect& clip, bool clipped, float alpha)
{
    node.x = x;
    node.y = y;
    node.z = z;
    node.clip = clip;
    node.clipped = clipped;
    node.inherited = alpha;
    node.paintAlpha = alpha * node.alpha;
    float passed = node.propagate ? node.paintAlpha : alpha;

    Rect childClip = clip;
    bool childClipped = clipped;
    if (flag(node, "clips_children", false)) {
        Rect own { x, y, node.w, node.h };
        childClip = clipped ? intersect(clip, own) : own;
        childClipped = true;
    }

    bool stack = node.type == "stack_panel";
    bool vertical = alongAxis(node, 1);
    bool anchored = !stack || flag(node, "use_child_anchors", false);
    bool grid = node.type == "grid";
    std::array<int, 2> cells = grid ? gridCells(node) : std::array<int, 2> { 1, 1 };
    bool fillRows = text(node, "grid_fill_direction") != "vertical";
    float cursor = 0.0f;
    int cell = 0;
    for (std::unique_ptr<Node>& owned : node.children) {
        Node& child = *owned;
        if (!child.shown) {
            continue;
        }
        float ox = 0.0f;
        float oy = 0.0f;
        auto boundOffset = child.bound.find("#offset");
        if (child.offsetOverride) {
            ox = (*child.offsetOverride)[0];
            oy = (*child.offsetOverride)[1];
        } else if (boundOffset != child.bound.end() && boundOffset->second.kind == UiValue::Kind::String) {
            // The game binds offsets as "x,y" in pixels, like #item_name_text_offset.
            const std::string& value = boundOffset->second.text;
            size_t comma = value.find(',');
            ox = static_cast<float>(std::atof(value.c_str()));
            oy = comma == std::string::npos ? 0.0f : static_cast<float>(std::atof(value.c_str() + comma + 1));
        } else if (flag(child, "use_anchored_offset", false)) {
            ox = static_cast<float>(lookup(child, "#anchored_offset_value_x").toNumber());
            oy = static_cast<float>(lookup(child, "#anchored_offset_value_y").toNumber());
        } else if (std::optional<float> moving = animated(child, "offset", 0, node.w)) {
            ox = *moving;
            oy = animated(child, "offset", 1, node.h).value_or(0.0f);
        } else if (const json::Value* offset = property(child, "offset"); offset && offset->isArray() && offset->mArray.size() == 2) {
            ox = term(child, offset->mArray[0].get(), node.w);
            oy = term(child, offset->mArray[1].get(), node.h);
        }
        float cx = x + ox;
        float cy = y + oy;
        if (anchored && !grid) {
            std::array<float, 2> from = anchorPoint(text(child, "anchor_from"));
            std::array<float, 2> to = anchorPoint(text(child, "anchor_to"));
            cx += from[0] * node.w - to[0] * child.w;
            cy += from[1] * node.h - to[1] * child.h;
        }
        if (child.scroller) {
            cy -= std::trunc(child.scroller->scroll * 8.0f) * 0.125f;
            if (anchorPoint(text(child, "anchor_from"))[1] == 1.0f) {
                cy += child.scroller->scrollRange;
            }
        }
        if (grid) {
            if (child.index >= 0) {
                cell = child.index;
            }
            int column = fillRows ? cell % cells[0] : cell / cells[1];
            int row = fillRows ? cell / cells[0] : cell % cells[1];
            cx = x + ox + static_cast<float>(column) * child.w;
            cy = y + oy + static_cast<float>(row) * child.h;
            ++cell;
        } else if (stack) {
            std::array<float, 2> from = anchorPoint(text(child, "anchor_from"));
            std::array<float, 2> to = anchorPoint(text(child, "anchor_to"));
            if (vertical) {
                cy = y + cursor + oy;
                cursor += child.h;
                if (!anchored) {
                    cx += from[0] * node.w - to[0] * child.w;
                }
            } else {
                cx = x + cursor + ox;
                cursor += child.w;
                if (!anchored) {
                    cy += from[1] * node.h - to[1] * child.h;
                }
            }
        }
        place(child, cx, cy, z + static_cast<float>(number(child, "layer", 0.0)), childClip, childClipped, passed);
    }
}

}
