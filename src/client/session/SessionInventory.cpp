#include "SessionData.h"
#include "client/DebugLog.h"
#include "Network/BedrockConnection.h"
#include "Protocol/Packets/ContainerOpenPacket.h"
#include "Protocol/Packets/ContainerClosePacket.h"
#include "Protocol/Packets/ContainerSetDataPacket.h"
#include "Protocol/Packets/CreativeContentPacket.h"
#include "Protocol/Packets/CraftingDataPacket.h"
#include "Protocol/Packets/InteractPacket.h"
#include "Protocol/Packets/InventoryContentPacket.h"
#include "Protocol/Packets/InventorySlotPacket.h"
#include "Protocol/Packets/ItemStackRequestPacket.h"
#include "Protocol/Packets/ItemStackResponsePacket.h"

#include <algorithm>
#include <limits>

namespace kestrel {
namespace {
using namespace inventory;

int containerSize(ContainerType type)
{
    switch (type) {
    case ContainerType::Container: case ContainerType::MinecartChest: case ContainerType::ChestBoat: return 27;
    case ContainerType::Hopper: case ContainerType::MinecartHopper: return 5;
    case ContainerType::Dispenser: case ContainerType::Dropper: return 9;
    case ContainerType::Furnace: case ContainerType::BlastFurnace: case ContainerType::Smoker: return 3;
    default: return 0;
    }
}
}

void Session::requestInventory(InventoryCommand command)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (inventoryCommands.size() < 128) inventoryCommands.push_back(std::move(command));
}

// Called with mutex held: copy only lightweight display data, never raw NBT, to the render thread.
void Session::publishInventory()
{
    auto& hud = current.hud;
    auto& view = hud.container;
    for (int i = 0; i < SlotCount; ++i) view.slots[i] = hudItemOf(inventoryModel.slots[i]);
    for (int i = 0; i < 36; ++i) hud.inventory[i] = view.slots[i];
    for (int i = 0; i < 4; ++i) hud.armor[i] = view.slots[Armor + i];
    hud.offhand = view.slots[Offhand];
    if (const auto* recipe = inventoryModel.matchingRecipe()) view.slots[Output] = hudItemOf(recipe->output);
    view.windowId = inventoryModel.windowId;
    view.type = inventoryModel.type;
    view.containerSize = inventoryModel.containerSize;
    view.pending = pendingInventoryRequest != 0;
    view.craftable.clear();
    for (const auto& recipe : inventoryModel.recipes) if (inventoryModel.canCraft(recipe)) view.craftable.push_back(recipe.recipe.mRecipeNetId);
    ++view.revision;
}

