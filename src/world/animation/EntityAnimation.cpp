#include "world/EntityAnimation.h"
#include "world/ItemInfo.h"

#include "Core/Json/Json.h"
#include "util/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace kestrel::world {

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr int MaxDepth = 8;
constexpr double TickSeconds = 0.05;
constexpr int MaxCatchUpTicks = 5;

using util::lowercase;

double wrapDegrees(double degrees)
{
    double value = std::fmod(degrees + 180.0, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value - 180.0;
}

molang::Script scriptOf(const json::Value* value, double fallback)
{
    if (!value) {
        return molang::Script(fallback);
    }
    if (value->isNumber()) {
        return molang::Script(value->mNumber);
    }
    if (value->isString()) {
        return molang::Script::compile(value->mString, fallback);
    }
    if (value->mType == json::Value::Type::Boolean) {
        return molang::Script(value->mBoolean ? 1.0 : 0.0);
    }
    return molang::Script(fallback);
}

molang::Script optionalScript(const json::Value* value)
{
    if (!value || (!value->isNumber() && !value->isString() && value->mType != json::Value::Type::Boolean)) {
        return {};
    }
    return scriptOf(value, 0.0);
}

std::vector<molang::Script> scriptList(const json::Value* value)
{
    std::vector<molang::Script> list;
    if (!value) {
        return list;
    }
    if (value->isString()) {
        list.push_back(molang::Script::compile(value->mString));
        return list;
    }
    if (value->isArray()) {
        for (const auto& entry : value->mArray) {
            if (entry->isString()) {
                list.push_back(molang::Script::compile(entry->mString));
            }
        }
    }
    return list;
}

std::array<molang::Script, 3> vectorOf(const json::Value* value, double fallback)
{
    std::array<molang::Script, 3> result { molang::Script(fallback), molang::Script(fallback), molang::Script(fallback) };
    if (!value) {
        return result;
    }
    if (value->isArray()) {
        if (value->mArray.size() == 1) {
            molang::Script single = scriptOf(value->mArray.front().get(), fallback);
            return { single, single, single };
        }
        for (size_t axis = 0; axis < 3 && axis < value->mArray.size(); ++axis) {
            result[axis] = scriptOf(value->mArray[axis].get(), fallback);
        }
        return result;
    }
    molang::Script uniform = scriptOf(value, fallback);
    return { uniform, uniform, uniform };
}

bool isKeyObject(const json::Value& value)
{
    return value.get("pre") || value.get("post") || value.get("lerp_mode");
}

AnimationKey keyOf(float time, const json::Value& value, double fallback)
{
    AnimationKey key;
    key.time = time;
    if (value.isObject() && isKeyObject(value)) {
        const json::Value* pre = value.get("pre");
        const json::Value* post = value.get("post");
        key.pre = vectorOf(pre ? pre : post, fallback);
        key.post = vectorOf(post ? post : pre, fallback);
        if (const json::Value* mode = value.get("lerp_mode"); mode && mode->isString()) {
            std::string name = lowercase(mode->mString);
            key.lerp = name == "step" ? LerpMode::Step : name == "catmullrom" ? LerpMode::CatmullRom : LerpMode::Linear;
        }
        return key;
    }
    key.pre = vectorOf(&value, fallback);
    key.post = key.pre;
    return key;
}

AnimationChannel channelOf(const json::Value* value, double fallback)
{
    AnimationChannel channel;
    if (!value) {
        return channel;
    }
    if (value->isObject() && !isKeyObject(*value)) {
        for (const std::string& name : value->mKeys) {
            char* end = nullptr;
            float time = std::strtof(name.c_str(), &end);
            if (end == name.c_str()) {
                continue;
            }
            channel.keys.push_back(keyOf(time, *value->get(name), fallback));
        }
        std::sort(channel.keys.begin(), channel.keys.end(), [](const AnimationKey& a, const AnimationKey& b) {
            return a.time < b.time;
        });
        return channel;
    }
    channel.keys.push_back(keyOf(0.0f, *value, fallback));
    return channel;
}

std::vector<std::pair<float, float>> curveOf(const json::Value* value)
{
    std::vector<std::pair<float, float>> curve;
    if (!value || !value->isObject()) {
        return curve;
    }
    for (const std::string& name : value->mKeys) {
        const json::Value* point = value->get(name);
        char* end = nullptr;
        float at = std::strtof(name.c_str(), &end);
        if (end == name.c_str() || !point || !point->isNumber()) {
            continue;
        }
        curve.emplace_back(at, static_cast<float>(point->mNumber));
    }
    std::sort(curve.begin(), curve.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return curve;
}

double sampleCurve(const std::vector<std::pair<float, float>>& curve, double at)
{
    if (curve.empty()) {
        return at;
    }
    if (at <= curve.front().first) {
        return curve.front().second;
    }
    if (at >= curve.back().first) {
        return curve.back().second;
    }
    for (size_t index = 0; index + 1 < curve.size(); ++index) {
        const auto& from = curve[index];
        const auto& to = curve[index + 1];
        if (at <= to.first) {
            double span = to.first - from.first;
            double t = span > 0.0 ? (at - from.first) / span : 1.0;
            return from.second + (to.second - from.second) * t;
        }
    }
    return curve.back().second;
}

std::vector<molang::Script> effectScripts(const json::Value* value)
{
    std::vector<molang::Script> list;
    if (!value) {
        return list;
    }
    auto add = [&](const json::Value* effect) {
        if (!effect || !effect->isObject()) {
            return;
        }
        if (const json::Value* script = effect->get("pre_effect_script"); script && script->isString()) {
            list.push_back(molang::Script::compile(script->mString));
        }
    };
    if (value->isArray()) {
        for (const auto& entry : value->mArray) {
            add(entry.get());
        }
    } else {
        add(value);
    }
    return list;
}

void addEffectTimeline(AnimationClip& clip, const json::Value* effects, float& longest)
{
    if (!effects || !effects->isObject()) {
        return;
    }
    for (const std::string& name : effects->mKeys) {
        char* end = nullptr;
        float time = std::strtof(name.c_str(), &end);
        if (end == name.c_str()) {
            continue;
        }
        longest = std::max(longest, time);
        std::vector<molang::Script> scripts = effectScripts(effects->get(name));
        if (!scripts.empty()) {
            clip.timeline.emplace_back(time, std::move(scripts));
        }
    }
}

AnimationClip clipOf(const json::Value& value)
{
    AnimationClip clip;
    if (const json::Value* loop = value.get("loop")) {
        if (loop->mType == json::Value::Type::Boolean) {
            clip.loop = loop->mBoolean ? LoopMode::Loop : LoopMode::Once;
        } else if (loop->isString()) {
            clip.loop = loop->mString == "hold_on_last_frame" ? LoopMode::Hold : loop->mString == "true" ? LoopMode::Loop : LoopMode::Once;
        }
    }
    if (const json::Value* override = value.get("override_previous_animation"); override && override->mType == json::Value::Type::Boolean) {
        clip.overridePrevious = override->mBoolean;
    }
    clip.animTimeUpdate = optionalScript(value.get("anim_time_update"));
    clip.blendWeight = optionalScript(value.get("blend_weight"));
    clip.startDelay = optionalScript(value.get("start_delay"));
    clip.loopDelay = optionalScript(value.get("loop_delay"));
    float longest = 0.0f;
    if (const json::Value* bones = value.get("bones"); bones && bones->isObject()) {
        for (const std::string& name : bones->mKeys) {
            const json::Value* bone = bones->get(name);
            if (!bone || !bone->isObject()) {
                continue;
            }
            AnimationBone track;
            track.bone = lowercase(name);
            track.rotation = channelOf(bone->get("rotation"), 0.0);
            track.position = channelOf(bone->get("position"), 0.0);
            track.scale = channelOf(bone->get("scale"), 1.0);
            if (const json::Value* relative = bone->get("relative_to"); relative && relative->isObject()) {
                const json::Value* rotation = relative->get("rotation");
                track.rotationRelativeToEntity = rotation && rotation->isString() && lowercase(rotation->mString) == "entity";
            }
            for (const AnimationChannel* channel : { &track.rotation, &track.position, &track.scale }) {
                if (!channel->keys.empty()) {
                    longest = std::max(longest, channel->keys.back().time);
                }
            }
            clip.bones.push_back(std::move(track));
        }
    }
    if (const json::Value* timeline = value.get("timeline"); timeline && timeline->isObject()) {
        for (const std::string& name : timeline->mKeys) {
            float time = std::strtof(name.c_str(), nullptr);
            clip.timeline.emplace_back(time, scriptList(timeline->get(name)));
            longest = std::max(longest, time);
        }
    }
    addEffectTimeline(clip, value.get("particle_effects"), longest);
    addEffectTimeline(clip, value.get("sound_effects"), longest);
    std::stable_sort(clip.timeline.begin(), clip.timeline.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    const json::Value* length = value.get("animation_length");
    clip.length = length && length->isNumber() ? static_cast<float>(length->mNumber) : longest;
    return clip;
}

std::vector<std::pair<std::string, molang::Script>> namedScripts(const json::Value* value)
{
    std::vector<std::pair<std::string, molang::Script>> list;
    if (!value || !value->isArray()) {
        return list;
    }
    for (const auto& entry : value->mArray) {
        if (entry->isString()) {
            list.emplace_back(lowercase(entry->mString), molang::Script {});
        } else if (entry->isObject()) {
            for (const std::string& name : entry->mKeys) {
                list.emplace_back(lowercase(name), scriptOf(entry->get(name), 1.0));
            }
        }
    }
    return list;
}

AnimationController controllerOf(const json::Value& value)
{
    AnimationController controller;
    if (const json::Value* initial = value.get("initial_state"); initial && initial->isString()) {
        controller.initialState = lowercase(initial->mString);
    }
    const json::Value* states = value.get("states");
    if (!states || !states->isObject()) {
        return controller;
    }
    for (const std::string& name : states->mKeys) {
        const json::Value* state = states->get(name);
        if (!state || !state->isObject()) {
            continue;
        }
        ControllerState parsed;
        parsed.animations = namedScripts(state->get("animations"));
        parsed.transitions = namedScripts(state->get("transitions"));
        parsed.onEntry = scriptList(state->get("on_entry"));
        parsed.onExit = scriptList(state->get("on_exit"));
        for (const char* effects : { "particle_effects", "sound_effects" }) {
            if (const json::Value* list = state->get(effects); list && list->isArray()) {
                for (const auto& entry : list->mArray) {
                    std::vector<molang::Script> scripts = effectScripts(entry.get());
                    parsed.onEntry.insert(parsed.onEntry.end(), scripts.begin(), scripts.end());
                }
            }
        }
        if (const json::Value* variables = state->get("variables"); variables && variables->isObject()) {
            for (const std::string& key : variables->mKeys) {
                const json::Value* entry = variables->get(key);
                if (!entry || !entry->isObject()) {
                    continue;
                }
                ControllerVariable variable;
                std::string variableName = lowercase(key);
                if (variableName.rfind("variable.", 0) == 0) {
                    variableName = variableName.substr(9);
                } else if (variableName.rfind("v.", 0) == 0) {
                    variableName = variableName.substr(2);
                }
                variable.name = variableName;
                variable.input = scriptOf(entry->get("input"), 0.0);
                variable.remap = curveOf(entry->get("remap_curve"));
                parsed.variables.push_back(std::move(variable));
            }
        }
        if (const json::Value* blend = state->get("blend_transition")) {
            if (blend->isNumber()) {
                parsed.blendTransition = static_cast<float>(blend->mNumber);
            } else if (blend->isObject()) {
                parsed.blendCurve = curveOf(blend);
                if (!parsed.blendCurve.empty()) {
                    parsed.blendTransition = parsed.blendCurve.back().first;
                }
            }
        }
        if (const json::Value* shortest = state->get("blend_via_shortest_path"); shortest && shortest->mType == json::Value::Type::Boolean) {
            parsed.blendShortestPath = shortest->mBoolean;
        }
        controller.states[lowercase(name)] = std::move(parsed);
    }
    if (!controller.states.count(controller.initialState) && !controller.states.empty()) {
        controller.initialState = controller.states.count("default") ? "default" : controller.states.begin()->first;
    }
    return controller;
}

using Row = std::array<float, 12>;

Row identity()
{
    return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
}

Row multiply(const Row& a, const Row& b)
{
    Row out {};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = a[row * 4 + 0] * b[0 * 4 + column] + a[row * 4 + 1] * b[1 * 4 + column] + a[row * 4 + 2] * b[2 * 4 + column];
            if (column == 3) {
                value += a[row * 4 + 3];
            }
            out[row * 4 + column] = value;
        }
    }
    return out;
}

Row localTransform(const std::array<float, 3>& pivot, const std::array<float, 3>& offset, const std::array<float, 3>& degrees, const std::array<float, 3>& scale)
{
    constexpr float Radians = static_cast<float>(Pi / 180.0);
    float sx = std::sin(degrees[0] * Radians);
    float cx = std::cos(degrees[0] * Radians);
    float sy = std::sin(degrees[1] * Radians);
    float cy = std::cos(degrees[1] * Radians);
    float sz = std::sin(degrees[2] * Radians);
    float cz = std::cos(degrees[2] * Radians);
    Row rx { 1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0 };
    Row ry { cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0 };
    Row rz { cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0 };
    Row rotation = multiply(rz, multiply(ry, rx));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            rotation[row * 4 + column] *= scale[column];
        }
    }
    Row out = rotation;
    for (int row = 0; row < 3; ++row) {
        float moved = 0.0f;
        for (int column = 0; column < 3; ++column) {
            moved += rotation[row * 4 + column] * -pivot[column];
        }
        out[row * 4 + 3] = moved + pivot[row] + offset[row];
    }
    return out;
}

}

