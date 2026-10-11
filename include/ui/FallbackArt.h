#pragma once

#include "ui/Skin.h"

#include <string_view>

namespace kestrel::ui {

// Procedural replacements for the installed HTML menu artwork. Unknown names stay missing.
bool drawFallbackArt(std::string_view name, Bitmap& bitmap);
BorderImage fallbackBorder(std::string_view component, std::string_view state);

}
