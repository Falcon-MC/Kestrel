#include "modding/ModDependencies.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace kestrel::modding;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool refused(const DependencyPlan& plan, size_t index)
{
    return std::any_of(plan.refused.begin(), plan.refused.end(), [index](const auto& entry) { return entry.first == index; });
}

int main()
{
    {
        DependencyPlan plan = planDependencies({ { "minimap", { "core" } }, { "core", {} } }, {});
        check(plan.order == std::vector<size_t> { 1, 0 }, "a dependency starts before the mod needing it");
        check(plan.refused.empty(), "nothing is refused when every dependency is there");
    }
    {
        DependencyPlan plan = planDependencies({ { "a", {} }, { "b", {} }, { "c", {} } }, {});
        check(plan.order == std::vector<size_t> { 0, 1, 2 }, "independent mods keep their order");
    }
    {
        DependencyPlan plan = planDependencies({ { "ui", { "core", "net" } }, { "net", { "core" } }, { "core", {} } }, {});
        check(plan.order == std::vector<size_t> { 2, 1, 0 }, "chains start from the bottom");
    }
    {
        DependencyPlan plan = planDependencies({ { "minimap", { "core" } } }, { "core" });
        check(plan.order == std::vector<size_t> { 0 }, "a running mod satisfies a dependency");
    }
    {
        DependencyPlan plan = planDependencies({ { "minimap", { "core" } }, { "waypoints", { "minimap" } }, { "fps", {} } }, {});
        check(plan.order == std::vector<size_t> { 2 }, "only the mod without dependencies starts");
        check(refused(plan, 0), "a mod with a missing dependency is refused");
        check(refused(plan, 1), "a mod depending on a refused mod is refused");
        auto reason = std::find_if(plan.refused.begin(), plan.refused.end(), [](const auto& entry) { return entry.first == 0; });
        check(reason != plan.refused.end() && reason->second.find("core") != std::string::npos, "the reason names the missing mod");
    }
    {
        DependencyPlan plan = planDependencies({ { "a", { "b" } }, { "b", { "a" } }, { "c", { "c" } }, { "d", {} } }, {});
        check(plan.order == std::vector<size_t> { 3 }, "mods outside a cycle still start");
        check(refused(plan, 0) && refused(plan, 1), "a two mod cycle is refused");
        check(refused(plan, 2), "a mod depending on itself is refused");
        check(plan.refused.size() == 3, "each refused mod is listed once");
    }
}