void Session::handleInventoryPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto content = std::dynamic_pointer_cast<InventoryContentPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (content->mContainerId == 0) {
            // A full inventory is a resync, including when a proxy switches servers.
            // Retire old predictions before applying it so a late response or close
            // cannot restore stacks from the previous server.
            if (inventoryBefore) {
                for (int slot : inventoryChangedSlots) inventoryModel.slots[slot] = (*inventoryBefore)[slot];
            }
            inventoryBefore.reset();
            inventoryChangedSlots.clear();
            pendingInventoryRequest = 0;
            inventoryCommands.clear();
            inventoryClosing = false;
        }
        debugLog("inventory content window " + std::to_string(content->mContainerId)
            + " stacks " + std::to_string(content->mContents.size()));
        if (content->mContainerId == inventoryModel.windowId && inventoryModel.windowId != 0 && content->mContents.size() <= 54 && !enderChestOpen)
            inventoryModel.containerSize = int(content->mContents.size());
        int length = content->mContainerId == 0 ? 36 : content->mContainerId == 120 ? 4 : content->mContainerId == 119 ? 1
            : content->mContainerId == 124 ? 54 : inventoryModel.containerSize;
        for (int i = 0; i < length; ++i) {
            int target = inventoryModel.packetSlot(content->mContainerId, i);
            if (target < 0) continue;
            ItemStack item = i < int(content->mContents.size()) ? content->mContents[i] : ItemStack::air();
            inventoryModel.slots[target] = item;
            if (inventoryBefore) (*inventoryBefore)[target] = item;
        }
        publishInventory();
    } else if (auto single = std::dynamic_pointer_cast<InventorySlotPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        int slot = inventoryModel.packetSlot(single->mContainerId, single->mSlot);
        if (slot >= 0) {
            inventoryModel.slots[slot] = single->mItem;
            if (inventoryBefore) (*inventoryBefore)[slot] = single->mItem;
            publishInventory();
        }
    } else if (auto open = std::dynamic_pointer_cast<ContainerOpenPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        // Opening a block container confirms a successful use even on servers
        // that do not echo an Animate packet to the player using it.
        if (open->mType != ContainerType::Inventory) current.hud.lastSwing = secondsNow();
        inventoryModel.windowId = uint8_t(open->mWindowId);
        inventoryModel.type = open->mType;
        inventoryModel.containerSize = containerSize(open->mType);
        enderChestOpen = false;
        if (open->mType == ContainerType::Container && open->mUniqueActorId == -1 && assets) {
            uint32_t block = blockAt(open->mBlockPosition.x, open->mBlockPosition.y, open->mBlockPosition.z);
            std::string name = assets->blockName(block, ids.hashed, ids.sequential.get());
            enderChestOpen = name == "minecraft:ender_chest" || name == "ender_chest";
        }
        current.hud.container.enderChest = enderChestOpen;
        current.hud.container.customName.clear();
        if (open->mUniqueActorId == -1) {
            const Vector3i& at = open->mBlockPosition;
            std::shared_ptr<const world::BlockEntityMap> entities = world.store().blockEntities({ current.dimension, at.x >> 4, at.y >> 4, at.z >> 4 });
            if (entities) {
                auto found = entities->find(static_cast<uint16_t>(world::linearIndex(uint32_t(at.x & 15), uint32_t(at.y & 15), uint32_t(at.z & 15))));
                const Tag* name = found != entities->end() ? found->second.get("CustomName") : nullptr;
                if (name && name->getType() == Tag::Type::String) current.hud.container.customName = name->asString();
            }
        }
        for (int i = Container; i < SlotCount; ++i) inventoryModel.slots[i] = ItemStack::air();
        current.hud.container.furnaceProgress = 0;
        current.hud.container.furnaceFlame = 0;
        ++current.hud.container.openRevision;
        publishInventory();
    } else if (auto close = std::dynamic_pointer_cast<ContainerClosePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (uint8_t(close->mWindowId) != inventoryModel.windowId) return;
        if (close->mServerInitiated) {
            ContainerClosePacket reply = *close;
            reply.mServerInitiated = false;
            transmit(reply);
        }
        if (inventoryBefore) for (int slot : inventoryChangedSlots) inventoryModel.slots[slot] = (*inventoryBefore)[slot];
        inventoryBefore.reset();
        pendingInventoryRequest = 0;
        inventoryClosing = false;
        inventoryCommands.clear();
        inventoryModel.type = ContainerType::Inventory;
        inventoryModel.windowId = 0;
        inventoryModel.containerSize = 0;
        current.hud.container.recipeGhost = {};
        current.hud.container.recipeGhostOutput = {};
        ++current.hud.container.closeRevision;
        publishInventory();
    } else if (auto response = std::dynamic_pointer_cast<ItemStackResponsePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        for (const auto& entry : response->mEntries) {
            if (entry.mRequestId != pendingInventoryRequest || !inventoryBefore) continue;
            if (entry.mResult != 0) {
                for (int slot : inventoryChangedSlots) inventoryModel.slots[slot] = (*inventoryBefore)[slot];
                inventoryCommands.clear();
                inventoryClosing = false;
                debugLog("inventory request rejected " + std::to_string(entry.mRequestId) + ", result " + std::to_string(entry.mResult));
            } else {
                for (const auto& container : entry.mContainers) for (const auto& update : container.mItems) {
                    int slot = inventoryModel.responseSlot(container.mContainerName.mContainer, update.mSlot);
                    if (slot < 0 || slot >= SlotCount) continue;
                    ItemStack& item = inventoryModel.slots[slot];
                    // A server that cancels the drop still answers ok, just with the stack left in place.
                    if (update.mCount > 0 && item.isAir()) item = (*inventoryBefore)[slot];
                    if (update.mCount <= 0) item = ItemStack::air();
                    else if (!item.isAir()) {
                        item.mCount = update.mCount;
                        item.mNetId = update.mStackNetworkId;
                        item.mUsingNetId = true;
                    }
                }
            }
            inventoryBefore.reset();
            inventoryChangedSlots.clear();
            pendingInventoryRequest = 0;
            publishInventory();
        }
    } else if (auto creative = std::dynamic_pointer_cast<CreativeContentPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        auto catalog = std::make_shared<std::vector<InventoryCatalogItem>>();
        inventoryModel.creative.clear();
        for (const auto& entry : creative->mItems) {
            if (InventoryModel::empty(entry.mItem)) continue;
            InventoryCatalogItem item;
            item.item = hudItemOf(entry.mItem);
            item.networkId = entry.mNetId;
            item.group = entry.mGroupIndex;
            if (item.group >= 0 && item.group < int(creative->mGroups.size())) {
                item.category = creative->mGroups[item.group].mCategory;
                item.groupName = creative->mGroups[item.group].mName;
            }
            inventoryModel.creative[item.networkId] = entry.mItem;
            catalog->push_back(std::move(item));
        }
        current.hud.container.creative = std::move(catalog);
        ++current.hud.container.revision;
    } else if (auto recipes = std::dynamic_pointer_cast<CraftingDataPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (recipes->mCleanRecipes) inventoryModel.recipes.clear();
        auto append = [&](const auto& entries, bool shaped) {
            for (const auto& entry : entries) {
                if (entry.mOutputs.empty() || (entry.mBlockName != "crafting_table" && entry.mBlockName != "minecraft:crafting_table")) continue;
                if (entry.mInputs.size() > 9 || (shaped && (entry.mWidth < 1 || entry.mHeight < 1))) continue;
                std::vector<ItemStack> stacks;
                for (const auto& output : entry.mOutputs) {
                    ItemStack stack;
                    stack.mDefinition = itemDefinitions.getDefinition(output.mRuntimeId);
                    if (!stack.mDefinition) break;
                    stack.mCount = output.mCount;
                    stack.mDamage = output.mMeta;
                    stack.mTag = output.mTag;
                    stack.mBlockDefinition = blockDefinitions.getDefinition(output.mBlockRuntimeId);
                    stacks.push_back(std::move(stack));
                }
                if (stacks.size() != entry.mOutputs.size()) continue;
                std::erase_if(inventoryModel.recipes, [&](const auto& existing) { return existing.recipe.mRecipeNetId == entry.mRecipeNetId; });
                ItemStack first = std::move(stacks.front());
                stacks.erase(stacks.begin());
                inventoryModel.recipes.push_back({ entry, std::move(first), shaped, std::move(stacks) });
            }
        };
        append(recipes->mShapedRecipes, true);
        append(recipes->mShapelessRecipes, false);
        auto catalog = std::make_shared<std::vector<InventoryCatalogItem>>();
        for (const auto& recipe : inventoryModel.recipes) {
            InventoryCatalogItem item;
            item.item = hudItemOf(recipe.output);
            item.networkId = recipe.recipe.mRecipeNetId;
            if (current.hud.container.creative) for (const auto& creative : *current.hud.container.creative)
                if (creative.item.identifier == item.item.identifier) { item.category = creative.category; item.group = creative.group; item.groupName = creative.groupName; break; }
            catalog->push_back(std::move(item));
        }
        current.hud.container.recipes = std::move(catalog);
        publishInventory();
    } else if (auto data = std::dynamic_pointer_cast<ContainerSetDataPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (uint8_t(data->mWindowId) == inventoryModel.windowId) {
            if (data->mProperty == 0) current.hud.container.furnaceProgress = std::clamp(data->mValue / 200.0f, 0.0f, 1.0f);
            if (data->mProperty == 1) current.hud.container.furnaceFlame = std::clamp(data->mValue / 200.0f, 0.0f, 1.0f);
        }
    }
}

