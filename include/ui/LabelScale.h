#pragma once

#include <string_view>

namespace kestrel::ui {

constexpr float labelFontScale(std::string_view size)
{
    if (size == "small") return 0.5f;
    if (size == "large") return 2.0f;
    if (size == "extra_large") return 4.0f;
    return 1.0f;
}

}
