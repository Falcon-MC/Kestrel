#include "SessionData.h"
#include "client/ContainerLayout.h"
#include "client/DebugLog.h"
#include "Core/Json/Json.h"
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
#include "Protocol/Packets/PlayerEnchantOptionsPacket.h"
#include "Protocol/Packets/NpcDialoguePacket.h"
#include "Protocol/Packets/NpcRequestPacket.h"
#include "Protocol/Packets/BookEditPacket.h"
#include "Protocol/Packets/PlayerToggleCrafterSlotRequestPacket.h"
#include "Protocol/Packets/TrimDataPacket.h"
#include "Protocol/Packets/BlockActorDataPacket.h"

#include <algorithm>
#include <limits>

namespace kestrel {
namespace {
using namespace inventory;

class LecternUpdatePacket final : public Packet {
public:
    static constexpr MinecraftPacketIds ID = static_cast<MinecraftPacketIds>(0x7d);

    MinecraftPacketIds getId() const override { return ID; }
    const char* getName() const override { return "LecternUpdatePacket"; }

    void write(BinaryStream& stream, const PacketCodecContext&) const override
    {
        stream.putByte(mPage);
        stream.putByte(mTotalPages);
        stream.putBlockPosition(mBlockPosition);
    }

    void read(ReadOnlyBinaryStream& stream, const PacketCodecContext&) override
    {
        mPage = stream.getByte();
        mTotalPages = stream.getByte();
        mBlockPosition = stream.getBlockPosition();
    }

    uint8_t mPage = 0;
    uint8_t mTotalPages = 0;
    Vector3i mBlockPosition;
};

int integerTag(const Tag& compound, const std::string& key, int fallback)
{
    const Tag* value = compound.isCompound() ? compound.get(key) : nullptr;
    if (!value) {
        return fallback;
    }
    switch (value->getType()) {
    case Tag::Type::Byte: return value->asByte();
    case Tag::Type::Short: return value->asShort();
    case Tag::Type::Int: return value->asInt();
    default: return fallback;
    }
}

int containerSize(ContainerType type)
{
    switch (type) {
    case ContainerType::Container: case ContainerType::MinecartChest: case ContainerType::ChestBoat: return 27;
    case ContainerType::Hopper: case ContainerType::MinecartHopper: return 5;
    case ContainerType::Dispenser: case ContainerType::Dropper: return 9;
    case ContainerType::Furnace: case ContainerType::BlastFurnace: case ContainerType::Smoker: return 3;
    case ContainerType::BrewingStand: return 5;
    case ContainerType::Crafter: return 9;
    case ContainerType::Horse: return 2;
    default: return 0;
    }
}
}

void Session::requestInventory(InventoryCommand command)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (inventoryCommands.size() < 128) inventoryCommands.push_back(std::move(command));
}

bool Session::openBook(int slot)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (slot < 0 || slot >= 36) {
        return false;
    }
    const auto& item = inventoryModel.slots[slot];
    if (item.isAir()) {
        return false;
    }
    const std::string& identifier = item.mDefinition->getIdentifier();
    if (identifier != "minecraft:writable_book" && identifier != "minecraft:written_book") {
        return false;
    }
    auto& view = current.hud.container;
    view.screen = "book.book_screen";
    view.bookEditable = identifier == "minecraft:writable_book";
    view.bookSlot = slot;
    view.pages.clear();
    view.author.clear();
    view.bookTitle.clear();
    Tag tag = item.mTag.isCompound() ? item.mTag : Tag::ofCompound();
    if (const Tag* pages = tag.get("pages"); pages && pages->isList()) {
        for (const auto& page : pages->getList()) {
            if (view.pages.size() == 50) {
                break;
            }
            const Tag* text = page.isCompound() ? page.get("text") : nullptr;
            view.pages.push_back(text && text->getType() == Tag::Type::String ? text->asString() : std::string());
        }
    }
    if (view.pages.empty()) {
        view.pages.emplace_back();
    }
    if (const Tag* author = tag.get("author"); author && author->getType() == Tag::Type::String) {
        view.author = author->asString();
    }
    if (const Tag* title = tag.get("title"); title && title->getType() == Tag::Type::String) {
        view.bookTitle = title->asString();
    }
    if (view.bookEditable && playerNamesByActor.contains(localUniqueId)) {
        view.author = playerNamesByActor.at(localUniqueId);
    }
    ++view.openRevision;
    ++view.revision;
    return true;
}

