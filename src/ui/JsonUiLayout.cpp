#include "JsonUiInternal.h"

#include "ui/Context.h"

#include <algorithm>
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

float easing(std::string_view name, float t)
{
    constexpr float Pi = 3.14159265f;
    t = std::clamp(t, 0.0f, 1.0f);
    auto bounceOut = [](float x) {
        if (x < 1.0f / 2.75f) {
            return 7.5625f * x * x;
        }
        if (x < 2.0f / 2.75f) {
            x -= 1.5f / 2.75f;
            return 7.5625f * x * x + 0.75f;
        }
        if (x < 2.5f / 2.75f) {
            x -= 2.25f / 2.75f;
            return 7.5625f * x * x + 0.9375f;
        }
        x -= 2.625f / 2.75f;
        return 7.5625f * x * x + 0.984375f;
    };
    auto power = [&](float exponent, std::string_view kind) {
        if (kind == "in") {
            return std::pow(t, exponent);
        }
        if (kind == "out") {
            return 1.0f - std::pow(1.0f - t, exponent);
        }
        return t < 0.5f ? std::pow(2.0f * t, exponent) * 0.5f : 1.0f - std::pow(-2.0f * t + 2.0f, exponent) * 0.5f;
    };
    size_t split = name.find('_');
    std::string_view kind = split == std::string_view::npos ? name : name.substr(0, split);
    std::string_view curve = split == std::string_view::npos ? std::string_view() : name.substr(split + 1);
    if (kind == "in" && curve.substr(0, 4) == "out_") {
        kind = "in_out";
        curve = curve.substr(4);
    }
    if (curve == "quad") {
        return power(2.0f, kind);
    }
    if (curve == "cubic") {
        return power(3.0f, kind);
    }
    if (curve == "quart") {
        return power(4.0f, kind);
    }
    if (curve == "quint") {
        return power(5.0f, kind);
    }
    if (curve == "sine") {
        return kind == "in" ? 1.0f - std::cos(t * Pi * 0.5f) : kind == "out" ? std::sin(t * Pi * 0.5f) : -(std::cos(Pi * t) - 1.0f) * 0.5f;
    }
    if (curve == "expo") {
        if (kind == "in") {
            return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        }
        if (kind == "out") {
            return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        }
        return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
    }
    if (curve == "circ") {
        return kind == "in" ? 1.0f - std::sqrt(1.0f - t * t) : kind == "out" ? std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f))
            : t < 0.5f ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) * 0.5f : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;
    }
    if (curve == "back") {
        constexpr float C1 = 1.70158f;
        constexpr float C3 = C1 + 1.0f;
        constexpr float C2 = C1 * 1.525f;
        if (kind == "in") {
            return C3 * t * t * t - C1 * t * t;
        }
        if (kind == "out") {
            return 1.0f + C3 * std::pow(t - 1.0f, 3.0f) + C1 * std::pow(t - 1.0f, 2.0f);
        }
        return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((C2 + 1.0f) * 2.0f * t - C2)) * 0.5f : (std::pow(2.0f * t - 2.0f, 2.0f) * ((C2 + 1.0f) * (t * 2.0f - 2.0f) + C2) + 2.0f) * 0.5f;
    }
    if (curve == "elastic") {
        constexpr float C4 = 2.0f * Pi / 3.0f;
        if (t <= 0.0f || t >= 1.0f) {
            return t;
        }
        if (kind == "in") {
            return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * C4);
        }
        if (kind == "out") {
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * C4) + 1.0f;
        }
        constexpr float C5 = 2.0f * Pi / 4.5f;
        return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * C5)) * 0.5f : std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * C5) * 0.5f + 1.0f;
    }
    if (curve == "bounce") {
        if (kind == "in") {
            return 1.0f - bounceOut(1.0f - t);
        }
        if (kind == "out") {
            return bounceOut(t);
        }
        return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) * 0.5f : (1.0f + bounceOut(2.0f * t - 1.0f)) * 0.5f;
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
    double duration = std::max(0.0, number(probe, "duration", 0.0));
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
 * The face a label draws with: the classic bitmap font, or Minecraft Ten for
 * font_type MinecraftTen.
 */
