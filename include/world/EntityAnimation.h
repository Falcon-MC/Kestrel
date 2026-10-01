#pragma once

#include "world/MolangScript.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace json {
struct Value;
}

namespace kestrel::world {

enum class LerpMode : uint8_t {
    Linear,
    Step,
    CatmullRom,
};

struct AnimationKey {
    float time = 0.0f;
    std::array<molang::Script, 3> pre;
    std::array<molang::Script, 3> post;
    LerpMode lerp = LerpMode::Linear;
};

struct AnimationChannel {
    std::vector<AnimationKey> keys;

    bool present() const
    {
        return !keys.empty();
    }
};

struct AnimationBone {
    std::string bone;
    AnimationChannel rotation;
    AnimationChannel position;
    AnimationChannel scale;
    bool rotationRelativeToEntity = false;
};

enum class LoopMode : uint8_t {
    Once,
    Loop,
    Hold,
};

/**
 * One animation clip: per bone rotation, position and scale channels made of
 * Molang keyframes, plus timeline scripts that fire when the clip passes them.
 */
struct AnimationClip {
    float length = 0.0f;
    LoopMode loop = LoopMode::Once;
    bool overridePrevious = false;
    molang::Script animTimeUpdate;
    molang::Script blendWeight;
    molang::Script startDelay;
    molang::Script loopDelay;
    std::vector<AnimationBone> bones;
    std::vector<std::pair<float, std::vector<molang::Script>>> timeline;
};

/**
 * A controller variable: its input is evaluated each frame, remapped through
 * the piecewise linear curve when one is given, and stored in the variable.
 */
struct ControllerVariable {
    std::string name;
    molang::Script input;
    std::vector<std::pair<float, float>> remap;
};

struct ControllerState {
    std::vector<std::pair<std::string, molang::Script>> animations;
    std::vector<std::pair<std::string, molang::Script>> transitions;
    std::vector<molang::Script> onEntry;
    std::vector<molang::Script> onExit;
    std::vector<ControllerVariable> variables;
    std::vector<std::pair<float, float>> blendCurve;
    float blendTransition = 0.0f;
    bool blendShortestPath = false;
};

struct AnimationController {
    std::string initialState = "default";
    std::map<std::string, ControllerState> states;
};

/**
 * Every animation and animation controller of the vanilla pack and the server
 * packs, by full name; later packs replace earlier definitions.
 */
class AnimationLibrary {
public:
    void parse(const json::Value& document);

    const AnimationClip* clip(const std::string& name) const;
    const AnimationController* controller(const std::string& name) const;

private:
    std::unordered_map<std::string, AnimationClip> clips;
    std::unordered_map<std::string, AnimationController> controllers;
};

/**
 * The scripts of one client entity definition: variable setup, per frame
 * scripts, the animations it plays with their conditions and its alias table.
 */
struct EntityScripts {
    std::vector<molang::Script> initialize;
    std::vector<molang::Script> preAnimation;
    std::vector<std::pair<std::string, molang::Script>> animate;
    std::unordered_map<std::string, std::string> aliases;
    molang::Script scale;
};

std::shared_ptr<EntityScripts> readEntityScripts(const json::Value& description);

struct EntityBone {
    std::string name;
    int32_t parent = -1;
    std::array<float, 3> pivot {};
    std::array<float, 3> rotation {};
    bool bound = false;
    // A texture mesh bone without a pivot of its own sits on the holder's bone of the same name.
    bool anchoredToHolder = false;
};

/**
 * Row major 3x4 affine transform in pixels.
 */
using BoneMatrix = std::array<float, 12>;

/**
 * Movement and look of one entity for a frame.
 */
struct AnimationInput {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;
    double now = 0.0;
    double worldTime = 0.0;
    std::array<uint64_t, 3> flags{};
    int variant = 0;
    int markVariant = 0;
    int color = 0;
    int skinId = 0;
    float health = 20.0f;
    float maxHealth = 20.0f;
    float hurtTime = 0.0f;
    bool onGround = true;
    float swimAmount = 0.0f;
    std::optional<bool> inWater;
    double cameraX = 0.0;
    double cameraY = 0.0;
    double cameraZ = 0.0;
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    std::string identifier;
    std::string name;
    std::string mainHandItem;
    std::string offHandItem;
    // Ticks the main hand item has been held in use, like a bow being drawn, 0 when it is not.
    double itemUseTicks = 0.0;
    std::vector<std::pair<std::string, double>> engineVariables;
    std::vector<std::pair<std::string, double>> contextVariables;
    // Animation aliases the engine plays besides the entity's own list, like the persona blink for animated faces.
    std::vector<std::string> extraAnimations;
};

/**
 * Whether the entity animates its full yaw through the target rotation
 * queries, so its world placement must not turn it again.
 */
bool targetRotationIsAbsolute(const std::string& identifier);

/**
 * Whether the entity is an item sprite turned toward the camera by its
 * animation, placed in the camera basis rather than by its body yaw.
 */
bool cameraFacingSprite(const std::string& identifier);

/**
 * The animation state of one entity: its Molang variables, controller states
 * and clip times. Each update runs the entity scripts, plays its animations
 * and yields one transform per bone, rest pose included.
 */
class EntityAnimator : public molang::QuerySource {
public:
    void update(const EntityScripts* scripts, const AnimationLibrary* library, const std::vector<EntityBone>& bones, const AnimationInput& input);

