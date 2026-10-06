#include "client/HandVisibility.h"

#include <initializer_list>

int main()
{
    for (bool hideHand : { false, true }) {
        bool hudHidden = false;
        if (kestrel::handVisible(hudHidden, hideHand) != !hideHand) return 1;
        hudHidden = !hudHidden;
        if (kestrel::handVisible(hudHidden, hideHand)) return 2;
        hudHidden = !hudHidden;
        if (kestrel::handVisible(hudHidden, hideHand) != !hideHand) return 3;
    }
}
