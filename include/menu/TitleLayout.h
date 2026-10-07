#pragma once

namespace kestrel::menu {

struct TitleLayout {
    float logoTop;
    float logoWidth;
    float dressingCenter;
    float dressingTop;
};

inline TitleLayout titleLayout(float width, float height)
{
    return { height * 0.12f, width * 0.55f, width * 0.75f + 37.0f, height * 0.9f - 28.0f };
}

}
