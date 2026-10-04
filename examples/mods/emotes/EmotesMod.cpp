#include "mod/Api.h"

using namespace kestrel::mod;

namespace {

// Each emote is a resource pack animation file. Rotations are in degrees and
// add to the player's own animations; the player bones are body, head,
// leftArm, rightArm, leftLeg and rightLeg.

constexpr const char* Wave = R"({
  "format_version": "1.10.0",
  "animations": {
    "animation.emotes_example.wave": {
      "animation_length": 2.0,
      "bones": {
        "rightArm": {
          "rotation": {
            "0.0": [0, 0, 0],
            "0.25": [0, 0, 150],
            "0.5": [0, 0, 120],
            "0.75": [0, 0, 160],
            "1.0": [0, 0, 120],
            "1.25": [0, 0, 160],
            "1.5": [0, 0, 120],
            "2.0": [0, 0, 0]
          }
        }
      }
    }
  }
})";

constexpr const char* Clap = R"({
  "format_version": "1.10.0",
  "animations": {
    "animation.emotes_example.clap": {
      "animation_length": 2.0,
      "bones": {
        "rightArm": {
          "rotation": {
            "0.0": [-70, 0, 0],
            "0.25": [-70, 25, 0],
            "0.5": [-70, 0, 0],
            "0.75": [-70, 25, 0],
            "1.0": [-70, 0, 0],
            "1.25": [-70, 25, 0],
            "1.5": [-70, 0, 0],
            "2.0": [0, 0, 0]
          }
        },
        "leftArm": {
          "rotation": {
            "0.0": [-70, 0, 0],
            "0.25": [-70, -25, 0],
            "0.5": [-70, 0, 0],
            "0.75": [-70, -25, 0],
            "1.0": [-70, 0, 0],
            "1.25": [-70, -25, 0],
            "1.5": [-70, 0, 0],
            "2.0": [0, 0, 0]
          }
        }
      }
    }
  }
})";

constexpr const char* Dance = R"({
  "format_version": "1.10.0",
  "animations": {
    "animation.emotes_example.dance": {
      "loop": true,
      "animation_length": 1.0,
      "bones": {
        "body": { "rotation": ["0", "math.sin(query.anim_time * 360) * 15", "0"] },
        "head": { "rotation": ["0", "math.sin(query.anim_time * 360) * -15", "math.sin(query.anim_time * 720) * 10"] },
        "rightArm": { "rotation": ["-90 + math.sin(query.anim_time * 720) * 40", "0", "20"] },
        "leftArm": { "rotation": ["-90 - math.sin(query.anim_time * 720) * 40", "0", "-20"] },
        "rightLeg": { "rotation": ["math.sin(query.anim_time * 360) * 25", "0", "0"] },
        "leftLeg": { "rotation": ["math.sin(query.anim_time * 360) * -25", "0", "0"] }
      }
    }
  }
})";

constexpr const char* Salute = R"({
  "format_version": "1.10.0",
  "animations": {
    "animation.emotes_example.salute": {
      "animation_length": 1.5,
      "bones": {
        "rightArm": {
          "rotation": {
            "0.0": [0, 0, 0],
            "0.3": [-130, -30, 0],
            "1.2": [-130, -30, 0],
            "1.5": [0, 0, 0]
          }
        },
        "head": {
          "rotation": {
            "0.0": [0, 0, 0],
            "0.3": [-10, 0, 0],
            "1.2": [-10, 0, 0],
            "1.5": [0, 0, 0]
          }
        }
      }
    }
  }
})";

}

/**
 * Adds four emotes to the emote wheel and a .emote command to play one by id.
 */
class EmotesExample : public Mod {
public:
    EmotesExample()
        : Mod({ .id = "emotes_example", .name = "Emotes Example", .version = "1.0.0" })
    {
    }

    void onEnable() override
    {
        emote({ .id = "emotes_example:wave", .name = "Wave", .animation = Wave });
        emote({ .id = "emotes_example:clap", .name = "Clap", .animation = Clap });
        emote({ .id = "emotes_example:dance", .name = "Dance", .animation = Dance });
        emote({ .id = "emotes_example:salute", .name = "Salute", .animation = Salute });
        command("emote", "Plays an emote by id, or stops the one playing", [this](CommandContext& context) {
            if (context.args.empty()) {
                emotes().stop();
                return;
            }
            emotes().play("emotes_example:" + context.args.front());
        });
    }
};

KESTREL_MOD(EmotesExample)
