#pragma once

namespace kestrel {

constexpr bool handVisible(bool hudHidden, bool hideHand)
{
    return !hudHidden && !hideHand;
}

}
