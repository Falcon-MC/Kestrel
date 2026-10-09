#include "ui/BindingVisibility.h"
#include "ui/BindingLookup.h"
#include "ui/LabelScale.h"
#include "ui/JsonUiInternal.h"

#include <cstdio>
#include <cstdlib>

using namespace kestrel::ui;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

struct BindingHarness {
    UiRow input;
    UiRow bound;
    bool parentVisible = true;

    void update(const json::Value& bindings)
    {
        auto lookup = [&](const std::string& name) {
            auto found = bound.find(name);
            if (found != bound.end()) return found->second;
            auto global = input.find(name);
            return global != input.end() ? global->second : UiValue {};
        };
        visitBindings(bindings, [](const json::Value* value) { return value; }, [&] {
            auto found = bound.find("#visible");
            return parentVisible && (found == bound.end() || found->second.truthy());
        }, [&](const json::Value& binding) {
            if (const auto* source = binding.get("source_property_name")) {
                bound[binding.get("target_property_name")->string()] = jsonui::evaluate(source->string(), lookup);
            } else {
                std::string name = binding.get("binding_name")->string();
                std::string target = binding.get("binding_name_override") ? binding.get("binding_name_override")->string() : name;
                if (auto found = input.find(name); found != input.end()) bound[target] = found->second;
            }
        });
    }
};

int main()
{
    for (const char* collection : { "furnace_ingredient_items", "furnace_fuel_items", "furnace_output_items" }) {
        jsonui::Node button;
        button.detailsCollection = collection;
        UiEvent target = jsonui::collectionTarget(button);
        require(target.collection == collection && target.index == 0, "Standalone furnace cells must address their collection's first row");
        jsonui::Node overlay;
        overlay.parent = &button;
        target = jsonui::collectionTarget(overlay);
        require(target.collection == collection && target.index == 0, "A child hit must retain its standalone cell's address");
    }
    jsonui::Node outer;
    outer.collection = "groups";
    outer.index = 2;
    jsonui::Node cell;
    cell.parent = &outer;
    cell.collection = "inventory_items";
    cell.index = 7;
    jsonui::Node button;
    button.parent = &cell;
    button.detailsCollection = "inventory_items";
    UiEvent target = jsonui::collectionTarget(button);
    require(target.collection == "inventory_items" && target.index == 7 && target.outerIndex == 2, "Generated grid cells must preserve their index and nested collection address");
    jsonui::Node unrelated;
    require(jsonui::collectionTarget(unrelated).index == -1, "Unbound controls must not become inventory cells");
    require(labelFontScale("small") == 0.5f && labelFontScale("normal") == 1.0f && labelFontScale("large") == 2.0f && labelFontScale("extra_large") == 4.0f, "Label font sizes must use the vanilla scale steps");
    require(labelFontScale("unknown") == 1.0f, "Unknown font sizes must retain the normal scale");
    UiRow variables {{ "$scale", UiValue::of(0.4) }, { "$divisor", UiValue::of(8.0) }, { "$alias", UiValue::of("#scale") }};
    UiRow source {{ "#scale", UiValue::of(8.0) }, { "$scale", UiValue::of(99.0) }};
    auto read = [](const UiRow& values, const std::string& key) {
        auto found = values.find(key);
        return found == values.end() ? UiValue {} : found->second;
    };
    UiLookup view = [&](const std::string& key) {
        return resolveBindingValue(key, [&](const std::string& name) { return read(variables, name); }, [&](const std::string& name) { return read(source, name); });
    };
    require(std::abs(jsonui::evaluate("($scale * (#scale / $divisor))", view).toNumber() - 0.4) < 0.000001, "View expressions must retain the receiving control's variables");
    require(view("$alias").toNumber() == 8.0, "Variable binding aliases must read the source control");
    auto bindings = jsonui::readUiJson(R"json([
        {"binding_name":"#title"},
        {"binding_type":"view","source_property_name":"(#stored - 'map:')","target_property_name":"#pixels"},
        {"binding_name":"#title","binding_name_override":"#stored","binding_condition":"visible"},
        {"binding_type":"view","source_property_name":"(not((#title - 'map:') = #title) and not(#title = #stored))","target_property_name":"#visible"}
    ])json");
    require(bindings && bindings->isArray(), "Synthetic binding fixture must parse");
    BindingHarness map;
    map.input["#title"] = UiValue::of("map:first");
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:first", "First visible frame must capture the current title");
    require(map.bound["#pixels"].toText() == "first", "Derived pixels must update before layout is cached");
    map.input["#title"] = UiValue::of("ordinary title");
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:first", "An unrelated title must preserve the last map frame");
    require(map.bound["#pixels"].toText() == "first", "Hidden capture must retain the displayed map");
    map.input["#title"] = UiValue::of("map:second");
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:second" && map.bound["#pixels"].toText() == "second", "A matching title must resume capture immediately");
    map.input["#title"] = UiValue::of("");
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:second", "Clearing titles must not erase the saved frame");
    map.parentVisible = false;
    map.input["#title"] = UiValue::of("map:third");
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:second", "A hidden ancestor must prevent conditional capture");
    map.parentVisible = true;
    map.update(*bindings);
    require(map.bound["#stored"].toText() == "map:third", "Revealing the parent must capture fresh data");

    auto biomeBindings = jsonui::readUiJson(R"json([
        {"binding_name":"#title"},
        {"binding_name":"#title","binding_name_override":"#stored","binding_condition":"visible"},
        {"binding_type":"view","source_property_name":"(not((#title - 'biome:') = #title) and not(#title = #stored))","target_property_name":"#visible"}
    ])json");
    BindingHarness biome;
    for (const char* title : { "biome:forest", "map:fourth", "ordinary title" }) {
        map.input["#title"] = UiValue::of(title);
        biome.input["#title"] = UiValue::of(title);
        map.update(*bindings);
        biome.update(*biomeBindings);
    }
    require(map.bound["#stored"].toText() == "map:fourth" && biome.bound["#stored"].toText() == "biome:forest", "Ordered title channels must latch independently before the rendered frame");

    auto simple = jsonui::readUiJson(R"json([
        {"binding_name":"#value","binding_name_override":"#stored","binding_condition":"visible"},
        {"binding_name":"#show","binding_name_override":"#visible"},
        {"binding_name":"#value","binding_name_override":"#unconditional","binding_condition":"always"},
        {"binding_type":"view","source_property_name":"(#value + '!')","target_property_name":"#derived","binding_condition":"visible"}
    ])json");
    BindingHarness panel;
    panel.input["#show"] = UiValue::of(false);
    panel.input["#value"] = UiValue::of("hidden");
    panel.update(*simple);
    require(!panel.bound.count("#stored") && !panel.bound.count("#derived"), "Initially hidden controls must not initialize visible bindings");
    require(panel.bound["#unconditional"].toText() == "hidden", "Unconditional bindings must keep updating when hidden");
    panel.input["#show"] = UiValue::of(true);
    panel.input["#value"] = UiValue::of("shown");
    panel.update(*simple);
    require(panel.bound["#stored"].toText() == "shown", "Visibility later in the binding array must take effect immediately");
    require(panel.bound["#derived"].toText() == "shown!", "Visible view bindings must resolve current data");
    panel.input["#show"] = UiValue::of(false);
    panel.input["#value"] = UiValue::of("changed");
    panel.update(*simple);
    require(panel.bound["#stored"].toText() == "shown", "Hidden values must remain latched");
    require(panel.bound["#unconditional"].toText() == "changed", "Visible conditions must not change always bindings");
}