void AnimationLibrary::parse(const json::Value& document)
{
    if (const json::Value* animations = document.get("animations"); animations && animations->isObject()) {
        for (const std::string& name : animations->mKeys) {
            const json::Value* clip = animations->get(name);
            if (clip && clip->isObject()) {
                clips[lowercase(name)] = clipOf(*clip);
            }
        }
    }
    if (const json::Value* controllersValue = document.get("animation_controllers"); controllersValue && controllersValue->isObject()) {
        for (const std::string& name : controllersValue->mKeys) {
            const json::Value* controller = controllersValue->get(name);
            if (controller && controller->isObject()) {
                controllers[lowercase(name)] = controllerOf(*controller);
            }
        }
    }
}

const AnimationClip* AnimationLibrary::clip(const std::string& name) const
{
    auto found = clips.find(name);
    return found == clips.end() ? nullptr : &found->second;
}

const AnimationController* AnimationLibrary::controller(const std::string& name) const
{
    auto found = controllers.find(name);
    return found == controllers.end() ? nullptr : &found->second;
}

std::shared_ptr<EntityScripts> readEntityScripts(const json::Value& description)
{
    auto scripts = std::make_shared<EntityScripts>();
    if (const json::Value* animations = description.get("animations"); animations && animations->isObject()) {
        for (const std::string& alias : animations->mKeys) {
            const json::Value* target = animations->get(alias);
            if (target && target->isString()) {
                scripts->aliases[lowercase(alias)] = lowercase(target->mString);
            }
        }
    }
    std::vector<std::string> legacyControllers;
    if (const json::Value* controllers = description.get("animation_controllers"); controllers && controllers->isArray()) {
        for (const auto& entry : controllers->mArray) {
            if (!entry->isObject()) {
                continue;
            }
            for (const std::string& alias : entry->mKeys) {
                const json::Value* target = entry->get(alias);
                if (target && target->isString()) {
                    // Legacy controller names can also name clips used inside that controller.
                    scripts->aliases.try_emplace(lowercase(alias), lowercase(target->mString));
                    legacyControllers.push_back(lowercase(target->mString));
                }
            }
        }
    }
    const json::Value* body = description.get("scripts");
    if (body && body->isObject()) {
        scripts->initialize = scriptList(body->get("initialize"));
        scripts->preAnimation = scriptList(body->get("pre_animation"));
        scripts->animate = namedScripts(body->get("animate"));
        scripts->scale = optionalScript(body->get("scale"));
        for (size_t axis = 0; axis < 3; ++axis) {
            scripts->axisScale[axis] = optionalScript(body->get(axis == 0 ? "scaleX" : axis == 1 ? "scaleY" : "scaleZ"));
        }
    }
    if (!body || !body->get("animate")) {
        for (const std::string& controller : legacyControllers) {
            scripts->animate.emplace_back(controller, molang::Script {});
        }
    }
    return scripts;
}

