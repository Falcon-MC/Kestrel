#include "SessionData.h"

#include "Protocol/Packets/RemoveObjectivePacket.h"
#include "Protocol/Packets/SetDisplayObjectivePacket.h"
#include "Protocol/Packets/SetScorePacket.h"

#include <algorithm>

namespace kestrel {

namespace {

constexpr const char* SidebarSlot = "sidebar";
constexpr size_t MaxSidebarLines = 15;
constexpr int32_t DescendingOrder = 1;

}

void Session::handleScorePacket(const std::shared_ptr<Packet>& packet)
{
    if (auto display = std::dynamic_pointer_cast<SetDisplayObjectivePacket>(packet)) {
        // Displaying an objective starts it over and clearing a slot drops what it showed,
        // the way the game keeps no scores for objectives it is not shown: servers that
        // rebuild their sidebar with fresh score ids would otherwise stack every copy.
        auto forget = [&](const std::string& objective) {
            std::erase_if(scores, [&](const auto& entry) { return entry.second.objective == objective; });
            objectives.erase(objective);
        };
        if (auto shown = displaySlots.find(display->mDisplaySlot); shown != displaySlots.end()) {
            std::string previous = shown->second.first;
            displaySlots.erase(shown);
            bool elsewhere = std::any_of(displaySlots.begin(), displaySlots.end(), [&](const auto& entry) { return entry.second.first == previous; });
            if (!elsewhere) {
                forget(previous);
            }
        }
        if (!display->mObjectiveId.empty()) {
            forget(display->mObjectiveId);
            objectives[display->mObjectiveId] = display->mDisplayName;
            displaySlots[display->mDisplaySlot] = { display->mObjectiveId, display->mSortOrder };
        }
    } else if (auto removed = std::dynamic_pointer_cast<RemoveObjectivePacket>(packet)) {
        objectives.erase(removed->mObjectiveId);
        std::erase_if(scores, [&](const auto& entry) { return entry.second.objective == removed->mObjectiveId; });
        std::erase_if(displaySlots, [&](const auto& entry) { return entry.second.first == removed->mObjectiveId; });
    } else if (auto score = std::dynamic_pointer_cast<SetScorePacket>(packet)) {
        for (const ScoreInfoEntry& info : score->mInfos) {
            if (info.mType == ScorerType::Invalid) {
                scores.erase(info.mScoreboardId);
                continue;
            }
            // The game drops scores of objectives it does not know, which is what keeps a
            // server that is being left through a proxy from refilling a removed sidebar.
            if (!objectives.contains(info.mObjectiveId)) {
                continue;
            }
            ScoreLine& line = scores[info.mScoreboardId];
            line.objective = info.mObjectiveId;
            line.score = info.mScore;
            line.name = info.mName;
            line.actorId = info.mActorId;
            line.player = info.mType == ScorerType::Player;
        }
    } else {
        return;
    }
    rebuildSidebar();
}

/**
 * Lines of the objective in the sidebar slot, sorted by score the way the
 * objective asks with ties kept in the order the scores were created.
 * Players and entities are shown by their current name.
 */
void Session::rebuildSidebar()
{
    SidebarView view;
    auto slot = displaySlots.find(SidebarSlot);
    if (slot != displaySlots.end()) {
        const auto& [objective, order] = slot->second;
        view.visible = true;
        view.title = objectives[objective];
        for (const auto& [id, line] : scores) {
            if (line.objective != objective) {
                continue;
            }
            std::string name = line.name;
            if (line.player) {
                if (auto known = playerNamesByActor.find(line.actorId); known != playerNamesByActor.end()) {
                    name = known->second;
                }
            } else if (name.empty()) {
                if (auto runtime = runtimeByUnique.find(line.actorId); runtime != runtimeByUnique.end()) {
                    if (auto actor = actors.find(runtime->second); actor != actors.end()) {
                        name = actor->second.name;
                    }
                }
            }
            view.lines.emplace_back(std::move(name), line.score);
        }
        std::stable_sort(view.lines.begin(), view.lines.end(), [&](const auto& a, const auto& b) {
            return order == DescendingOrder ? a.second > b.second : a.second < b.second;
        });
        if (view.lines.size() > MaxSidebarLines) {
            view.lines.resize(MaxSidebarLines);
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.sidebar = std::move(view);
}

}