void Session::flushInventory()
{
    if (!connection) return;
    std::lock_guard<std::mutex> guard(mutex);
    if (pendingInventoryRequest) {
        if (secondsNow() - inventoryRequestTime < 5.0) return;
        if (inventoryBefore) for (int slot : inventoryChangedSlots) inventoryModel.slots[slot] = (*inventoryBefore)[slot];
        inventoryBefore.reset();
        inventoryChangedSlots.clear();
        pendingInventoryRequest = 0;
        inventoryCommands.clear();
        inventoryClosing = false;
        debugLog("inventory response timed out; discarded queued predictions");
        publishInventory();
    }
    if (inventoryClosing) {
        ContainerClosePacket close;
        close.mWindowId = int8_t(inventoryModel.windowId);
        close.mType = inventoryModel.type;
        close.mServerInitiated = false;
        transmit(close);
        inventoryClosing = false;
        inventoryModel.windowId = 0;
        inventoryModel.type = ContainerType::Inventory;
        inventoryModel.containerSize = 0;
        publishInventory();
    }
    if (inventoryCommands.empty()) return;
    InventoryCommand command = std::move(inventoryCommands.front());
    inventoryCommands.erase(inventoryCommands.begin());
    if (command.action == InventoryAction::Open) {
        InteractPacket open;
        open.mAction = InteractPacket::Action::OpenInventory;
        open.mRuntimeActorId = localRuntimeId;
        open.mHasMousePosition = false;
        transmit(open);
        return;
    }
    inventoryModel.creativeMode = current.hud.gameType == 1;
    if (command.action == InventoryAction::Close) inventoryClosing = true;
    inventoryBefore = inventoryModel.slots;
    if (inventoryRequestId < std::numeric_limits<int32_t>::min() + 2) inventoryRequestId = -1;
    ItemStackRequest request = inventoryModel.plan(command, inventoryRequestId);
    inventoryRequestId -= 2;
    auto& ghost = current.hud.container;
    if (command.action == InventoryAction::SelectRecipe) {
        std::array<HudItem, 9> cells {};
        HudItem output;
        if (inventoryModel.recipeGhost(command.value, cells, output)) {
            ghost.recipeGhost = cells;
            ghost.recipeGhostOutput = output;
        }
    } else {
        ghost.recipeGhost = {};
        ghost.recipeGhostOutput = {};
    }
    if (request.mActions.empty()) {
        inventoryBefore.reset();
        publishInventory();
        return;
    }
    inventoryChangedSlots.clear();
    for (int i = 0; i < SlotCount; ++i) {
        const auto& before = (*inventoryBefore)[i];
        const auto& after = inventoryModel.slots[i];
        if (before.mNetId != after.mNetId || before.mCount != after.mCount || before.mDefinition != after.mDefinition) inventoryChangedSlots.insert(i);
    }
    pendingInventoryRequest = request.mRequestId;
    inventoryRequestTime = secondsNow();
    ItemStackRequestPacket packet;
    packet.mRequests.push_back(std::move(request));
    transmit(packet);
    publishInventory();
}
}