// Called with mutex held: copy only lightweight display data, never raw NBT, to the render thread.
void Session::publishInventory()
{
    auto& hud = current.hud;
    auto& view = hud.container;
    inventoryModel.creativeMode = hud.gameType == 1;
    for (int i = 0; i < SlotCount; ++i) view.slots[i] = hudItemOf(inventoryModel.slots[i]);
    for (int i = 0; i < 36; ++i) hud.inventory[i] = view.slots[i];
    for (int i = 0; i < 4; ++i) hud.armor[i] = view.slots[Armor + i];
    hud.offhand = view.slots[Offhand];
    if (inventoryModel.type == ContainerType::Inventory || inventoryModel.type == ContainerType::Workbench) {
        if (const auto* recipe = inventoryModel.matchingRecipe()) {
            view.slots[Output] = hudItemOf(recipe->output);
        }
    } else if (const auto* recipe = inventoryModel.matchingStationRecipe()) {
        view.slots[Output] = hudItemOf(inventoryModel.stationRecipeResult(*recipe));
    } else if (inventoryModel.type == ContainerType::Anvil || inventoryModel.type == ContainerType::Grindstone || inventoryModel.type == ContainerType::Loom) {
        inventoryModel.bookDefinition = itemDefinitions.getDefinition("minecraft:book");
        view.slots[Output] = hudItemOf(inventoryModel.stationPreview(nullptr, &view.stationCost));
    } else if (inventoryModel.type == ContainerType::Crafter) {
        InventoryModel preview = inventoryModel;
        preview.type = ContainerType::Workbench;
        for (int index = 0; index < 9; ++index) {
            preview.slots[Craft + index] = inventoryModel.slots[Container + index];
        }
        const auto* recipe = preview.matchingRecipe();
        view.slots[Output] = recipe ? hudItemOf(recipe->output) : HudItem {};
    }
    view.stationName = inventoryModel.stationNameEdited ? inventoryModel.stationName : hudItemOf(inventoryModel.slots[Ui + 1]).customName;
    view.selectedStationRecipe = inventoryModel.stationRecipe;
    view.loomPatterns = inventoryModel.type == ContainerType::Loom ? inventoryModel.availableLoomPatterns() : std::vector<std::string>();
    if (inventoryModel.type == ContainerType::Loom) {
        auto selected = std::find(view.loomPatterns.begin(), view.loomPatterns.end(), inventoryModel.loomPattern);
        view.selectedStationRecipe = selected == view.loomPatterns.end() ? -1 : int(selected - view.loomPatterns.begin());
    }
    view.stationOptions.clear();
    if (inventoryModel.type == ContainerType::Stonecutter) {
        for (const auto& recipe : inventoryModel.recipes) {
            if ((recipe.recipe.mBlockName == "stonecutter" || recipe.recipe.mBlockName == "minecraft:stonecutter")
                && recipe.recipe.mInputs.size() == 1 && inventoryModel.ingredientMatches(recipe.recipe.mInputs.front(), inventoryModel.slots[Ui + 3])) {
                InventoryCatalogItem option;
                option.item = hudItemOf(recipe.output);
                option.networkId = recipe.recipe.mRecipeNetId;
                view.stationOptions.push_back(std::move(option));
            }
        }
    }
    view.windowId = inventoryModel.windowId;
    view.type = inventoryModel.type;
    view.containerSize = inventoryModel.containerSize;
    view.pending = pendingInventoryRequest != 0;
    view.experienceLevel = hud.level;
    view.craftable.clear();
    for (const auto& recipe : inventoryModel.recipes) if (inventoryModel.canCraft(recipe)) view.craftable.push_back(recipe.recipe.mRecipeNetId);
    ++view.revision;
}

