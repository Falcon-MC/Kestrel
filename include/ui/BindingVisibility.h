#pragma once

#include "Core/Json/Json.h"

namespace kestrel::ui {

template<class Resolve, class Visible, class Apply>
void visitBindings(const json::Value& bindings, Resolve resolve, Visible visible, Apply apply)
{
    bool conditional = false;
    auto whenVisible = [&](const json::Value& binding) {
        const json::Value* condition = resolve(binding.get("binding_condition"));
        return condition && condition->isString() && condition->mString == "visible";
    };
    for (const auto& binding : bindings.mArray) {
        if (!binding->isObject()) continue;
        if (whenVisible(*binding)) conditional = true;
        else apply(*binding);
    }
    if (!conditional || !visible()) return;
    for (const auto& binding : bindings.mArray) {
        if (binding->isObject() && whenVisible(*binding)) apply(*binding);
    }
    // Derived values must see the captured data in the same update, before layout is cached.
    for (const auto& binding : bindings.mArray) {
        if (!binding->isObject() || whenVisible(*binding)) continue;
        const json::Value* type = resolve(binding->get("binding_type"));
        if (type && type->isString() && type->mString == "view") apply(*binding);
    }
}

}