bool targetRotationIsAbsolute(const std::string& identifier)
{
    return identifier == "minecraft:arrow" || identifier == "minecraft:fireworks_rocket" || identifier == "minecraft:wither_skull"
        || identifier == "minecraft:wither_skull_dangerous";
}

bool cameraFacingSprite(const std::string& identifier)
{
    static constexpr const char* Sprites[] = {
        "minecraft:xp_bottle",
        "minecraft:ender_pearl",
        "minecraft:xp_orb",
        "minecraft:dragon_fireball",
        "minecraft:fireball",
        "minecraft:snowball",
        "minecraft:small_fireball",
        "minecraft:splash_potion",
        "minecraft:egg",
        "minecraft:eye_of_ender_signal",
        "minecraft:lingering_potion",
    };
    return std::any_of(std::begin(Sprites), std::end(Sprites), [&](const char* sprite) {
        return identifier == sprite;
    });
}

float entityModelYaw(const std::string& identifier, float bodyYaw)
{
    if (cameraFacingSprite(identifier)) {
        bool southFacing = identifier == "minecraft:xp_orb" || identifier == "minecraft:fireball"
            || identifier == "minecraft:dragon_fireball" || identifier == "minecraft:small_fireball";
        return southFacing ? 0.0f : 180.0f;
    }
    return targetRotationIsAbsolute(identifier) ? 0.0f : static_cast<float>(wrapDegrees(bodyYaw));
}

bool projectileEntity(const std::string& identifier)
{
    return targetRotationIsAbsolute(identifier) || (cameraFacingSprite(identifier) && identifier != "minecraft:xp_orb")
        || identifier == "minecraft:thrown_trident" || identifier == "minecraft:fishing_hook"
        || identifier == "minecraft:wind_charge_projectile" || identifier == "minecraft:breeze_wind_charge_projectile"
        || identifier == "minecraft:shulker_bullet";
}

void EntityAnimator::runScripts(const std::vector<molang::Script>& scripts)
{
    scope.temps.clear();
    for (const molang::Script& script : scripts) {
        script.run(scope);
    }
}

double EntityAnimator::evaluate(const molang::Script& script)
{
    scope.variables = &variables;
    scope.queries = this;
    scope.temps.clear();
    return script.run(scope);
}

double EntityAnimator::evaluateWithThis(const molang::Script& script, double base)
{
    scope.thisValue = base;
    double value = evaluate(script);
    scope.thisValue = 0.0;
    return std::isfinite(value) ? value : 0.0;
}

std::array<float, 3> EntityAnimator::evaluateKey(const std::array<molang::Script, 3>& values, const std::array<float, 3>& current)
{
    std::array<float, 3> out {};
    for (size_t axis = 0; axis < 3; ++axis) {
        scope.temps.clear();
        scope.thisValue = current[axis];
        out[axis] = static_cast<float>(values[axis].run(scope));
    }
    scope.thisValue = 0.0;
    return out;
}

std::unordered_map<std::string, std::array<float, 3>> EntityAnimator::rotationsAt(const AnimationClip& clip, double time)
{
    scope.queries = this;
    animTime = time;
    std::unordered_map<std::string, std::array<float, 3>> rotations;
    for (const AnimationBone& bone : clip.bones) {
        if (bone.rotation.present()) {
            rotations[bone.bone] = sample(bone.rotation, time, { 0.0f, 0.0f, 0.0f });
        }
    }
    return rotations;
}

std::array<float, 3> EntityAnimator::sample(const AnimationChannel& channel, double time, const std::array<float, 3>& current)
{
    struct RestoreLerp {
        double& value;
        double previous;
        ~RestoreLerp() { value = previous; }
    };
    double& lerp = variables["key_frame_lerp_time"];
    RestoreLerp restore { lerp, lerp };
    lerp = 0.0;
    const std::vector<AnimationKey>& keys = channel.keys;
    if (keys.size() == 1 || time <= keys.front().time) {
        return evaluateKey(keys.size() == 1 ? keys.front().post : keys.front().pre, current);
    }
    if (time >= keys.back().time) {
        lerp = 1.0;
        return evaluateKey(keys.back().post, current);
    }
    size_t index = 0;
    while (index + 1 < keys.size() && keys[index + 1].time <= time) {
        ++index;
    }
    const AnimationKey& from = keys[index];
    const AnimationKey& to = keys[index + 1];
    if (from.lerp == LerpMode::Step) {
        return evaluateKey(from.post, current);
    }
    float span = to.time - from.time;
    float t = span > 0.0f ? static_cast<float>((time - from.time) / span) : 0.0f;
    lerp = t;
    std::array<float, 3> a = evaluateKey(from.post, current);
    std::array<float, 3> b = evaluateKey(to.pre, current);
    std::array<float, 3> out {};
    if (from.lerp == LerpMode::CatmullRom || to.lerp == LerpMode::CatmullRom) {
        std::array<float, 3> before = index > 0 ? evaluateKey(keys[index - 1].post, current) : a;
        std::array<float, 3> after = index + 2 < keys.size() ? evaluateKey(keys[index + 2].pre, current) : b;
        float t2 = t * t;
        float t3 = t2 * t;
        for (size_t axis = 0; axis < 3; ++axis) {
            out[axis] = 0.5f * ((2.0f * a[axis]) + (-before[axis] + b[axis]) * t + (2.0f * before[axis] - 5.0f * a[axis] + 4.0f * b[axis] - after[axis]) * t2 + (-before[axis] + 3.0f * a[axis] - 3.0f * b[axis] + after[axis]) * t3);
        }
        return out;
    }
    for (size_t axis = 0; axis < 3; ++axis) {
        out[axis] = a[axis] + (b[axis] - a[axis]) * t;
    }
    return out;
}

