#pragma once

#include "Core/NBT/Tag.h"

#include <string>

namespace kestrel::world {

/**
 * Evaluates a custom block permutation condition such as
 * "q.block_state('facing') == 2 && !query.block_state('open')" against the
 * states of one block. Unknown queries evaluate to zero.
 */
bool evaluateCondition(const std::string& expression, const Tag& states);

}