TextStyle JsonUiRuntime::labelStyle(const Node& node) const
{
    std::string type = text(node, "font_type");
    return type.rfind("MinecraftTen", 0) == 0 ? TextStyle::TenLabel : TextStyle::Pixel;
}

/**
 * How many columns and rows a grid lays its items out in: the ones it is
 * given, or with grid_rescaling_type as many as fit across (or down) its size
 * and as many rows (or columns) as the items need.
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
    if (cell && (rescaling == "horizontal" || rescaling == "vertical")) {
        bool across = rescaling == "horizontal";
        float room = across ? node.w : node.h;
        float size = across ? cell->w : cell->h;
        int fit = size > 0.0f ? std::max(1, static_cast<int>(std::floor(room / size + 0.01f))) : 1;
        int other = std::max(1, (items + fit - 1) / fit);
        return across ? std::array<int, 2> { fit, other } : std::array<int, 2> { other, fit };
    }
    std::string binding = text(node, "grid_dimension_binding");
    std::string dimensions = binding.empty() ? std::string() : lookup(node, binding).toText();
    if (!dimensions.empty()) {
        size_t comma = dimensions.find(',');
        return { std::max(1, std::atoi(dimensions.c_str())), comma == std::string::npos ? 1 : std::max(1, std::atoi(dimensions.c_str() + comma + 1)) };
    }
    if (const json::Value* grid = property(node, "grid_dimensions"); grid && grid->isArray() && grid->mArray.size() == 2) {
        return { std::max(1, static_cast<int>(term(node, grid->mArray[0].get(), 0.0f))), std::max(1, static_cast<int>(term(node, grid->mArray[1].get(), 0.0f))) };
    }
    return { 1, std::max(1, items) };
}

float JsonUiRuntime::natural(Node& node, int axis)
{
    if (node.type == "label") {
        float scale = labelScale(node);
        float padding = static_cast<float>(number(node, "line_padding", 0.0));
        TextStyle style = labelStyle(node);
        if (axis == 0) {
            float widest = 0.0f;
            size_t start = 0;
            while (true) {
                size_t end = node.text.find('\n', start);
                widest = std::max(widest, ui->measure(std::string_view(node.text).substr(start, end == std::string::npos ? std::string::npos : end - start), style));
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return std::ceil(widest * scale);
        }
        size_t lines = 0;
        size_t start = 0;
        std::vector<std::string_view> wrapped;
        while (true) {
            size_t end = node.text.find('\n', start);
            std::string_view line = std::string_view(node.text).substr(start, end == std::string::npos ? std::string::npos : end - start);
            wrapped.clear();
            lines += std::max<size_t>(1, node.w > 0.0f ? ui->wrap(line, style, node.w / scale + 0.01f, wrapped) : 1);
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        if (node.text.empty()) {
            lines = 1;
        }
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
float JsonUiRuntime::intrinsic(Node& node, int axis)
{
    if (node.measured[axis]) {
        return node.intrinsic[axis];
    }
    node.measured[axis] = true;
    node.intrinsic[axis] = Unknown;
    const Extent& extent = axis == 0 ? node.width : node.height;
    if (uses(extent, TermKind::Parent) || uses(extent, TermKind::Fill) || (axis == 0 && uses(extent, TermKind::OwnY))) {
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
            cy -= child.scroller->scroll;
        }
        if (grid) {
            int column = fillRows ? cell % cells[0] : cell / cells[1];
            int row = fillRows ? cell / cells[0] : cell % cells[1];
            cx = x + ox + static_cast<float>(column) * child.w;
            cy = y + oy + static_cast<float>(row) * child.h;
            ++cell;
        } else if (stack) {
            if (vertical) {
                cy = y + cursor + oy;
                cursor += child.h;
            } else {
                cx = x + cursor + ox;
                cursor += child.w;
            }
        }
        place(child, cx, cy, z + static_cast<float>(number(child, "layer", 0.0)), childClip, childClipped, passed);
    }
}

}
