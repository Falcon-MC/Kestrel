#include "modding/ModDependencies.h"

namespace kestrel::modding {

DependencyPlan planDependencies(const std::vector<DependencyNode>& nodes, const std::set<std::string>& running)
{
    DependencyPlan plan;
    std::set<std::string> ready = running;
    std::vector<bool> settled(nodes.size(), false);
    bool progressed = true;
    while (progressed) {
        progressed = false;
        std::set<std::string> offered;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!settled[i]) {
                offered.insert(nodes[i].id);
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (settled[i]) {
                continue;
            }
            bool waiting = false;
            const std::string* missing = nullptr;
            for (const std::string& dependency : nodes[i].dependencies) {
                if (ready.contains(dependency)) {
                    continue;
                }
                if (offered.contains(dependency)) {
                    waiting = true;
                    continue;
                }
                missing = &dependency;
                break;
            }
            if (missing) {
                settled[i] = true;
                progressed = true;
                plan.refused.emplace_back(i, "it needs the mod " + *missing + ", which is missing or could not start");
                continue;
            }
            if (waiting) {
                continue;
            }
            settled[i] = true;
            progressed = true;
            plan.order.push_back(i);
            ready.insert(nodes[i].id);
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!settled[i]) {
            plan.refused.emplace_back(i, "its dependencies form a cycle");
        }
    }
    return plan;
}

}
