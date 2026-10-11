#pragma once

#include "ui/Types.h"

namespace kestrel::ui::theme {

// One menu unit is one pixel of the classic GUI, so a 4x4 nine slice texture maps 1:1.
// The HTML menus size everything in rem, which lands on 3.75 of those units.
inline constexpr float Rem = 3.75f;

constexpr float css(float pixels)
{
    return pixels * Rem / 10.0f;
}

inline constexpr Color White { 255, 255, 255, 255 };
inline constexpr Color Black { 0, 0, 0, 255 };
inline constexpr Color Clear { 0, 0, 0, 0 };

// Straight from the menu theme stylesheet.
inline constexpr Color Primary { 0x3c, 0x85, 0x27, 255 };
inline constexpr Color Secondary { 0xd0, 0xd1, 0xd4, 255 };
inline constexpr Color Destructive { 0xca, 0x36, 0x36, 255 };
inline constexpr Color Muted0 { 0xd0, 0xd1, 0xd4, 255 };
inline constexpr Color Muted1 { 0xb1, 0xb2, 0xb5, 255 };
inline constexpr Color Disabled { 0x58, 0x58, 0x5a, 255 };
inline constexpr Color Caret { 0x6c, 0xc3, 0x49, 255 };

// Colour roles of the menu theme, named the way the game's components ask for them.
inline constexpr Color Neutral60 { 0x58, 0x58, 0x5a, 255 };
inline constexpr Color Neutral80 { 0x31, 0x32, 0x33, 255 };
inline constexpr Color Neutral80Hovered { 0x48, 0x49, 0x4a, 255 };
inline constexpr Color Neutral90 { 0x24, 0x24, 0x25, 255 };
inline constexpr Color Neutral100 { 0x1e, 0x1e, 0x1f, 255 };
inline constexpr Color NeutralAlpha60 { 0, 0, 0, 153 };
inline constexpr Color Informative { 0x2e, 0x6b, 0xe5, 255 };
inline constexpr Color InformativeTint { 0x8c, 0xb3, 0xff, 255 };
inline constexpr Color NoticeTint { 0xff, 0xe8, 0x66, 255 };
inline constexpr Color DestructiveTint { 0xff, 0x80, 0x80, 255 };
inline constexpr Color SuccessTint { 0xa0, 0xe0, 0x81, 255 };

// Surfaces sampled from the HTML menus, they aren't in the theme sheet.
inline constexpr Color Panel { 0x48, 0x49, 0x4a, 255 };
inline constexpr Color PanelDark { 0x31, 0x32, 0x33, 255 };
inline constexpr Color Divider { 0x1e, 0x1e, 0x1f, 255 };
inline constexpr Color HeaderBar { 0xe6, 0xe8, 0xeb, 255 };
inline constexpr Color HeaderEdge { 0xb1, 0xb2, 0xb5, 255 };
inline constexpr Color InkDark { 0x1e, 0x1e, 0x1f, 255 };

// Classic screens.
inline constexpr Color ButtonText { 0x4c, 0x4c, 0x4c, 255 };
inline constexpr Color ButtonTextHover { 255, 255, 255, 255 };

}
