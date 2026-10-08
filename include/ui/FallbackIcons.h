#pragma once

#include "ui/GameAssets.h"

#include <string_view>

namespace kestrel::ui {

/**
 * Kestrel's own pixel art for an HTML menu image, by its name without the hbui/ prefix, for builds that ship
 * without the game's HTML menus. Glyphs come out white so the screens' tints still apply. False when Kestrel
 * has no drawing for that image.
 */
bool fallbackIcon(std::string_view name, Bitmap& out);

}