void Session::handleInventoryPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto block = std::dynamic_pointer_cast<BlockActorDataPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        auto& view = current.hud.container;
        if (inventoryModel.windowId != 0 && block->mData.isCompound()
            && view.blockPosition == std::array<int, 3> { block->mBlockPosition.x, block->mBlockPosition.y, block->mBlockPosition.z }) {
            view.disabledSlots = integerTag(block->mData, "disabled_slots", view.disabledSlots);
            view.beaconLevel = integerTag(block->mData, "Levels", view.beaconLevel);
            inventoryModel.disabledSlots = view.disabledSlots;
            view.customName = block->mData.getString("CustomName", view.customName);
            ++view.revision;
        }
    } else if (auto trims = std::dynamic_pointer_cast<TrimDataPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        inventoryModel.trimPatterns.clear();
        inventoryModel.trimMaterials.clear();
        for (const auto& pattern : trims->mPatterns) {
            inventoryModel.trimPatterns[pattern.mItemName] = pattern.mPatternId;
        }
        for (const auto& material : trims->mMaterials) {
            inventoryModel.trimMaterials[material.mItemName] = material.mMaterialId;
        }
        publishInventory();
    } else if (auto options = std::dynamic_pointer_cast<PlayerEnchantOptionsPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.hud.container.enchantments = options->mOptions;
        publishInventory();
    } else if (auto dialogue = std::dynamic_pointer_cast<NpcDialoguePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        auto& view = current.hud.container;
        if (dialogue->mAction == NpcDialoguePacket::Action::Close) {
            if (view.screen == "npc_interact.npc_screen" && view.npcId == uint64_t(dialogue->mUniqueActorId)) {
                view.screen.clear();
                ++view.closeRevision;
            }
            return;
        }
        view.screen = "npc_interact.npc_screen";
        view.npcId = uint64_t(dialogue->mUniqueActorId);
        view.scene = dialogue->mSceneName;
        view.customName = dialogue->mNpcName;
        view.dialogue = dialogue->mDialogue;
        view.npcButtons.clear();
        auto actions = dialogue->mActionJson.size() <= 1024 * 1024 ? json::parse(dialogue->mActionJson) : nullptr;
        if (actions && actions->isArray()) {
            for (size_t index = 0; index < std::min<size_t>(256, actions->mArray.size()); ++index) {
                const auto& action = actions->mArray[index];
                if (!action || !action->isObject()) {
                    continue;
                }
                const auto* mode = action->get("mode");
                const auto* label = action->get("button_name");
                if ((!mode || mode->number() == 0) && label && label->isString()) {
                    view.npcButtons.emplace_back(int(index), label->string());
                }
            }
        }
        NpcRequestPacket opening;
        opening.mRuntimeActorId = view.npcId;
        opening.mRequestType = NpcRequestPacket::RequestType::ExecuteOpeningCommands;
        opening.mActionType = 0;
        opening.mSceneName = view.scene;
        transmit(opening);
        ++view.openRevision;
        ++view.revision;
    } else if (auto content = std::dynamic_pointer_cast<InventoryContentPacket>(packet)) {
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
        inventoryModel.stationRecipe = -1;
        inventoryModel.stationName.clear();
        inventoryModel.stationNameEdited = false;
        inventoryModel.loomPattern.clear();
        current.hud.container.screen.clear();
        current.hud.container.enchantments.clear();
        current.hud.container.properties.clear();
        current.hud.container.blockIdentifier.clear();
        current.hud.container.blockPosition = { open->mBlockPosition.x, open->mBlockPosition.y, open->mBlockPosition.z };
        current.hud.container.mountIdentifier.clear();
        current.hud.container.mountRuntimeId = 0;
        current.hud.container.disabledSlots = 0;
        current.hud.container.beaconLevel = 0;
        if (auto runtime = runtimeByUnique.find(open->mUniqueActorId); runtime != runtimeByUnique.end()) {
            current.hud.container.mountRuntimeId = runtime->second;
            if (auto actor = actors.find(runtime->second); actor != actors.end()) {
                current.hud.container.mountIdentifier = actor->second.identifier;
            }
        }
        inventoryModel.mountIdentifier = current.hud.container.mountIdentifier;
        if (open->mUniqueActorId == -1 && assets) {
            uint32_t value = blockAt(open->mBlockPosition.x, open->mBlockPosition.y, open->mBlockPosition.z);
            current.hud.container.blockIdentifier = assets->blockName(value, ids.hashed, ids.sequential.get());
        }
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
                if (found != entities->end()) {
                    current.hud.container.disabledSlots = integerTag(found->second, "disabled_slots", 0);
                    current.hud.container.beaconLevel = integerTag(found->second, "Levels", 0);
                    if (open->mType == ContainerType::Lectern) {
                        auto& view = current.hud.container;
                        view.bookEditable = false;
                        view.bookSlot = -1;
                        view.pages.clear();
                        const Tag* book = found->second.get("book");
                        const Tag* tag = book && book->isCompound() ? book->get("tag") : nullptr;
                        const Tag* pages = tag && tag->isCompound() ? tag->get("pages") : nullptr;
                        if (pages && pages->isList()) {
                            for (const auto& page : pages->getList()) {
                                if (view.pages.size() >= 50) {
                                    break;
                                }
                                view.pages.push_back(page.isCompound() ? page.getString("text", "") : std::string());
                            }
                            view.author = tag->getString("author", "");
                            view.bookTitle = tag->getString("title", "");
                        }
                    }
                }
            }
        }
        for (int i = Container; i < SlotCount; ++i) inventoryModel.slots[i] = ItemStack::air();
        inventoryModel.slots[Output] = ItemStack::air();
        inventoryModel.disabledSlots = current.hud.container.disabledSlots;
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
                        if (!update.mCustomName.empty() || !update.mFilteredCustomName.empty() || update.mDurabilityCorrection != 0) {
                            if (!item.mTag.isCompound()) {
                                item.mTag = Tag::ofCompound();
                            }
                            if (!update.mCustomName.empty() || !update.mFilteredCustomName.empty()) {
                                const Tag* previous = item.mTag.get("display");
                                Tag display = previous && previous->isCompound() ? *previous : Tag::ofCompound();
                                display.putString("Name", update.mFilteredCustomName.empty() ? update.mCustomName : update.mFilteredCustomName);
                                item.mTag.put("display", std::move(display));
                            }
                            if (update.mDurabilityCorrection != 0) {
                                item.mTag.putInt("Damage", update.mDurabilityCorrection);
                            }
                            item.mUserData.clear();
                        }
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
        if (recipes->mCleanRecipes) {
            inventoryModel.repairRecipe = -1;
        }
        for (const auto& recipe : recipes->mMultiRecipes) {
            if (recipe.mUuid == Uuid::fromString("00000000-0000-0000-0000-000000000001")) {
                inventoryModel.repairRecipe = recipe.mRecipeNetId;
            }
        }
        auto append = [&](const auto& entries, bool shaped) {
            for (const auto& entry : entries) {
                if (entry.mOutputs.empty()) continue;
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
        std::vector<CraftingRecipeEntry> smithing;
        for (const auto& entry : recipes->mSmithingTransformRecipes) {
            CraftingRecipeEntry recipe;
            recipe.mRecipeId = entry.mRecipeId;
            recipe.mBlockName = entry.mBlockName;
            recipe.mRecipeNetId = entry.mRecipeNetId;
            recipe.mInputs = { entry.mTemplate, entry.mInput, entry.mAddition };
            recipe.mOutputs = { entry.mOutput };
            smithing.push_back(std::move(recipe));
        }
        append(smithing, false);
        for (const auto& entry : recipes->mSmithingTrimRecipes) {
            InventoryRecipe recipe;
            recipe.recipe.mRecipeId = entry.mRecipeId;
            recipe.recipe.mBlockName = entry.mBlockName;
            recipe.recipe.mRecipeNetId = entry.mRecipeNetId;
            recipe.recipe.mInputs = { entry.mTemplate, entry.mInput, entry.mAddition };
            recipe.trim = true;
            std::erase_if(inventoryModel.recipes, [&](const auto& existing) {
                return existing.recipe.mRecipeNetId == entry.mRecipeNetId;
            });
            inventoryModel.recipes.push_back(std::move(recipe));
        }
        auto catalog = std::make_shared<std::vector<InventoryCatalogItem>>();
        for (const auto& recipe : inventoryModel.recipes) {
            if (recipe.recipe.mBlockName != "crafting_table" && recipe.recipe.mBlockName != "minecraft:crafting_table") {
                continue;
            }
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
            auto& view = current.hud.container;
            view.properties[data->mProperty] = data->mValue;
            float duration = inventoryModel.type == ContainerType::Furnace ? 200.0f : 100.0f;
            view.furnaceProgress = std::clamp(view.properties[0] / duration, 0.0f, 1.0f);
            int total = view.properties[2];
            view.furnaceFlame = total > 0 ? std::clamp(float(view.properties[1]) / float(total), 0.0f, 1.0f) : 0.0f;
            ++view.revision;
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
    auto& view = current.hud.container;
    if (inventoryModel.type == ContainerType::Lectern && command.action == InventoryAction::BookPage) {
        if (command.slot >= 0 && command.slot < int(view.pages.size())) {
            LecternUpdatePacket page;
            page.mPage = uint8_t(command.slot);
            page.mTotalPages = uint8_t(std::min(size_t(255), view.pages.size()));
            page.mBlockPosition = Vector3i(view.blockPosition[0], view.blockPosition[1], view.blockPosition[2]);
            transmit(page);
        }
        return;
    }
    if (command.action == InventoryAction::ToggleCrafter) {
        if (inventoryModel.type != ContainerType::Crafter || command.slot < 0 || command.slot >= 9 || !inventoryModel.slots[Container + command.slot].isAir()) {
            return;
        }
        PlayerToggleCrafterSlotRequestPacket toggle;
        toggle.mBlockPosition = Vector3i(view.blockPosition[0], view.blockPosition[1], view.blockPosition[2]);
        toggle.mSlot = int8_t(command.slot);
        toggle.mDisabled = (view.disabledSlots & (1 << command.slot)) == 0;
        transmit(toggle);
        view.disabledSlots ^= 1 << command.slot;
        inventoryModel.disabledSlots = view.disabledSlots;
        ++view.revision;
        return;
    }
    if (view.screen == "book.book_screen") {
        if (command.action == InventoryAction::Close) {
            view.screen.clear();
            return;
        }
        if (!view.bookEditable || view.bookSlot < 0 || view.bookSlot >= 36) {
            return;
        }
        if (command.action != InventoryAction::BookPage && command.action != InventoryAction::BookSign) {
            return;
        }
        BookEditPacket edit;
        edit.mInventorySlot = view.bookSlot;
        edit.mPageNumber = command.slot;
        edit.mSecondaryPageNumber = command.value;
        edit.mText = command.text;
        if (command.action == InventoryAction::BookSign) {
            if (command.text.empty() || command.text.size() > 64) {
                return;
            }
            edit.mAction = BookEditPacket::Action::SignBook;
            edit.mTitle = command.text;
            edit.mAuthor = view.author;
            view.bookEditable = false;
            view.bookTitle = command.text;
        } else {
            if (command.slot < 0 || command.slot >= 50 || command.text.size() > 4096) {
                return;
            }
            if (command.value == -1) {
                if (view.pages.size() >= 50 || command.slot > int(view.pages.size())) {
                    return;
                }
                edit.mAction = BookEditPacket::Action::AddPage;
                view.pages.insert(view.pages.begin() + command.slot, command.text);
            } else if (command.value == -2) {
                if (view.pages.size() <= 1 || command.slot >= int(view.pages.size())) {
                    return;
                }
                edit.mAction = BookEditPacket::Action::DeletePage;
                view.pages.erase(view.pages.begin() + command.slot);
            } else if (command.value >= 0) {
                if (command.slot >= int(view.pages.size()) || command.value >= int(view.pages.size())) {
                    return;
                }
                edit.mAction = BookEditPacket::Action::SwapPages;
                std::swap(view.pages[command.slot], view.pages[command.value]);
            } else {
                if (command.slot >= int(view.pages.size())) {
                    return;
                }
                edit.mAction = BookEditPacket::Action::ReplacePage;
                view.pages[command.slot] = command.text;
            }
        }
        transmit(edit);
        ++view.revision;
        return;
    }
    if (view.screen == "npc_interact.npc_screen") {
        if (command.action == InventoryAction::NpcAction || command.action == InventoryAction::Close) {
            NpcRequestPacket reply;
            reply.mRuntimeActorId = view.npcId;
            reply.mSceneName = view.scene;
            reply.mActionType = 0;
            reply.mRequestType = NpcRequestPacket::RequestType::ExecuteClosingCommands;
            if (command.action == InventoryAction::NpcAction) {
                if (command.value < 0 || command.value >= int(view.npcButtons.size())) {
                    return;
                }
                reply.mActionType = view.npcButtons[command.value].first;
                reply.mRequestType = NpcRequestPacket::RequestType::ExecuteCommandAction;
            } else {
                view.screen.clear();
            }
            transmit(reply);
        }
        return;
    }
    inventoryModel.creativeMode = current.hud.gameType == 1;
    if (command.action == InventoryAction::Rename && command.text.size() > 512) {
        return;
    }
    if (command.slot == Output && inventoryModel.type == ContainerType::Anvil && !inventoryModel.creativeMode) {
        int cost = 0;
        inventoryModel.anvilPreview(nullptr, &cost);
        if (cost > current.hud.level) {
            return;
        }
    }
    if (command.action == InventoryAction::Enchant) {
        if (inventoryModel.type != ContainerType::Enchantment || command.value < 0 || command.value >= int(view.enchantments.size())) {
            return;
        }
        const auto& option = view.enchantments[command.value];
        if (!inventoryModel.creativeMode && (current.hud.level < option.mCost || inventoryModel.slots[Ui + 15].mCount <= command.value)) {
            return;
        }
        command.value = option.mEnchantNetId;
    }
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