bool EntityAnimator::playClip(const std::string& key, const AnimationClip& clip, double weight)
{
    ClipState& state = clipStates[key];
    if (state.frame != 0 && state.frame + 1 < frame) {
        state = ClipState {};
    }
    if (state.frame != frame) {
        bool fresh = state.frame == 0;
        if (fresh) {
            state.startWait = evaluate(clip.startDelay, 0.0);
        }
        state.frame = frame;
        if (state.startWait > 0.0) {
            state.startWait -= deltaTime;
            if (state.startWait > 0.0) {
                state.finished = false;
                return false;
            }
            state.startWait = 0.0;
        }
        double previous = state.time;
        if (state.loopWait > 0.0) {
            state.loopWait -= deltaTime;
            if (state.loopWait <= 0.0) {
                state.loopWait = 0.0;
                state.time = 0.0;
                previous = 0.0;
                fresh = true;
            }
        } else if (!clip.animTimeUpdate.empty()) {
            animTime = state.time;
            scope.temps.clear();
            state.time = clip.animTimeUpdate.run(scope);
        } else {
            state.time += deltaTime;
        }
        if (clip.length > 0.0f && state.time >= clip.length) state.finished = true;
        bool delayedLoop = clip.loop == LoopMode::Loop && !clip.loopDelay.empty() && clip.length > 0.0f;
        if (delayedLoop && state.loopWait <= 0.0 && state.time >= clip.length) {
            double delay = evaluate(clip.loopDelay, 0.0);
            if (delay > 0.0) {
                state.time = clip.length;
                state.loopWait = delay;
            } else {
                state.time = std::fmod(state.time, static_cast<double>(clip.length));
                previous = -1.0;
            }
        }
        for (const auto& [time, scripts] : clip.timeline) {
            double at = time;
            if (clip.loop == LoopMode::Loop && clip.length > 0.0f && state.loopWait <= 0.0 && !delayedLoop) {
                double cycle = std::floor(state.time / clip.length) * clip.length;
                at += cycle;
            }
            if ((at > previous || (fresh && at == 0.0)) && at <= state.time) {
                runScripts(scripts);
            }
        }
    }
    if (state.startWait > 0.0) {
        return false;
    }
    double sampleTime = state.time;
    bool finished = clip.loop == LoopMode::Loop && state.finished;
    if (clip.length > 0.0f) {
        if (clip.loop == LoopMode::Loop) {
            sampleTime = state.loopWait > 0.0 ? static_cast<double>(clip.length) : std::fmod(state.time, static_cast<double>(clip.length));
        } else if (state.time >= clip.length) {
            finished = true;
            sampleTime = clip.length;
            if (clip.loop == LoopMode::Once) {
                state.finished = true;
                return true;
            }
        }
    } else {
        finished = clip.loop != LoopMode::Loop;
    }
    state.finished = finished;
    animTime = sampleTime;
    double blend = weight;
    if (!clip.blendWeight.empty()) {
        scope.temps.clear();
        blend *= clip.blendWeight.run(scope);
    }
    if (blend == 0.0) {
        return finished;
    }
    float w = static_cast<float>(blend);
    // These legacy wolf clips set absolute bone coordinates relative to the
    // old 24-pixel model origin, rather than offsets from the geometry.
    bool legacyWolfPose = current.identifier == "minecraft:wolf" && activeLibrary
        && (&clip == activeLibrary->clip("animation.wolf.setup") || &clip == activeLibrary->clip("animation.wolf.sitting"));
    bool guardianAbsolutePosition = (current.identifier == "minecraft:guardian" || current.identifier == "minecraft:elder_guardian") && activeLibrary
        && (&clip == activeLibrary->clip("animation.guardian.spikes") || &clip == activeLibrary->clip("animation.guardian.move_eye"));
    bool polarBearMove = current.identifier == "minecraft:polar_bear" && activeLibrary
        && &clip == activeLibrary->clip("animation.polarbear.move");
    bool endermanAbsolutePosition = current.identifier == "minecraft:enderman" && activeLibrary
        && (&clip == activeLibrary->clip("animation.enderman.base_pose") || &clip == activeLibrary->clip("animation.enderman.base_pose_v1.0")
            || &clip == activeLibrary->clip("animation.enderman.scary_face") || &clip == activeLibrary->clip("animation.enderman.scary_face_v1.0"));
    for (const AnimationBone& track : clip.bones) {
        auto found = boneIndex.find(track.bone);
        if (found == boneIndex.end()) {
            continue;
        }
        BonePose& pose = poses[found->second];
        const EntityBone& bone = (*activeBones)[found->second];
        if (clip.overridePrevious) {
            pose = BonePose {};
        }
        if (track.rotation.present()) {
            // "this" is the value the earlier animations left, in the pack's own signs.
            std::array<float, 3> inherited { -pose.rotation[0], -pose.rotation[1], pose.rotation[2] };
            if (legacyWolfPose) {
                inherited[0] -= bone.rotation[0];
                inherited[1] -= bone.rotation[1];
                inherited[2] += bone.rotation[2];
            }
            std::array<float, 3> value = sample(track.rotation, sampleTime, inherited);
            pose.rotation[0] -= value[0] * w;
            pose.rotation[1] -= value[1] * w;
            pose.rotation[2] += value[2] * w;
            if (track.rotationRelativeToEntity) {
                pose.relativeRotation = true;
            }
        }
        if (track.position.present()) {
            std::array<float, 3> inherited { -pose.position[0], pose.position[1], pose.position[2] };
            if (legacyWolfPose || (polarBearMove && track.bone == "body")) {
                inherited[0] -= bone.pivot[0];
                inherited[1] += bone.pivot[1] - 24.0f;
                inherited[2] += bone.pivot[2];
            } else if (guardianAbsolutePosition) {
                inherited[0] -= bone.pivot[0];
                inherited[1] += bone.pivot[1];
                inherited[2] += bone.pivot[2];
            } else if (endermanAbsolutePosition) {
                std::array<float, 3> parent { 0.0f, 24.0f, 0.0f };
                if (bone.parent >= 0 && size_t(bone.parent) < activeBones->size()) {
                    parent = (*activeBones)[size_t(bone.parent)].pivot;
                }
                inherited[0] -= bone.pivot[0] - parent[0];
                inherited[1] += bone.pivot[1] - parent[1];
                inherited[2] += bone.pivot[2] - parent[2];
            }
            std::array<float, 3> value = sample(track.position, sampleTime, inherited);
            pose.position[0] -= value[0] * w;
            pose.position[1] += value[1] * w;
            pose.position[2] += value[2] * w;
        }
        if (track.scale.present()) {
            std::array<float, 3> value = sample(track.scale, sampleTime, pose.scale);
            for (size_t axis = 0; axis < 3; ++axis) {
                pose.scale[axis] *= 1.0f + (value[axis] - 1.0f) * w;
            }
        }
    }
    return finished;
}

double EntityAnimator::evaluate(const molang::Script& script, double fallback)
{
    if (script.empty()) {
        return fallback;
    }
    scope.temps.clear();
    return script.run(scope);
}

void EntityAnimator::updateVariables(const ControllerState& state)
{
    for (const ControllerVariable& variable : state.variables) {
        double value = evaluate(variable.input, 0.0);
        if (!variable.remap.empty()) {
            value = sampleCurve(variable.remap, value);
        }
        variables[variable.name] = value;
    }
}

void EntityAnimator::playState(const std::string& key, const ControllerState& state, double weight, int depth, bool& all, bool& any)
{
    all = true;
    any = false;
    for (const auto& [alias, weightScript] : state.animations) {
        double factor = 1.0;
        if (!weightScript.empty()) {
            scope.temps.clear();
            factor = weightScript.run(scope);
        }
        if (factor <= 0.0) {
            continue;
        }
        bool finished = false;
        play(alias, weight * factor, depth + 1, &finished);
        all = all && finished;
        any = any || finished;
    }
    (void)key;
}

