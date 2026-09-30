#pragma once

#include <optional>
#include <string>

namespace kestrel::world {

/**
 * Whether a particle file is in the game's compiled binary form rather than
 * JSON text.
 */
bool isParticleBinary(const std::string& data);

/**
 * The JSON text of a particle effect stored in the game's compiled binary
 * form, written with the same keys a particles/*.json file uses; nothing when
 * the data holds a component the format reader does not know or is cut short.
 */
std::optional<std::string> particleBinaryToJson(const std::string& data);

}
