#pragma once

#include "ui/Types.h"

namespace kestrel::ui::theme {

inline constexpr Color CanvasTop { 24, 19, 34, 255 };
inline constexpr Color Canvas { 14, 12, 19, 255 };
inline constexpr Color Header { 18, 15, 25, 235 };
inline constexpr Color Surface { 27, 23, 37, 255 };
inline constexpr Color SurfaceTop { 36, 30, 50, 255 };
inline constexpr Color SurfaceAlt { 35, 30, 47, 255 };
inline constexpr Color Raised { 46, 40, 62, 255 };
inline constexpr Color Hover { 52, 45, 70, 255 };
inline constexpr Color GhostHover { 255, 255, 255, 14 };
inline constexpr Color Field { 16, 14, 22, 255 };
inline constexpr Color FieldFocused { 22, 19, 31, 255 };
inline constexpr Color Line { 255, 255, 255, 20 };
inline constexpr Color LineStrong { 255, 255, 255, 40 };
inline constexpr Color Shadow { 0, 0, 0, 120 };
inline constexpr Color Text { 245, 242, 250, 255 };
inline constexpr Color Muted { 172, 164, 190, 255 };
inline constexpr Color Subtle { 118, 110, 138, 255 };

inline constexpr Color Accent { 200, 170, 242, 255 };
inline constexpr Color AccentHover { 216, 192, 250, 255 };
inline constexpr Color AccentDeep { 166, 128, 222, 255 };
inline constexpr Color AccentPressed { 150, 112, 206, 255 };
inline constexpr Color AccentSoft { 200, 170, 242, 30 };
inline constexpr Color AccentLine { 200, 170, 242, 70 };
inline constexpr Color AccentGlow { 170, 128, 236, 70 };
inline constexpr Color OnAccent { 26, 16, 40, 255 };

inline constexpr Color Secondary { 124, 220, 198, 255 };
inline constexpr Color SecondaryDeep { 76, 176, 156, 255 };
inline constexpr Color SecondarySoft { 124, 220, 198, 30 };
inline constexpr Color SecondaryGlow { 124, 220, 198, 40 };

inline constexpr Color Danger { 238, 112, 128, 255 };
inline constexpr Color DangerHover { 248, 134, 148, 255 };
inline constexpr Color DangerDeep { 206, 84, 104, 255 };
inline constexpr Color CloseHover { 220, 60, 80, 255 };
inline constexpr Color Scrim { 6, 4, 10, 190 };

inline constexpr float Gap = 8.0f;
inline constexpr float Pad = 16.0f;
inline constexpr float PadLg = 24.0f;
inline constexpr float PadXl = 32.0f;
inline constexpr float ControlHeight = 42.0f;
inline constexpr float HeaderHeight = 68.0f;
inline constexpr float ContentMaxWidth = 1160.0f;
inline constexpr float RowHeight = 68.0f;
inline constexpr float Radius = 16.0f;

}
