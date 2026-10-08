#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::modding {

/**
 * A mod waiting to start: its id and the ids of the mods it needs first.
 */
struct DependencyNode {
    std::string id;
    std::vector<std::string> dependencies;
};

/**
 * The order to start mods in, as indices into the nodes, and the ones that
 * cannot start with the reason why.
 */
struct DependencyPlan {
    std::vector<size_t> order;
    std::vector<std::pair<size_t, std::string>> refused;
};

/**
 * Orders nodes so each one starts after its dependencies, which are either
 * running already or among the nodes. A node that needs a mod nobody offers,
 * needs a refused one or sits in a cycle is refused. Nodes free to start in
 * any order keep the order they came in.
 */
DependencyPlan planDependencies(const std::vector<DependencyNode>& nodes, const std::set<std::string>& running);

}