void EntityAnimator::playController(const std::string& key, const AnimationController& controller, double weight, int depth)
{
    ControllerRuntime& runtime = controllerStates[key];
    if (!runtime.entered) {
        runtime.entered = true;
        runtime.state = controller.initialState;
        if (auto found = controller.states.find(runtime.state); found != controller.states.end()) {
            runScripts(found->second.onEntry);
        }
    }
    auto current = controller.states.find(runtime.state);
    if (current == controller.states.end()) {
        return;
    }
    finishedAll = runtime.allFinished;
    finishedAny = runtime.anyFinished;
    updateVariables(current->second);
    for (const auto& [target, condition] : current->second.transitions) {
        auto next = controller.states.find(target);
        if (next == controller.states.end() || next == current) {
            continue;
        }
        scope.temps.clear();
        if (condition.run(scope) == 0.0) {
            continue;
        }
        runScripts(current->second.onExit);
        runtime.previous = runtime.state;
        runtime.blendLength = current->second.blendTransition;
        runtime.blendLeft = runtime.blendLength;
        runtime.state = target;
        runtime.allFinished = false;
        runtime.anyFinished = false;
        for (const auto& animation : next->second.animations) {
            clipStates.erase(animation.first);
        }
        runScripts(next->second.onEntry);
        current = next;
        break;
    }
    auto previous = controller.states.end();
    double factor = 1.0;
    if (runtime.blendLeft > 0.0 && runtime.blendLength > 0.0) {
        previous = controller.states.find(runtime.previous);
        double elapsed = runtime.blendLength - runtime.blendLeft;
        if (previous != controller.states.end() && !previous->second.blendCurve.empty()) {
            factor = 1.0 - std::clamp(sampleCurve(previous->second.blendCurve, elapsed), 0.0, 1.0);
        } else {
            factor = elapsed / runtime.blendLength;
        }
        runtime.blendLeft = std::max(0.0, runtime.blendLeft - deltaTime);
        if (previous == current) {
            previous = controller.states.end();
        }
    }
    bool all = false;
    bool any = false;
    if (previous == controller.states.end()) {
        playState(key, current->second, weight * factor, depth, all, any);
    } else if (!previous->second.blendShortestPath) {
        bool previousAll = false;
        bool previousAny = false;
        playState(key, previous->second, weight * (1.0 - factor), depth, previousAll, previousAny);
        playState(key, current->second, weight * factor, depth, all, any);
    } else {
        std::vector<BonePose> base = poses;
        bool previousAll = false;
        bool previousAny = false;
        playState(key, previous->second, weight, depth, previousAll, previousAny);
        std::vector<BonePose> from = poses;
        poses = base;
        playState(key, current->second, weight, depth, all, any);
        float t = static_cast<float>(factor);
        for (size_t index = 0; index < poses.size(); ++index) {
            BonePose& pose = poses[index];
            const BonePose& start = from[index];
            for (size_t axis = 0; axis < 3; ++axis) {
                float delta = static_cast<float>(wrapDegrees(pose.rotation[axis] - start.rotation[axis]));
                pose.rotation[axis] = start.rotation[axis] + delta * t;
                pose.position[axis] = start.position[axis] + (pose.position[axis] - start.position[axis]) * t;
                pose.scale[axis] = start.scale[axis] + (pose.scale[axis] - start.scale[axis]) * t;
            }
            pose.relativeRotation = t < 0.5f ? start.relativeRotation : pose.relativeRotation;
        }
    }
    runtime.allFinished = all;
    runtime.anyFinished = any;
}

void EntityAnimator::play(const std::string& alias, double weight, int depth, bool* finished)
{
    if (depth > MaxDepth || !activeLibrary) {
        return;
    }
    std::string name = alias;
    if (activeScripts) {
        if (auto found = activeScripts->aliases.find(alias); found != activeScripts->aliases.end()) {
            name = found->second;
        }
    }
    if (const AnimationController* controller = activeLibrary->controller(name)) {
        playController(alias, *controller, weight, depth);
        if (finished) {
            *finished = controllerStates[alias].allFinished;
        }
        return;
    }
    const AnimationClip* clip = activeLibrary->clip(name);
    if (!clip && current.extraLibrary) {
        clip = current.extraLibrary->clip(name);
    }
    if (clip) {
        bool done = playClip(alias, *clip, weight);
        if (finished) {
            *finished = done;
        }
    }
}

std::vector<EntityBone> poseBonesForGeometry(const std::vector<EntityBone>& combined, const std::vector<EntityBone>& selected)
{
    std::vector<EntityBone> output = combined;
    std::unordered_map<std::string, int32_t> indices;
    for (size_t index = 0; index < combined.size(); ++index) {
        indices.emplace(lowercase(combined[index].name), static_cast<int32_t>(index));
    }
    for (const EntityBone& bone : selected) {
        auto target = indices.find(lowercase(bone.name));
        if (target == indices.end()) continue;
        EntityBone replacement = bone;
        replacement.parent = -1;
        if (bone.parent >= 0 && size_t(bone.parent) < selected.size()) {
            auto parent = indices.find(lowercase(selected[size_t(bone.parent)].name));
            if (parent != indices.end()) replacement.parent = parent->second;
        }
        output[size_t(target->second)] = std::move(replacement);
    }
    return output;
}