    const std::vector<BoneMatrix>& matrices() const
    {
        return boneMatrices;
    }

    float scale() const
    {
        return modelScale;
    }

    double query(const std::string& name, std::span<const double> arguments) override;

    /**
     * Runs one expression against this entity's variables and queries, as of
     * its last update.
     */
    double evaluate(const molang::Script& script);

private:
    struct ClipState {
        double time = 0.0;
        uint64_t frame = 0;
        double startWait = 0.0;
        double loopWait = 0.0;
        bool finished = false;
    };

    struct ControllerRuntime {
        std::string state;
        std::string previous;
        double blendLeft = 0.0;
        double blendLength = 0.0;
        bool entered = false;
        bool allFinished = false;
        bool anyFinished = false;
    };

    struct BonePose {
        std::array<float, 3> rotation {};
        std::array<float, 3> position {};
        std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
        bool relativeRotation = false;
    };

    void runScripts(const std::vector<molang::Script>& scripts);
    void play(const std::string& alias, double weight, int depth, bool* finished);
    void playController(const std::string& key, const AnimationController& controller, double weight, int depth);
    void playState(const std::string& key, const ControllerState& state, double weight, int depth, bool& all, bool& any);
    bool playClip(const std::string& key, const AnimationClip& clip, double weight);
    std::array<float, 3> sample(const AnimationChannel& channel, double time, const std::array<float, 3>& current);
    std::array<float, 3> evaluateKey(const std::array<molang::Script, 3>& values, const std::array<float, 3>& current);
    double evaluate(const molang::Script& script, double fallback);
    void updateVariables(const ControllerState& state);

    std::unordered_map<std::string, double> variables;
    std::unordered_map<std::string, ClipState> clipStates;
    std::unordered_map<std::string, ControllerRuntime> controllerStates;
    std::unordered_map<std::string, int32_t> boneIndex;
    std::vector<BonePose> poses;
    std::vector<BoneMatrix> boneMatrices;
    molang::Scope scope;
    const EntityScripts* activeScripts = nullptr;
    const AnimationLibrary* activeLibrary = nullptr;
    const std::vector<EntityBone>* activeBones = nullptr;
    AnimationInput current;
    uint64_t frame = 0;
    bool initialized = false;
    double firstSeen = 0.0;
    double lastUpdate = 0.0;
    double deltaTime = 0.0;
    double animTime = 0.0;
    double tickClock = 0.0;
    std::array<double, 3> lastPosition {};
    std::array<double, 3> velocity {};
    double previousLimbAmount = 0.0;
    double limbAmount = 0.0;
    double limbDistance = 0.0;
    double previousWalkDistance = 0.0;
    double walkDistance = 0.0;
    double lastYaw = 0.0;
    double yawSpeed = 0.0;
    bool finishedAll = false;
    bool finishedAny = false;
    float modelScale = 1.0f;
};

}
