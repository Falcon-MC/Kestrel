#pragma once

#include "ui/Context.h"
#include "ui/Theme.h"

namespace kestrel::menu::play {

inline constexpr float TabHeight = 37.0f;
inline constexpr float RowHeight = 23.33f;
inline constexpr float ButtonHeight = 22.0f;
inline constexpr float SectionGap = 10.0f;

/**
 * The hairline the HTML menus put between rows and sections.
 */
inline void divider(ui::Context& ui, float x, float y, float width)
{
    ui.fill({ x, y, width, ui::theme::css(2.0f) }, ui::theme::Divider);
}

}
