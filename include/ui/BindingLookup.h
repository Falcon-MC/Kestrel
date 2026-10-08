#pragma once

#include "ui/JsonUi.h"

namespace kestrel::ui {

inline UiValue resolveBindingValue(const std::string& key, const UiLookup& variables, const UiLookup& binding)
{
    if (!key.empty() && key.front() == '#') return binding(key);
    UiValue value = variables(key);
    if (!key.empty() && key.front() == '$' && value.kind == UiValue::Kind::String && value.text.size() > 1 && value.text.front() == '#') {
        return binding(value.text);
    }
    return value;
}

}