void EntityAnimator::update(const EntityScripts* scripts, const AnimationLibrary* library, const std::vector<EntityBone>& bones, const AnimationInput& input)
{
    activeScripts = scripts;
    activeLibrary = library;
    bool rigChanged = activeBones != &bones;
    activeBones = &bones;
    current = input;
    if (!initialized) {
        firstSeen = input.now;
        lastUpdate = input.now;
        lastPosition = { input.x, input.y, input.z };
        lastYaw = input.yaw;
        variables["gliding_speed_value"] = 1.0;
    }
    if (rigChanged || boneIndex.size() != bones.size()) {
        boneIndex.clear();
        for (size_t index = 0; index < bones.size(); ++index) {
            boneIndex.emplace(lowercase(bones[index].name), static_cast<int32_t>(index));
        }
    }
    deltaTime = std::clamp(input.now - lastUpdate, 0.0, 0.25);
    lastUpdate = input.now;

    tickClock += deltaTime;
    int ticks = 0;
    while (tickClock >= TickSeconds && ticks < MaxCatchUpTicks) {
        tickClock -= TickSeconds;
        ++ticks;
    }
    tickClock = std::min(tickClock, TickSeconds);
    if (ticks > 0) {
        std::array<double, 3> moved { input.x - lastPosition[0], input.y - lastPosition[1], input.z - lastPosition[2] };
        if (moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2] > 64.0) {
            moved = {};
        }
        velocity = { moved[0] / ticks / TickSeconds, moved[1] / ticks / TickSeconds, moved[2] / ticks / TickSeconds };
        double step = std::sqrt(moved[0] * moved[0] + moved[2] * moved[2]) / ticks;
        double target = step > 0.0 ? std::min(step * 4.0, 1.0)
            : std::min(std::abs(wrapDegrees(input.yaw - lastYaw)) / ticks * 0.05, 0.5);
        for (int tick = 0; tick < ticks; ++tick) {
            previousLimbAmount = limbAmount;
            limbAmount = input.flags[0] & (uint64_t(1) << 2) ? 0.0 : limbAmount + (target - limbAmount) * 0.4;
            limbDistance += limbAmount;
        }
        double stride = step > 0.05 ? step * 1.8 : 0.0;
        previousWalkDistance = walkDistance + stride * (ticks - 1);
        walkDistance += stride * ticks;
        lastPosition = { input.x, input.y, input.z };
    }
    yawSpeed = deltaTime > 0.0 ? wrapDegrees(input.yaw - lastYaw) / deltaTime : 0.0;
    if (input.tickPositionDelta) {
        for (size_t axis = 0; axis < 3; ++axis) velocity[axis] = (*input.tickPositionDelta)[axis] / TickSeconds;
    }
    lastYaw = input.yaw;

    scope.variables = &variables;
    scope.queries = this;
    if (!initialized) {
        initialized = true;
        randomState = input.randomSeed | 1;
        if (scripts) {
            runScripts(scripts->initialize);
        }
    }
    for (const auto& [name, value] : input.engineVariables) {
        variables[name] = value;
    }
    for (const auto& [name, value] : input.contextVariables) {
        scope.context[name] = value;
    }
    bool horse = input.identifier == "minecraft:horse" || input.identifier == "minecraft:donkey" || input.identifier == "minecraft:mule"
        || input.identifier == "minecraft:zombie_horse" || input.identifier == "minecraft:skeleton_horse";
    if (input.identifier == "minecraft:llama" || input.identifier == "minecraft:trader_llama") {
        static constexpr std::string_view Colors[] = {
            "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
            "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
        };
        std::string_view item = input.armorItems[4];
        double decoration = 0.0;
        if (item.starts_with("minecraft:") && item.ends_with("_carpet")) {
            std::string_view color = item.substr(10, item.size() - 17);
            for (size_t index = 0; index < std::size(Colors); ++index) {
                if (color == Colors[index]) decoration = double(index + 1);
            }
        }
        variables["decortextureindex"] = decoration;
    }
    if (input.identifier == "minecraft:cod" || input.identifier == "minecraft:salmon" || input.identifier == "minecraft:pufferfish" || input.identifier == "minecraft:tropicalfish") {
        float speed = std::sqrt(input.nativeVelocity[0] * input.nativeVelocity[0] + input.nativeVelocity[1] * input.nativeVelocity[1] + input.nativeVelocity[2] * input.nativeVelocity[2]);
        for (int tick = 0; tick < ticks; ++tick) {
            fishPhase[1] = fishPhase[0];
            fishPhase[0] += 1.0f + speed * 0.1f;
        }
        variables["animationamount"] = fishPhase[0];
        variables["animationamountprev"] = fishPhase[1];
    }
    if (auto target = input.metadataQueries.find("has_target"); target != input.metadataQueries.end()) variables["has_target"] = target->second;
    if (horse) {
        for (int tick = 0; tick < ticks; ++tick) {
            horseStand = (input.flags[0] & (uint64_t(1) << 39))
                ? std::min(horseStand + (1.0f - horseStand) * 0.4f + 0.05f, 1.0f)
                : std::max(horseStand + (horseStand * horseStand * horseStand * 0.8f - horseStand) * 0.6f - 0.05f, 0.0f);
            randomState ^= randomState << 13;
            randomState ^= randomState >> 7;
            randomState ^= randomState << 17;
            if (randomState % 200 == 0) horseTailTicks = 1;
            if (horseTailTicks && ++horseTailTicks > 8) horseTailTicks = 0;
        }
        variables["stand_anim"] = horseStand;
        variables["shake_tail"] = horseTailTicks > 0 ? 1.0 : 0.0;
        variables["open_mouth"] = (input.horseFlags >> 7) & 1;
    }
    if (input.identifier == "minecraft:tropicalfish") {
        int family = static_cast<uint8_t>(input.variant) != 0 ? 1 : 0;
        variables["tropicalfish.base"] = family;
        variables["tropicalfish.pattern"] = (input.markVariant >= 0 && input.markVariant < 6 ? input.markVariant : 0) + family * 6;
    }
    if (input.identifier == "minecraft:ender_dragon") {
        if (frame == 0) dragonHistory.fill({ wrapDegrees(input.yaw), input.y });
        for (int tick = 0; tick < ticks; ++tick) {
            dragonHistoryIndex = (dragonHistoryIndex + 1) % dragonHistory.size();
            dragonHistory[dragonHistoryIndex] = { wrapDegrees(input.yaw), input.y };
            double dx = velocity[0] * TickSeconds, dy = velocity[1] * TickSeconds, dz = velocity[2] * TickSeconds;
            dragonFlap += (input.flags[0] & (uint64_t(1) << 24)) ? 0.1 : std::exp2(std::clamp(dy, -16.0, 16.0)) * 0.2 / (std::hypot(dx, dz) * 10.0 + 1.0);
        }
        if (input.deathTicks > 0.0f) dragonFlap = 0.0;
        for (size_t offset = 0; offset < 24; ++offset) {
            const auto& history = dragonHistory[(dragonHistoryIndex + dragonHistory.size() - offset - (input.deathTicks > 0.0f ? 1 : 0)) % dragonHistory.size()];
            variables["historical_frame_" + std::to_string(offset) + ".rot_y"] = history[0];
            variables["historical_frame_" + std::to_string(offset) + ".pos_y"] = history[1];
        }
    }
    ++frame;
    poses.assign(bones.size(), BonePose {});
    animTime = 0.0;
    if (scripts) {
        runScripts(scripts->preAnimation);
        for (const auto& [alias, condition] : scripts->animate) {
            if (!condition.empty()) {
                scope.temps.clear();
                if (condition.run(scope) == 0.0) {
                    continue;
                }
            }
            play(alias, 1.0, 0, nullptr);
        }
        for (const std::string& alias : input.extraAnimations) {
            play(alias, 1.0, 0, nullptr);
        }
        modelScale = 1.0f;
        if (!scripts->scale.empty()) {
            scope.temps.clear();
            modelScale = static_cast<float>(scripts->scale.run(scope));
            if (!(modelScale > 0.0f)) {
                modelScale = 1.0f;
            }
        }
    }

    boneMatrices.assign(bones.size(), identity());
    std::vector<uint8_t> done(bones.size(), 0);
    for (size_t start = 0; start < bones.size(); ++start) {
        std::vector<size_t> chain;
        for (int32_t index = static_cast<int32_t>(start); index >= 0 && !done[index] && chain.size() <= bones.size(); index = bones[index].parent) {
            chain.push_back(static_cast<size_t>(index));
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            size_t index = *it;
            const EntityBone& bone = bones[index];
            const BonePose& pose = poses[index];
            std::array<float, 3> rotation { bone.rotation[0] + pose.rotation[0], bone.rotation[1] + pose.rotation[1], bone.rotation[2] + pose.rotation[2] };
            Row local = localTransform(bone.pivot, pose.position, rotation, pose.scale);
            boneMatrices[index] = bone.parent >= 0 && done[bone.parent] ? multiply(boneMatrices[bone.parent], local) : local;
            if (pose.relativeRotation && bone.parent >= 0 && done[bone.parent]) {
                Row& world = boneMatrices[index];
                std::array<float, 3> anchor {};
                for (int row = 0; row < 3; ++row) {
                    anchor[row] = world[row * 4 + 0] * bone.pivot[0] + world[row * 4 + 1] * bone.pivot[1] + world[row * 4 + 2] * bone.pivot[2] + world[row * 4 + 3];
                }
                for (int row = 0; row < 3; ++row) {
                    float turned = 0.0f;
                    for (int column = 0; column < 3; ++column) {
                        world[row * 4 + column] = local[row * 4 + column];
                        turned += local[row * 4 + column] * bone.pivot[column];
                    }
                    world[row * 4 + 3] = anchor[row] - turned;
                }
            }
            done[index] = 1;
        }
    }
    if (input.identifier != "minecraft:player" && (input.name == "Dinnerbone" || input.name == "Grumm")) {
        auto height = input.metadataQueries.find("upside_down_height");
        if (height != input.metadataQueries.end() && std::isfinite(height->second) && height->second > 0.0 && std::isfinite(modelScale) && modelScale > 0.0f) {
            Row upsideDown { -1, 0, 0, 0, 0, -1, 0, float(height->second / modelScale), 0, 0, 1, 0 };
            for (auto& matrix : boneMatrices) matrix = multiply(upsideDown, matrix);
        }
    }
    if (input.deathTicks > 0.0f && input.identifier != "minecraft:ender_dragon") {
        float angle = std::sqrt(std::clamp(input.deathTicks / 20.0f, 0.0f, 1.0f)) * float(Pi * 0.5);
        Row tilt { std::cos(angle), -std::sin(angle), 0, 0, std::sin(angle), std::cos(angle), 0, 0, 0, 0, 1, 0 };
        for (auto& matrix : boneMatrices) matrix = multiply(tilt, matrix);
    }

}

void EntityAnimator::setRenderContext(const AnimationInput& input, double now, float partialTick)
{
    current.now = now;
    current.worldTime = input.worldTime;
    current.cameraX = input.cameraX;
    current.cameraY = input.cameraY;
    current.cameraZ = input.cameraZ;
    current.cameraYaw = input.cameraYaw;
    current.cameraPitch = input.cameraPitch;
    current.frameAlpha = partialTick;
    current.hurtTime = input.hurtTime;
    current.deathTicks = input.deathTicks;
    current.onFireTime = input.onFireTime;
}

double EntityAnimator::query(const std::string& name, std::span<const double> arguments)
{
    if (name == "swim_amount") return current.swimAmount;
    if (name == "is_in_water") {
        if (current.inWater) return *current.inWater ? 1.0 : 0.0;
        const std::string& id = current.identifier;
        bool aquatic = id == "minecraft:cod" || id == "minecraft:salmon" || id == "minecraft:pufferfish"
            || id == "minecraft:tropicalfish" || id == "minecraft:squid" || id == "minecraft:glow_squid"
            || id == "minecraft:guardian" || id == "minecraft:elder_guardian" || id == "minecraft:dolphin" || id == "minecraft:axolotl";
        return ((current.flags[0] >> 57) & 1) || (aquatic && !current.onGround) ? 1.0 : 0.0;
    }
    auto argument = [&](size_t index) {
        return index < arguments.size() ? arguments[index] : 0.0;
    };
    if (name == "property" || name == "has_property") {
        if (arguments.empty()) return 0.0;
        auto found = current.properties.find(molang::stringOf(argument(0)));
        if (name == "has_property") return found != current.properties.end() ? 1.0 : 0.0;
        return found != current.properties.end() ? found->second : 0.0;
    }
    double speed = std::sqrt(velocity[0] * velocity[0] + velocity[2] * velocity[2]);
    if (name == "key_frame_lerp_time") return variables["key_frame_lerp_time"];
    if (name == "anim_time") {
        return animTime;
    }
    if (name == "life_time") {
        return current.now - firstSeen;
    }
    if (name == "delta_time") {
        return deltaTime;
    }
    double partialTick = current.frameAlpha.value_or(float(tickClock / TickSeconds));
    if (name == "frame_alpha") {
        return current.frameAlpha.value_or(float(partialTick));
    }
    if (name == "shake_time") return current.shakeTime;
    if (name == "is_attached_to_entity") return current.attachedToEntity ? 1.0 : 0.0;
    // The game counts a used item's duration down from its maximum.
    if (name == "main_hand_item_max_duration") {
        return itemMaxUseTicks(current.mainHandItem);
    }
    if (name == "main_hand_item_use_duration") {
        return current.itemUseTicks > 0.0 ? itemMaxUseTicks(current.mainHandItem) - std::floor(current.itemUseTicks) : 0.0;
    }
    if (name == "get_animation_frame") {
        return itemUseAnimationFrame(current.mainHandItem, current.itemUseTicks);
    }
    if (name == "modified_distance_moved") {
        return limbDistance - limbAmount * (1.0 - partialTick);
    }
    if (name == "modified_move_speed") {
        return previousLimbAmount + (limbAmount - previousLimbAmount) * partialTick;
    }
    if (name == "ground_speed") {
        return speed;
    }
    if (name == "vertical_speed") {
        return velocity[1];
    }
    if (name == "walk_distance" || name == "distance_moved") {
        return previousWalkDistance + (walkDistance - previousWalkDistance) * partialTick;
    }
    if (name == "distance_from_camera" || name == "rotation_to_camera") {
        double dx = current.cameraX - current.x;
        double dy = current.cameraY - current.y;
        double dz = current.cameraZ - current.z;
        double flat = std::sqrt(dx * dx + dz * dz);
        if (name == "distance_from_camera") {
            return std::sqrt(flat * flat + dy * dy);
        }
        constexpr double Degrees = 180.0 / 3.14159265358979;
        return argument(0) == 0.0 ? std::atan2(-dy, flat) * Degrees : std::atan2(-dx, dz) * Degrees;
    }
    if (name == "camera_rotation") {
        return argument(0) == 0.0 ? current.cameraPitch : current.cameraYaw;
    }
    auto flag = [&](int bit) {
        return (current.flags[static_cast<size_t>(bit) / 64] >> (bit % 64)) & 1 ? 1.0 : 0.0;
    };
    static const std::unordered_map<std::string, int> flagQueries = {
        {"is_on_fire", 0},
        {"is_sneaking", 1},
        {"is_riding", 2},
        {"is_sprinting", 3},
        {"is_using_item", 4},
        {"is_invisible", 5},
        {"is_tempted", 6},
        {"is_in_love", 7},
        {"is_saddled", 8},
        {"is_powered", 9},
        {"is_ignited", 10},
        {"is_baby", 11},
        {"is_converting", 12},
        {"is_critical", 13},
        {"is_immobile", 16},
        {"is_silent", 17},
        {"is_wall_climbing", 18},
        {"can_climb", 19},
        {"can_swim", 20},
        {"can_fly", 21},
        {"can_walk", 22},
        {"can_damage_nearby_mobs", 56},
        {"is_resting", 23},
        {"is_sitting", 24},
        {"is_angry", 25},
        {"is_interested", 26},
        {"is_charged", 27},
        {"is_tamed", 28},
        {"is_orphaned", 29},
        {"is_leashed", 30},
        {"is_sheared", 31},
        {"is_gliding", 32},
        {"is_elder", 33},
        {"show_bottom", 38},
        {"is_breathing", 35},
        {"is_chested", 36},
        {"is_stackable", 37},
        {"is_rearing", 39},
        {"is_idling", 41},
        {"is_casting", 42},
        {"is_charging", 43},
        {"can_power_jump", 45},
        {"can_dash", 46},
        {"is_fire_immune", 50},
        {"is_dancing", 51},
        {"is_enchanted", 52},
        {"is_transforming", 55},
        {"is_spin_attacking", 56},
        {"is_swimming", 57},
        {"is_bribed", 58},
        {"is_pregnant", 59},
        {"is_laying_egg", 60},
        {"is_eating", 63},
        {"is_laying_down", 64},
        {"is_sneezing", 65},
        {"is_trusting", 66},
        {"is_rolling", 67},
        {"is_scared", 68},
        {"is_in_scaffolding", 69},
        {"is_blocking", 72},
        {"blocking", 72},
        {"is_sleeping", 76},
        {"is_illager_captain", 82},
        {"is_stunned", 83},
        {"is_roaring", 84},
        {"is_delayed_attacking", 85},
        {"is_avoiding_mobs", 86},
        {"is_avoiding_block", 87},
        {"is_facing_target_to_range_attack", 88},
        {"is_in_ui", 90},
        {"is_stalking", 91},
        {"is_emoting", 92},
        {"is_celebrating", 93},
        {"is_admiring", 94},
        {"is_celebrating_special", 95},
        {"is_ram_attacking", 97},
        {"is_playing_dead", 98},
        {"is_croaking", 101},
        {"is_eating_mob", 102},
        {"is_jump_goal_jumping", 103},
        {"is_emerging", 104},
        {"is_sniffing", 105},
        {"is_digging", 106},
        {"is_sonic_booming", 107},
        {"has_dash_cooldown", 108},
        {"is_scenting", 110},
        {"is_rising", 111},
        {"is_feeling_happy", 112},
        {"is_searching", 113},
        {"is_crawling", 114},
        {"timer_flag_1", 115},
        {"timer_flag_2", 116},
        {"timer_flag_3", 117},
    };
    if (name == "is_grazing" && current.identifier == "minecraft:sheep") {
        auto until = current.metadataQueries.find("grazing_until");
        return until != current.metadataQueries.end() && current.now < until->second ? 1.0 : 0.0;
    }
    if (name == "is_grazing" && (current.identifier == "minecraft:horse" || current.identifier == "minecraft:donkey" || current.identifier == "minecraft:mule"
        || current.identifier == "minecraft:zombie_horse" || current.identifier == "minecraft:skeleton_horse")) return (current.horseFlags >> 5) & 1;
    if (auto found = flagQueries.find(name); found != flagQueries.end()) {
        return flag(found->second);
    }
    if (name == "variant") {
        return current.variant;
    }
    if (name == "mark_variant") {
        return current.markVariant;
    }
    if (name == "color") {
        return current.color;
    }
    if (name == "skin_id") {
        return current.skinId;
    }
    if (name == "is_moving") {
        return flag(34) > 0.0 || speed > 0.05 ? 1.0 : 0.0;
    }
    if (name == "is_on_ground") {
        return current.onGround ? 1.0 : 0.0;
    }
    if (name == "hurt_time") {
        return current.hurtTime;
    }
    if (auto found = current.metadataQueries.find(name); found != current.metadataQueries.end()) return found->second;
    if (name == "death_ticks") return current.deathTicks;
    if (name == "on_fire_time") return current.onFireTime;
    if (name == "overlay_alpha") return current.hurtTime > 0.0f || current.deathTicks > 0.0f ? 0.5 : 0.0;
    if (name == "wing_flap_position") return dragonFlap;
    if (name == "is_shield_powered") return current.maxHealth > 0.0f && current.health <= current.maxHealth * 0.5f ? 1.0 : 0.0;
    if (name == "tail_angle") {
        if (current.identifier != "minecraft:wolf") return 0.0;
        if (flag(25)) return 1.539380431175232;
        if (!flag(28) || current.maxHealth <= 0.0f) return 0.6283185482025146;
        return (current.health / current.maxHealth * 0.4 + 0.15) * Pi;
    }
    if (name == "is_alive") return current.health > 0.0f && current.deathTicks == 0.0f ? 1.0 : 0.0;
    if (name == "has_collision" || name == "has_gravity") {
        return 1.0;
    }
    if (name == "head_x_rotation") return arguments.empty() ? 0.0 : current.pitch;
    if (name == "body_x_rotation" || name == "target_x_rotation" || name == "eye_target_x_rotation") {
        return current.pitch;
    }
    if (name == "target_y_rotation" && targetRotationIsAbsolute(current.identifier)) {
        return current.yaw;
    }
    if (name == "target_y_rotation" || name == "eye_target_y_rotation") {
        return std::clamp(wrapDegrees(current.headYaw - current.yaw), -85.0, 85.0);
    }
    if (name == "head_y_rotation") {
        return arguments.empty() ? 0.0 : std::clamp(wrapDegrees(current.headYaw - current.yaw), -std::abs(argument(0)), std::abs(argument(0)));
    }
    if (name == "body_y_rotation") {
        return current.yaw;
    }
    if (name == "yaw_speed") {
        return yawSpeed;
    }
    if (name == "time_of_day") {
        double day = std::fmod(current.worldTime / 24000.0, 1.0);
        return day < 0.0 ? day + 1.0 : day;
    }
    if (name == "time_stamp") {
        return std::floor(current.worldTime);
    }
    if (name == "all_animations_finished") {
        return finishedAll ? 1.0 : 0.0;
    }
    if (name == "any_animation_finished") {
        return finishedAny ? 1.0 : 0.0;
    }
    if (name == "position") {
        int axis = static_cast<int>(argument(0));
        return axis == 0 ? current.x : axis == 1 ? current.y : current.z;
    }
    if (name == "position_delta") {
        int axis = static_cast<int>(argument(0));
        return velocity[std::clamp(axis, 0, 2)] / 20.0;
    }
    if (name == "movement_direction") {
        int axis = std::clamp(static_cast<int>(argument(0)), 0, 2);
        double length = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
        return length > 1.0e-4 ? velocity[axis] / length : 0.0;
    }
    if (name == "scale") {
        return 1.0;
    }
    if (name == "health") {
        return current.health;
    }
    if (name == "max_health") {
        return current.maxHealth;
    }
    if (name == "get_name" || name == "get_nametag") {
        return molang::internString(current.name);
    }
    if (name == "armor_texture_slot" || name == "armor_color_slot" || name == "armor_damage_slot") {
        double slotValue = argument(0);
        if (arguments.empty() || !std::isfinite(slotValue) || slotValue < 0.0 || slotValue >= current.armorItems.size()) return 0.0;
        size_t slot = static_cast<size_t>(slotValue);
        if (slot == 1 && current.identifier == "minecraft:wolf") slot = 4;
        const std::string& item = current.armorItems[slot];
        if (name == "armor_damage_slot") return std::max(current.armorDamage[slot], 0);
        if (name == "armor_color_slot") {
            double channel = argument(1);
            if (arguments.size() < 2 || !std::isfinite(channel)) return 0.0;
            if (channel < 0.0 || channel >= 3.0) return 1.0;
            uint32_t rgb = current.armorColors[slot].value_or(item.starts_with("minecraft:leather_") ? 0xa06540u : 0xffffffu);
            return double((rgb >> (16 - static_cast<unsigned>(channel) * 8)) & 255) / 255.0;
        }
        if (slot == 1 && item == "minecraft:elytra") return 5.0;
        static constexpr std::pair<const char*, double> Materials[] = {
            { "minecraft:leather_", 1 }, { "minecraft:iron_", 2 }, { "minecraft:golden_", 3 },
            { "minecraft:gold_", 3 }, { "minecraft:diamond_", 4 }, { "minecraft:copper_", 5 }, { "minecraft:netherite_", 6 },
        };
        for (const auto& [prefix, value] : Materials) if (item.starts_with(prefix)) return value;
        return 0.0;
    }
    if (name == "is_item_name_any") {
        if (arguments.size() < 2) return 0.0;
        const std::string* item = nullptr;
        if (argument(0) == molang::internString("slot.weapon.mainhand")) item = &current.mainHandItem;
        if (argument(0) == molang::internString("slot.weapon.offhand")) item = &current.offHandItem;
        if (argument(0) == molang::internString("slot.armor.body")) item = &current.armorItems[4];
        if (!item || item->empty()) return 0.0;
        const double value = molang::internString(*item);
        for (size_t i = 1; i < arguments.size(); ++i) {
            if (arguments[i] == value) return 1.0;
        }
        return 0.0;
    }
    if (name == "is_item_equipped") {
        if (arguments.empty() || argument(0) == 0.0 || argument(0) == molang::internString("main_hand")) return !current.mainHandItem.empty();
        if (argument(0) == 1.0 || argument(0) == molang::internString("off_hand")) return !current.offHandItem.empty();
        return 0.0;
    }
    if (name == "get_equipped_item_name") {
        bool offHand = argument(0) == molang::internString("off_hand") || argument(0) == 1.0;
        const std::string& item = offHand ? current.offHandItem : current.mainHandItem;
        std::string shortName = item.substr(item.find(':') == std::string::npos ? 0 : item.find(':') + 1);
        if (shortName == "filled_map") {
            shortName = "map";
        }
        return molang::internString(shortName);
    }
    if (name == "get_actor_info_id" || name == "owner_identifier" || name == "identifier" || name == "entity_identifier") {
        return molang::internString(current.identifier);
    }
    if (name == "is_name_any" || name == "is_owner_identifier_any") {
        const std::string& text = name == "is_name_any" ? current.name : current.identifier;
        double value = molang::internString(text);
        for (double candidate : arguments) {
            if (candidate == value) {
                return 1.0;
            }
        }
        return 0.0;
    }
    return 0.0;
}

}
