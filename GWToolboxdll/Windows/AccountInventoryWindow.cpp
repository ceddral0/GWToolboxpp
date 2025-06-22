#include "stdafx.h"

#include <GWCA/GameContainers/Array.h>

#include <GWCA/GameEntities/Hero.h>

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Context/CharContext.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/ItemContext.h>
#include <GWCA/Context/WorldContext.h>

#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Managers/ItemMgr.h>

#include <GWCA/Utilities/Hook.h>

#include <Logger.h>
#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Modules/InventoryManager.h>
#include <Windows/CompletionWindow.h>
#include <Windows/RerollWindow.h>
#include <Windows/AccountInventoryWindow.h>

#include <GWToolbox.h>
#include <Utils/TextUtils.h>
#include <string.h>
#include <algorithm>

#include <type_traits>

#include <fileapi.h>

std::unordered_map<int, uint32_t> AccountInventoryWindow::ChestModelIDToHeroID = {
    {17979, GW::Constants::HeroID::Koss},
    {20080, GW::Constants::HeroID::Koss},
    {20081, GW::Constants::HeroID::Koss},
    {19015, GW::Constants::HeroID::Dunkoro},
    {20090, GW::Constants::HeroID::Dunkoro},
    {20091, GW::Constants::HeroID::Dunkoro},
    {19020, GW::Constants::HeroID::Tahlkora},
    {20055, GW::Constants::HeroID::Tahlkora},
    {20056, GW::Constants::HeroID::Tahlkora},
    {19025, GW::Constants::HeroID::Melonni},
    {20100, GW::Constants::HeroID::Melonni},
    {20101, GW::Constants::HeroID::Melonni},
    {19030, GW::Constants::HeroID::AcolyteJin},
    {20110, GW::Constants::HeroID::AcolyteJin},
    {20111, GW::Constants::HeroID::AcolyteJin},
    {19035, GW::Constants::HeroID::AcolyteSousuke},
    {20120, GW::Constants::HeroID::AcolyteSousuke},
    {20121, GW::Constants::HeroID::AcolyteSousuke},
    {19966, GW::Constants::HeroID::Zenmai},
    {19967, GW::Constants::HeroID::Zenmai},
    {19968, GW::Constants::HeroID::Zenmai},
    {19981, GW::Constants::HeroID::Norgu},
    {19982, GW::Constants::HeroID::Norgu},
    {19983, GW::Constants::HeroID::Norgu},
    {19996, GW::Constants::HeroID::Goren},
    {19997, GW::Constants::HeroID::Goren},
    {19998, GW::Constants::HeroID::Goren},
    {20011, GW::Constants::HeroID::ZhedShadowhoof},
    {20012, GW::Constants::HeroID::ZhedShadowhoof},
    {20013, GW::Constants::HeroID::ZhedShadowhoof},
    {20026, GW::Constants::HeroID::GeneralMorgahn},
    {20027, GW::Constants::HeroID::GeneralMorgahn},
    {20028, GW::Constants::HeroID::GeneralMorgahn},
    {20041, GW::Constants::HeroID::MargridTheSly},
    {20042, GW::Constants::HeroID::MargridTheSly},
    {20043, GW::Constants::HeroID::MargridTheSly},
    {20066, GW::Constants::HeroID::MasterOfWhispers},
    {20067, GW::Constants::HeroID::MasterOfWhispers},
    {20068, GW::Constants::HeroID::MasterOfWhispers},
    {20131, GW::Constants::HeroID::Olias},
    {20132, GW::Constants::HeroID::Olias},
    {20133, GW::Constants::HeroID::Olias},
    {25997, GW::Constants::HeroID::Anton},
    {25998, GW::Constants::HeroID::Anton},
    {25999, GW::Constants::HeroID::Anton},
    {26012, GW::Constants::HeroID::Gwen},
    {26013, GW::Constants::HeroID::Gwen},
    {26014, GW::Constants::HeroID::Gwen},
    {26027, GW::Constants::HeroID::Livia},
    {26028, GW::Constants::HeroID::Livia},
    {26029, GW::Constants::HeroID::Livia},
    {26042, GW::Constants::HeroID::Vekk},
    {26043, GW::Constants::HeroID::Vekk},
    {26044, GW::Constants::HeroID::Vekk},
    {26057, GW::Constants::HeroID::Ogden},
    {26058, GW::Constants::HeroID::Ogden},
    {26059, GW::Constants::HeroID::Ogden},
    {26072, GW::Constants::HeroID::Jora},
    {26073, GW::Constants::HeroID::Jora},
    {26074, GW::Constants::HeroID::Jora},
    {26087, GW::Constants::HeroID::PyreFierceshot},
    {26088, GW::Constants::HeroID::PyreFierceshot},
    {26089, GW::Constants::HeroID::PyreFierceshot},
    {26102, GW::Constants::HeroID::Kahmu},
    {26103, GW::Constants::HeroID::Kahmu},
    {26104, GW::Constants::HeroID::Kahmu},
    {26117, GW::Constants::HeroID::Xandra},
    {26118, GW::Constants::HeroID::Xandra},
    {26119, GW::Constants::HeroID::Xandra},
    {26132, GW::Constants::HeroID::Hayda},
    {26133, GW::Constants::HeroID::Hayda},
    {26134, GW::Constants::HeroID::Hayda},
    {30240, GW::Constants::HeroID::Miku},
    {30849, GW::Constants::HeroID::MOX},
    {36079, GW::Constants::HeroID::KeiranThackeray},
    {36542, GW::Constants::HeroID::Razah},
    {36543, GW::Constants::HeroID::Razah},
    {36544, GW::Constants::HeroID::Razah},
    {30235, GW::Constants::HeroID::ZeiRi},
};
std::vector<std::string> AccountInventoryWindow::HeroName = {
    "(Player)", "Norgu", "Goren", "Tahlkora",
    "Master Of Whispers", "Acolyte Jin", "Koss", "Dunkoro",
    "Acolyte Sousuke", "Melonni", "Zhed Shadowhoof",
    "General Morgahn", "Margrid The Sly", "Zenmai",
    "Olias", "Razah", "MOX", "Keiran Thackeray", "Jora",
    "Pyre Fierceshot", "Anton", "Livia", "Hayda",
    "Kahmu", "Gwen", "Xandra", "Vekk", "Ogden",
    "Mercenary Hero 1", "Mercenary Hero 2", "Mercenary Hero 3",
    "Mercenary Hero 4", "Mercenary Hero 5", "Mercenary Hero 6",
    "Mercenary Hero 7", "Mercenary Hero 8", "Miku", "Zei Ri",
    "Unknown"
};
std::vector<std::string> AccountInventoryWindow::BagName = {
    "", "Backpack", "Belt Pouch", "Bag 1", "Bag 2", "Equipment Pack",
    "Material Storage", "Unclaimed Items", "Storage 1", "Storage 2",
    "Storage 3", "Storage 4", "Storage 5", "Storage 6", "Storage 7",
    "Storage 8", "Storage 9", "Storage 10", "Storage 11", "Storage 12",
    "Storage 13", "Storage 14", "Equipped Items"
};
std::vector<uint32_t> AccountInventoryWindow::BagMaxSize = {
    0, 20, 10, 15, 15, 20,
    36, 12, 25, 25,
    25, 25, 25, 25, 25,
    25, 25, 25, 25, 25,
    25, 25, 9
};
std::vector<bool> AccountInventoryWindow::BagCanHoldAnything = {
    false, true, true, true, true, false,
    false, false, true, true,
    true, true, true, true, true,
    true, true, true, true, true,
    true, true, false
};

static bool IsChestBag(uint32_t bag_id) {
    if ((uint32_t)GW::Constants::Bag::Material_Storage == bag_id) return true;
    if ((uint32_t)GW::Constants::Bag::Storage_1 <= bag_id && bag_id <= (uint32_t)GW::Constants::Bag::Storage_14) return true;
    return false;
}

static bool IsHeroArmor(uint32_t hero_id, uint32_t slot) {
    return hero_id != (uint32_t)GW::Constants::HeroID::NoHero && slot >= 2;
}

static bool IsOnHero(uint32_t hero_id) {
    return (uint32_t)GW::Constants::HeroID::NoHero < hero_id && hero_id < (uint32_t)GW::Constants::HeroID::AllHeroes;
}

static bool GetIsMapReady()
{
    return GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading && GW::Map::GetIsMapLoaded() && GW::Agents::GetControlledCharacter();
}

static ImVec4 HSVRotate(ImVec4 color, float hue)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    return (ImVec4)ImColor::HSV(hue, s, v, color.w);
}

static std::wstring GetAccountEmail()
{
    const auto c = GW::GetCharContext();
    return c ? c->player_email : L"";
}

static void RightAlignText(std::string text)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x - ImGui::GetScrollX());
    ImGui::Text("%s", text.c_str());
}

std::wstring AccountInventoryWindow::getIniID(std::wstring account, std::wstring character) {
    if (character == L"(Chest)") {
        return account;
    } else {
        return character;
    }
}

bool AccountInventoryWindow::checkIniDirty(std::shared_ptr<InventoryIni> ini)
{
    FILETIME change_time;
    HANDLE f = CreateFileW(ini->location_on_disk.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool res = GetFileTime(f, NULL, NULL, &change_time);
    CloseHandle(f);
    if (!res) return false;
    if (CompareFileTime(&change_time, &ini->last_change_time) != 0) {
        ini->last_change_time = change_time;
        return true;
    }
    return false;
}

// jump to location of clicked item, i.e. open chest/add hero/change character
// with Ctrl: move item to/from chest after jump
void AccountInventoryWindow::OnInventoryItemClicked(std::shared_ptr<InventoryItem> i, bool move) {
    if (mapLoadedDelayedTrigger || rerollStage != RerollStage::None || GW::Map::GetInstanceType() != GW::Constants::InstanceType::Outpost) return;
    std::wstring currentAccount = GetAccountEmail();
    if (i->account != currentAccount) return;
    if (move) {
        if (IsHeroArmor(i->hero_id, i->slot)) return; // can not unequip hero armor
        itemToMove = i;
    } else {
        itemToMove = nullptr;
    }
    bool itemIsInChest = IsChestBag(i->bag_id);
    bool itemIsOnCurrentCharacter = i->character == std::wstring(GW::AccountMgr::GetCurrentPlayerName());
    bool itemIsOnHero = (uint32_t)GW::Constants::HeroID::NoHero != i->hero_id;
    if (itemIsOnHero) {
        cachedHeroes.clear();
        cachedHeroes.push_back(i->hero_id);
    }

    if (itemIsInChest && itemToMove) {
        MoveItem();
    } else if (itemIsInChest && !itemToMove) {
        GW::GameThread::Enqueue([]() { GW::Items::OpenXunlaiWindow(); });
    } else if (itemIsOnCurrentCharacter && itemIsOnHero) {
        // MoveItem will be triggered through StepReroll if the hero is already present.
        // Otherwise through AddItem, once the heroes inventory has been added by GW
        rerollStage = RerollStage::RerollToItem;
        StepReroll();
    } else if (itemIsOnCurrentCharacter && !itemIsOnHero && itemToMove) {
        MoveItem();
    } else if (!itemIsOnCurrentCharacter) {
        // MoveItem will be triggered through StepReroll if the item is on a player character or
        // if the corresponding hero is already present.
        // Otherwise through AddItem, once the heroes inventory has been added by GW
        rerollStage = RerollStage::RerollToItem;
        if (!RerollWindow::Instance().Reroll(i->character.c_str(), false, false, true, false)) {
            rerollStage = RerollStage::None;
            itemToMove = nullptr;
        }
    }
}

void AccountInventoryWindow::MoveItem()
{
    if (!itemToMove) return;
    std::shared_ptr<InventoryItem> i{};
    auto it = inventory.find(itemToMove);
    itemToMove = nullptr;
    if (it == inventory.end()) return;
    i = *it;

    // can only move from current player or from chest
    if (i->character != std::wstring(GW::AccountMgr::GetCurrentPlayerName()) && !IsChestBag(i->bag_id)) return;

    GW::GameThread::Enqueue([i=i] {
        auto item = GW::Items::GetItemById(i->itemPartial.item_id);
        // plausibilize that our item_id was up to date, this should only be false immediately after loading a new map or if toolbox was not running when this map was loaded
        if (item && item->bag && item->slot == i->slot && (uint32_t)item->bag->bag_id() == i->bag_id && item->model_id == i->itemPartial.model_id) {
            if (!IsChestBag(i->bag_id)) GW::Items::OpenXunlaiWindow();
            InventoryManager::MoveItem((InventoryManager::Item *)item);
        }
    });
}

void AccountInventoryWindow::Initialize()
{
    ToolboxWindow::Initialize();
    const GW::UI::UIMessage ui_messages[] = {
        GW::UI::UIMessage::kItemUpdated,
        GW::UI::UIMessage::kEquipmentSlotUpdated,
        GW::UI::UIMessage::kInventorySlotUpdated,
        GW::UI::UIMessage::kEquipmentSlotCleared,
        GW::UI::UIMessage::kInventorySlotCleared,
        GW::UI::UIMessage::kPartyAddHero,
        GW::UI::UIMessage::kMapChange,
        GW::UI::UIMessage::kMapLoaded,
        GW::UI::UIMessage::kLogout
    };
    for (auto message_id : ui_messages) {
        GW::UI::RegisterUIMessageCallback(&OnUIMessage_HookEntry, (GW::UI::UIMessage)message_id,
            [this] (GW::HookStatus*, GW::UI::UIMessage message_id, void* wparam, void*) {
                switch (message_id) {
                    case GW::UI::UIMessage::kItemUpdated: {
                        const auto p = (GW::UI::UIPacket::kItemUpdated*)wparam;
                        AddItem(p->item_id);
                        break;
                    }
                    case GW::UI::UIMessage::kEquipmentSlotUpdated:
                    case GW::UI::UIMessage::kInventorySlotUpdated: {
                        const auto p = (GW::UI::UIPacket::kInventorySlotUpdated*)wparam;
                        AddItem(p->item_id);
                        break;
                    }
                    case GW::UI::UIMessage::kEquipmentSlotCleared:
                    case GW::UI::UIMessage::kInventorySlotCleared: {
                        const auto p = (GW::UI::UIPacket::kInventorySlotUpdated*)wparam;
                        RemoveItem(p->item_id);
                        break;
                    }
                    case GW::UI::UIMessage::kPartyAddHero:
                        OnPartyAddHero();
                        break;
                    case GW::UI::UIMessage::kMapChange:
                        PreMapLoad();
                        break;
                    case GW::UI::UIMessage::kMapLoaded:
                        PostMapLoad();
                        break;
                    case GW::UI::UIMessage::kLogout: {
                        SaveToFiles();
                        break;
                    }
                }
            }
        );
    }
    LoadFromFiles(false);
    auto ic = GW::GetItemContext();
    if (ic) {
        // fake a map load to clear missing items and remove deleted characters
        PreMapLoad();
        for (auto const &i: ic->item_array) {
            if (i) {
                AddItem(i->item_id);
            }
        }
        PostMapLoad();
    }
}

void AccountInventoryWindow::Terminate()
{
    GW::UI::RemoveUIMessageCallback(&OnUIMessage_HookEntry);
    inventory.clear();
    inventoryLookup.clear();
    inventorySorted.clear();
    freeSlots.clear();
    iniByPath.clear();
    iniByCharacter.clear();
    inventoryDirty.clear();
    rerollHeroQueue.clear();
    rerollCharQueue.clear();
    lastCharacter = L"";
    lastAvailableChars.clear();
    rerollStage = RerollStage::None;
    mapLoadedDelayedTrigger = false;
    ToolboxWindow::Terminate();
}

void AccountInventoryWindow::Update(float)
{
    {
        std::lock_guard<std::mutex> guard(descriptionDecodeLock);
        while (!descriptionDecodeQueue.empty()) {
            auto q = descriptionDecodeQueue.front();
            q->i->description = q->description;
            descriptionDecodeQueue.pop();
            needsSorting = true;
        }
    }
    if (mapLoadedDelayedTrigger && TIMER_DIFF(mapLoadedDelayedTimer) > mapLoadedDelayedTimeout) {
        OnMapLoadedDelayed();
    }
    // wait until after OnMapLoadedDelayed to continue rerolling
    if (!mapLoadedDelayedTrigger && rerollStage == RerollStage::DoSaveHeroes && TIMER_DIFF(saveHeroTimer) > saveHeroTimeout) {
        SaveHeroes();
        rerollStage = RerollStage::DoneCharacterLoad;
        StepReroll();
    }
}

void AccountInventoryWindow::LoadAllInventories()
{
    rerollHeroQueue.clear();
    rerollCharQueue.clear();
    cachedHeroes.clear();
    auto available_characters = GW::AccountMgr::GetAvailableChars();
    for (const auto& available_char : *available_characters) {
        const auto char_select_info = GW::AccountMgr::GetAvailableCharacter(available_char.player_name);
        if (!char_select_info) {
            continue;
        }
        if (wcscmp(available_char.player_name, GW::AccountMgr::GetCurrentPlayerName()) == 0) {
            rerollCharQueue.insert(rerollCharQueue.begin(), available_char);
        } else {
            rerollCharQueue.push_back(available_char);
            const auto reroll_to_player_current_map = char_select_info->map_id();
            if (GWToolbox::ShouldDisableToolbox(reroll_to_player_current_map)) {
                const auto charname_str = TextUtils::WStringToString(char_select_info->player_name);
                const auto msg = std::format("{} is currently in {}.\n"
                    "This is an outpost that toolbox won't work in.\n"
                    "All characters must be in outposts where toolbox can work,\n"
                    "e.g. Great Temple of Balthazar.\n\n"
                    "Reroll to {} so you can move it to another outpost?",
                    charname_str, Resources::GetMapName(reroll_to_player_current_map)->string(), charname_str);
                ImGui::ConfirmDialog(msg.c_str(), [](bool result, void*){if (result) AccountInventoryWindow::Instance().OnRerollPromptReply();});
                return;
            }
        }
    }
    rerollStage = RerollStage::NextCharacter;
    StepReroll();
}

void AccountInventoryWindow::OnRerollPromptReply()
{
    auto nextChar = rerollCharQueue.back();
    rerollCharQueue.clear();
    RerollWindow::Instance().Reroll(nextChar.player_name, false, false, true, true);
}

void AccountInventoryWindow::OnMapLoadedDelayed()
{
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        // not done loading, retry later
        mapLoadedDelayedTimer = TIMER_INIT();
        return;
    }
    mapLoadedDelayedTrigger = false;
    bool characterChanged = false;
    std::wstring currentAccount = GetAccountEmail();
    std::wstring currentCharacter = GW::AccountMgr::GetCurrentPlayerName();
    if (lastCharacter != currentCharacter) {
        lastCharacter = currentCharacter;
        characterChanged = true;
    }
    if (characterChanged) {
        std::set<std::wstring> availableChars{};
        const auto chars = GW::AccountMgr::GetAvailableChars();
        for (const auto& availableCharacter : *chars) {
            availableChars.insert(availableCharacter.player_name);
        }
        if (availableChars != lastAvailableChars) {
            // erase deleted characters
            for (auto it = inventory.begin(); it != inventory.end();) {
                if (!IsChestBag((*it)->bag_id) && (*it)->account == currentAccount) {
                    if (auto avail = availableChars.find((*it)->character); avail == availableChars.end()) {
                        // not found
                        inventoryDirty.insert(getIniID((*it)->account, (*it)->character));
                        freeSlots.erase(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, (*it)->character}));
                        inventoryLookup.erase((*it)->itemPartial.item_id);
                        it = inventory.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
        lastAvailableChars = availableChars;
    }
    if (rerollStage == RerollStage::RerollToItem) {
        StepReroll();
        return;
    }
}

void AccountInventoryWindow::PreMapLoad()
{
    std::wstring currentAccount = GetAccountEmail();
    std::wstring currentCharacter = GW::AccountMgr::GetCurrentPlayerName();
    inventoryLookup.clear(); // discard now outdated item_id cache
    std::wstring characters[] = {L"(Chest)", currentCharacter};
    for (auto & character: characters) {
        if (character.empty()) continue;
        auto it = freeSlots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, character}));
        std::shared_ptr<CharacterFreeSlots> freeSlot = nullptr;
        if (it != freeSlots.end()) {
            freeSlot = (*it);
        } else {
            freeSlot = std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, character});
            auto available_characters = GW::AccountMgr::GetAvailableChars();
            if (available_characters->size() > 0) {
                // alphabetically first character name, to be shown in tooltip to distinguish chests from multiple accounts without showing email addresses
                const wchar_t *min = nullptr;
                for (const auto& available_char : *available_characters) {
                    if (!min || wcscmp(available_char.player_name, min) < 0) min = available_char.player_name;
                }
                freeSlot->account_representing_character = min;
            }
            freeSlots.insert(freeSlot);
        }
        if (character == currentCharacter) {
            freeSlot->occupied_equipment = 0;
            freeSlot->occupied_inventory = 0;
        } else {
            freeSlot->occupied_inventory = 0;
        }
    }
}

void AccountInventoryWindow::PostMapLoad()
{
    mapLoadedDelayedTrigger = true;
    mapLoadedDelayedTimer = TIMER_INIT();
    std::wstring currentAccount = GetAccountEmail();
    std::wstring currentCharacter = GW::AccountMgr::GetCurrentPlayerName();
    GW::Inventory* gwInventory = GW::Items::GetInventory();
    if (gwInventory) {
        uint32_t max_chest = 0;
        uint32_t max_equipment = 0;
        uint32_t max_inventory = 0;
        bool lastChestPaneContainsAnyItem = false;
        for (uint32_t bag_id = 1; bag_id < std::size(gwInventory->bags); ++bag_id) {
            auto bag = gwInventory->bags[bag_id];
            std::wstring character = currentCharacter;
            if (IsChestBag(bag_id)) {
                character = L"(Chest)";
            }
            if (!bag) {
                for (uint32_t slot = 0; slot < (uint32_t)BagMaxSize[bag_id]; ++slot) {
                    ClearMissingItem(currentAccount, character, GW::Constants::HeroID::NoHero, bag_id, slot);
                }
                continue;
            }
            if (IsChestBag(bag_id)) {
                lastChestPaneContainsAnyItem = false;
            }
            for (uint32_t slot = 0; slot < std::size(bag->items); ++slot) {
                auto item = bag->items[slot];
                if (item) {
                    if (IsChestBag(bag_id)) {
                        lastChestPaneContainsAnyItem = true;
                    }
                    if (!inventoryLookup.contains(item->item_id)) {
                        // should never happen
                        Log::Error("AccountInventoryWindow: Item present in GW Inventory but not in Account Inventory. Please report a bug.");
                        continue;
                    }
                    // item->equipped is never set when an item triggers InventorySlotUpdated on map load.
                    // manually check and reapply after every map load.
                    auto i = inventoryLookup[item->item_id];
                    if (i->itemPartial.equipped != item->equipped) {
                        i->itemPartial.equipped = item->equipped;
                        inventoryDirty.insert(getIniID(i->account, i->character));
                    }
                } else {
                    // on mapLoad keep count of empty inventory slots and
                    // clear them, in case inventory was modified without us tracking it
                    ClearMissingItem(currentAccount, character, GW::Constants::HeroID::NoHero, bag_id, slot);
                }
            }
            if (bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
                max_equipment = std::size(bag->items);
            } else if (BagCanHoldAnything[bag_id]) {
                if (IsChestBag(bag_id)) {
                    max_chest += std::size(bag->items);
                } else {
                    max_inventory += std::size(bag->items);
                }
            }
        }
        std::wstring characters[] = {L"(Chest)", currentCharacter};
        for (auto & character: characters) {
            if (auto it = freeSlots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, character})); it != freeSlots.end()) {
                if (character == currentCharacter) {
                    (*it)->max_equipment = max_equipment;
                    (*it)->max_inventory = max_inventory;
                } else {
                    // Since we do not know whether the anniversary storage pane is actually available,
                    // assume it is not, unless there has been at least one item in it at some point.
                    if ((*it)->anniversary_pane_active || lastChestPaneContainsAnyItem) {
                        (*it)->anniversary_pane_active = true;
                    } else {
                        max_chest -= 25;
                    }
                    (*it)->max_inventory = max_chest;
                }
            }
        }
    }
    needsSorting = true; // show updated equipped flags and removed characters
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        SaveToFiles(); // save inventory in outposts only to avoid impacting gameplay
    }
    switch (rerollStage) {
        case RerollStage::None:
            break;
        case RerollStage::WaitForCharacterLoad:
            saveHeroTimer = TIMER_INIT();
            rerollStage = RerollStage::DoSaveHeroes;
            break;
        // RerollStage::RerollToItem is handled in OnMapLoadedDelayed as it might call MoveItem,
        // which does not work immediately after loading into a map
    }
}

void AccountInventoryWindow::OnPartyAddHero()
{
    if (rerollStage == RerollStage::None) return;
    switch (rerollStage) {
        case RerollStage::WaitForHeroLoad:
            rerollStage = RerollStage::DoneHeroLoad;
            StepReroll();
            return;
        case RerollStage::DoRestoreHeroes:
            const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
            if (party_info) {
                if (party_info->heroes.size() < cachedHeroes.size()) return;
            }
            cachedHeroes.clear();
            rerollStage = RerollStage::NextCharacter;
            StepReroll();
            return;
    }
}

void AccountInventoryWindow::SaveHeroes()
{
    if (!cachedHeroes.empty()) return;
    const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
    const GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
    if (!(party_info && me)) return;
    for (const auto &hero: party_info->heroes) {
        if (hero.owner_player_id != me->login_number) continue;
        cachedHeroes.push_back(hero.hero_id);
    }
}

void AccountInventoryWindow::RestoreHeroes()
{
    GW::PartyMgr::KickAllHeroes();
    if (cachedHeroes.empty()) {
        rerollStage = RerollStage::NextCharacter;
        StepReroll();
        return;
    }
    for (const auto hero_id: cachedHeroes) {
        GW::PartyMgr::AddHero(hero_id);
    }
}

void AccountInventoryWindow::StepReroll()
{
    GW::AvailableCharacterInfo nextChar{};
    uint32_t nextHero{};
    switch (rerollStage) {
        case RerollStage::NextCharacter:
            if (rerollCharQueue.empty()) {
                rerollStage = RerollStage::None;
                SaveToFiles();
                return;
            }
            rerollStage = RerollStage::WaitForCharacterLoad;
            nextChar = rerollCharQueue.back();
            rerollCharQueue.pop_back();
            rerollHeroQueue = CompletionWindow::GetCharacterCompletion(nextChar.player_name)->heroes;
            if (!RerollWindow::Instance().Reroll(nextChar.player_name, false, false, true, false)) {
                rerollStage = RerollStage::None;
            }
            return;
        case RerollStage::DoneHeroLoad:
            if (rerollHeroQueue.empty()) {
                rerollStage = RerollStage::DoRestoreHeroes;
                RestoreHeroes();
                return;
            }
            // fall through
        case RerollStage::DoneCharacterLoad:
            if (rerollHeroQueue.empty()) {
                rerollStage = RerollStage::NextCharacter;
                StepReroll();
                return;
            }
            rerollStage = RerollStage::WaitForHeroLoad;
            nextHero = rerollHeroQueue.back();
            rerollHeroQueue.pop_back();
            GW::PartyMgr::KickAllHeroes();
            GW::PartyMgr::AddHero(nextHero);
            return;
        case RerollStage::RerollToItem:
            rerollStage = RerollStage::None;
            if (cachedHeroes.empty()) {
                // no cachedHeroes means we rerolled for an item on the player character
                // move the item, but skip any hero related setup
                MoveItem();
                return;
            }
            auto hero_id = cachedHeroes.front();
            // do not kick heroes, if the one we need is already added
            bool heroIsPresent = false;
            const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
            const GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
            if (party_info && me) {
                for (const auto &hero: party_info->heroes) {
                    if (hero.owner_player_id != me->login_number) continue;
                    if (hero.hero_id != hero_id) continue;
                    heroIsPresent = true;
                    break;
                }
            }
            if (heroIsPresent) {
                // hero is already in the party, just move the item
                MoveItem();
            } else {
                // MoveItem will be triggered through AddItem, once the item is available
                GW::PartyMgr::KickAllHeroes();
                GW::PartyMgr::AddHero(hero_id);
            }
            cachedHeroes.clear();
            return;
    }
}

void AccountInventoryWindow::QueueDescriptionDecode(std::shared_ptr<InventoryItem> i)
{
    struct SyncDecode {
        std::shared_ptr<QueueDescription> q;
        std::mutex & lock;
        std::queue<std::shared_ptr<QueueDescription>> & queue;
    };
    auto sync = new SyncDecode {
        std::make_shared<QueueDescription>(QueueDescription{i, L""}),
        descriptionDecodeLock,
        descriptionDecodeQueue,
    };
    switch (i->itemPartial.type) {
        case GW::Constants::ItemType::Headpiece:
        case GW::Constants::ItemType::Boots:
        case GW::Constants::ItemType::Chestpiece:
        case GW::Constants::ItemType::Gloves:
        case GW::Constants::ItemType::Leggings:
            // ShorthandItemDescription includes item name for these
            break;
        default:
            // Default to single_item_name so merge_stacks can combine stacks of single and multiple items.
            if (i->itemPartial.single_item_name) {
                sync->q->description += i->itemPartial.single_item_name;
            } else if (i->itemPartial.complete_name_enc) {
                sync->q->description += i->itemPartial.complete_name_enc;
            } else if (i->itemPartial.name_enc) {
                sync->q->description += i->itemPartial.name_enc;
            }
    }
    if (i->itemPartial.info_string) {
        if (!sync->q->description.empty()) {
            sync->q->description += L"\x2\x102\x2";
        }
        sync->q->description += ToolboxUtils::ShorthandItemDescription(&(i->itemPartial));
    }
    GW::GameThread::Enqueue([sync] {
        GW::UI::AsyncDecodeStr(sync->q->description.c_str(), [](void* param, const wchar_t* s) {
            auto sync = (SyncDecode *) param;
            sync->q->description = TextUtils::StripTags(s);
            {
                std::lock_guard<std::mutex> guard(sync->lock);
                sync->queue.push(sync->q);
            }
            delete sync;
        }, sync, (GW::Constants::Language)0xff);
    });
}

void AccountInventoryWindow::OnItemTooltip(std::shared_ptr<MergeStack> ms)
{
    std::wstring currentAccount = GetAccountEmail();
    std::wstring prevCharacter{};
    std::wstring prev_account_representing_character{};
    std::string prevLocation{};
    for (auto it = ms->i.begin(); it != ms->i.end(); it++) {
        int styleCount = 0;
        if ((*it)->account != currentAccount) {
            styleCount = 1;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        std::wstring account_representing_character;
        if (auto fs = freeSlots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{(*it)->account, (*it)->character})); fs != freeSlots.end()) {
            account_representing_character = (*fs)->account_representing_character;
        }
        bool reprint = (*it)->character != prevCharacter || account_representing_character != prev_account_representing_character;
        if (reprint) {
            std::string suffix = "";
            if ((*it)->account != currentAccount && (*it)->character == L"(Chest)" && !account_representing_character.empty()) {
                suffix = " [" + TextUtils::WStringToString(account_representing_character) + "]";
            }
            ImGui::Text("%s%s", TextUtils::WStringToString((*it)->character).c_str(), suffix.c_str());
        }
        reprint |= (*it)->location != prevLocation;
        if (reprint) {
            ImGui::Text("- %s", (*it)->location.c_str());
        }
        ImGui::PopStyleColor(styleCount);
        prev_account_representing_character = account_representing_character;
        prevCharacter = (*it)->character;
        prevLocation = (*it)->location;
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(440.f * ImGui::GetIO().FontGlobalScale);
    std::string quantityStr = "";
    if (ms->quantity > 1) {
        quantityStr = std::to_string(ms->quantity) + " ";
    }
    ImGui::Text("%s%s", quantityStr.c_str(), TextUtils::WStringToString(ms->description).c_str());
    ImGui::PopTextWrapPos();
}

void AccountInventoryWindow::SortSlots(ImGuiTableSortSpecs* sortSpecs)
{
    std::wstring currentAccount = GetAccountEmail();
    freeSlotsSorted = std::set<std::shared_ptr<CharacterFreeSlots>, SlotCompare>(SlotCompare{sortSpecs});
    for (auto & freeSlot: freeSlots) {
        if (hide_other_accounts && freeSlot->account != currentAccount) {
            continue;
        }
        freeSlotsSorted.insert(freeSlot);
    }
}

void AccountInventoryWindow::SortInventory(ImGuiTableSortSpecs* sortSpecs)
{
    std::wstring currentAccount = GetAccountEmail();
    std::string MercHeroNames[8] = {};
    const auto ctx = GW::GetGameContext();
    auto& hero_array = ctx->world->hero_info;
    for (const auto& hero : hero_array) {
        if ((uint32_t)GW::Constants::HeroID::Merc1 <= hero.hero_id && hero.hero_id <= (uint32_t)GW::Constants::HeroID::Merc8) {
            MercHeroNames[hero.hero_id - (uint32_t)GW::Constants::HeroID::Merc1] = TextUtils::WStringToString(hero.name);
        }
    }
    inventorySorted.clear();
    std::unordered_map<std::wstring, std::shared_ptr<MergeStack>> mergedStacks{};
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        auto i = *it;
        if (i->description.empty()) {
            QueueDescriptionDecode(i);
        }
        if ((uint32_t)GW::Constants::HeroID::Merc1 <= i->hero_id && i->hero_id <= (uint32_t)GW::Constants::HeroID::Merc8) {
            i->location = MercHeroNames[i->hero_id - (uint32_t)GW::Constants::HeroID::Merc1];
        }
        if (hide_other_accounts && i->account != currentAccount) {
            continue;
        }
        if (hide_equipment && (i->bag_id == (uint32_t)GW::Constants::Bag::Equipped_Items || i->itemPartial.equipped)) {
            continue;
        }
        if (hide_equipment_pack && i->bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
            continue;
        }
        if (hide_hero_armor && IsHeroArmor(i->hero_id, i->slot)) {
            continue;
        }
        if (hide_unclaimed_items && i->bag_id == (uint32_t)GW::Constants::Bag::Unclaimed_Items) {
            continue;
        }
        // some hero armor like Jin and Sousukes 'Zaishen Gloves' would be stacked together without model_id
        auto merge_id = std::to_wstring(i->itemPartial.model_id) + i->description;
        std::shared_ptr<MergeStack> ms;
        if (merge_stacks && mergedStacks.contains(merge_id)) {
            ms = mergedStacks[merge_id];
        } else {
            ms = std::make_shared<MergeStack>(currentAccount);
            inventorySorted.push_back(ms);
            if (i->description.empty()) {
                // do not merge stacks if their description still waits in descriptionDecodeQueue
                ms->description = L"loading...";
            } else {
                mergedStacks[merge_id] = ms;
                ms->description = i->description;
            }
        }
        ms->quantity += i->itemPartial.quantity;
        ms->i.insert(i);
    }
    if (inventorySorted.size() > 1) {
        std::sort(inventorySorted.begin(), inventorySorted.end(), ItemCompare{sortSpecs, currentAccount});
    }
    if (sortSpecs) sortSpecs->SpecsDirty = false;
    needsSorting = false;
}

void AccountInventoryWindow::Draw(IDirect3DDevice9*)
{
    auto fontScale = ImGui::GetIO().FontGlobalScale;
    if (RerollStage::None < rerollStage && rerollStage < RerollStage::RerollToItem) {
        ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.f * fontScale, 80.f * fontScale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Account Inventory loading in progress", GetVisiblePtr(), GetWinFlags())) {
            ImGui::TextWrapped("Please do not interrupt inventory loading.");
            if (ImGui::Button("Abort!")) {
                rerollStage = RerollStage::None;
            }
        }
        ImGui::End();
    }
    if (visible) {
        ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(760.f * fontScale, 400.f * fontScale), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags()) || ImGui::IsWindowCollapsed()) {
            ImGui::End();
            return;
        }
        std::wstring currentAccount = GetAccountEmail();
        auto style = ImGui::GetStyle();
        const float itemSpacing = style.ItemInnerSpacing.x;
        float checkboxMaxWidth = 160.f * fontScale;
        ImVec4 colorChestItem        = HSVRotate(style.Colors[ImGuiCol_Button], 0.333f);
        ImVec4 colorChestItemHovered = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.333f);
        ImVec4 colorChestItemActive  = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.333f);
        ImVec4 colorHeroItem         = HSVRotate(style.Colors[ImGuiCol_Button], 0.166f);
        ImVec4 colorHeroItemHovered  = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.166f);
        ImVec4 colorHeroItemActive   = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.166f);

        // view related settings
        ImGui::Checkbox("Grid View", &grid_view);
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Merge Stacks", &merge_stacks)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Hide other Accounts", &hide_other_accounts)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Equipment", &hide_equipment)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Equipment Packs", &hide_equipment_pack)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Hero Armor", &hide_hero_armor)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkboxMaxWidth) ImGui::NewLine();
        if (ImGui::Checkbox("Hide unclaimed Items", &hide_unclaimed_items)) needsSorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < 110.f * fontScale) ImGui::NewLine();
        if (ImGui::Button("Load Inventories")) {
            ImGui::ConfirmDialog("In order to load all available items, this will cycle\nthrough all characters and all heroes.\nThis will take a few minutes if you have many characters.\nAre you sure?", [](bool result, void*){if (result) AccountInventoryWindow::Instance().LoadAllInventories();});
        }

        if (ImGui::CollapsingHeader("Free Slots")) {
            if (!ImGui::BeginTable("###freeslots", SlotColumnID_Max, ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::End();
                return;
            }
            ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, 0.f, SlotColumnID_Character);
            ImGui::TableSetupColumn("Inventory", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.f, SlotColumnID_Inventory);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 0.f, SlotColumnID_InventorySize);
            ImGui::TableSetupColumn("Equipment", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 0.f, SlotColumnID_Equipment);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 0.f, SlotColumnID_EquipmentSize);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGuiTableSortSpecs* slotSortSpecs = ImGui::TableGetSortSpecs();
            if (slotSortSpecs && slotSortSpecs->SpecsDirty) {
                SortSlots(slotSortSpecs);
            }
            for (auto & freeSlot: freeSlotsSorted) {
                auto free_equipment = freeSlot->max_equipment - freeSlot->occupied_equipment;
                auto free_inventory = freeSlot->max_inventory - freeSlot->occupied_inventory;
                bool isChest = freeSlot->character == L"(Chest)";
                std::string suffix = "";
                int styleCount = 0;
                if (freeSlot->account != currentAccount) {
                    styleCount = 1;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (isChest && !freeSlot->account_representing_character.empty()) {
                        suffix = " [" + TextUtils::WStringToString(freeSlot->account_representing_character) + "]";
                    }
                }
                ImGui::TableNextColumn();
                if (freeSlot->account == currentAccount) {
                    if (ImGui::Button(TextUtils::WStringToString(freeSlot->character).c_str())) {
                        // reroll to target or open chest
                        OnInventoryItemClicked(std::make_shared<InventoryItem>(InventoryItem{freeSlot->account, freeSlot->character, 0, isChest ? (uint32_t)GW::Constants::Bag::Storage_1 : 0, 0}), false);
                    }
                } else {
                    ImGui::Text("%s%s", TextUtils::WStringToString(freeSlot->character).c_str(), suffix.c_str());
                }

                ImGui::TableNextColumn();
                if (freeSlot->max_inventory) RightAlignText(std::to_string(free_inventory)+"/");

                ImGui::TableNextColumn();
                if (freeSlot->max_inventory) ImGui::Text("%d", freeSlot->max_inventory);

                ImGui::TableNextColumn();
                if (freeSlot->max_equipment) RightAlignText(std::to_string(free_equipment)+"/");

                ImGui::TableNextColumn();
                if (freeSlot->max_equipment) ImGui::Text("%d", freeSlot->max_equipment);
                ImGui::PopStyleColor(styleCount);
            }
            ImGui::EndTable();
        }

        float itemstableHeight = std::max(ImGui::GetContentRegionAvail().y, itemsTableMinHeight);
        float innerWidth = ImGui::GetContentRegionAvail().x - itemSpacing;
        // sorting/filtering header
        ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody;
        if (grid_view) {
            itemstableHeight = 2 * ImGui::GetFrameHeight();
            flags |= ImGuiTableFlags_SizingFixedFit;
        } else {
            flags |= ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg;
        }
        if (!ImGui::BeginTable("###itemstable", ItemColumnID_Max, flags, ImVec2(innerWidth, itemstableHeight))) {
            ImGui::End();
            return;
        }
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Character);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Location);
        ImGui::TableSetupColumn("Model ID", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_ModelID);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Description);
        ImGui::TableSetupScrollFreeze(2, 2);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###nameFilter", nameFilterBuf, BUFFER_SIZE);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###locationFilter", locationFilterBuf, BUFFER_SIZE);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###modelIDFilter", modelIDFilterBuf, BUFFER_SIZE);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(300.f * fontScale);
        ImGui::InputText("###itemFilter", itemFilterBuf, BUFFER_SIZE);
        ImGui::SameLine();
        ImGui::Text("Filter   %d/%d Items", filteredItemCount, inventorySorted.size());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGuiTableSortSpecs* itemSortSpecs = ImGui::TableGetSortSpecs();
        if (needsSorting || (itemSortSpecs && itemSortSpecs->SpecsDirty)) {
            SortInventory(itemSortSpecs);
        }


        if (grid_view) {
            // detail view follows the structure of the sorting header
            // grid view needs its own table
            ImGui::EndTable();
            if (!ImGui::BeginTable("###itemgrid", std::max(1, (int)(innerWidth / (3.3f*ImGui::GetTextLineHeight() + itemSpacing))), ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY, ImVec2(innerWidth, std::max(ImGui::GetContentRegionAvail().y, itemsTableMinHeight)))) {
                ImGui::End();
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
        }

        auto nameFilter = std::string(nameFilterBuf);
        auto locationFilter = std::string(locationFilterBuf);
        auto modelIDFilter = std::string(modelIDFilterBuf);
        auto itemFilter = std::string(itemFilterBuf);
        bool nameIsLower = std::all_of(nameFilter.begin(), nameFilter.end(), [](unsigned char c){ return !std::isupper(c); });
        bool locationIsLower = std::all_of(locationFilter.begin(), locationFilter.end(), [](unsigned char c){ return !std::isupper(c); });
        bool itemIsLower = std::all_of(itemFilter.begin(), itemFilter.end(), [](unsigned char c){ return !std::isupper(c); });
        filteredItemCount = 0;
        for (auto & ims: inventorySorted) {
            auto iFront = *(ims->i.begin());
            std::wstring description = ims->description;
            if (ims->quantity > 1) {
                description = std::to_wstring(ims->quantity) + L" " + description;
            }
            auto descriptionOneLine = TextUtils::ctre_regex_replace<L"\n", L" - ">(description);
            uint16_t quantity = ims->quantity;

            // filter item
            bool filterMatch = false;
            for (auto it = ims->i.begin(); it != ims->i.end(); it++) {
                std::wstring characterCheck = (*it)->character;
                std::string locationCheck = (*it)->location;
                if (nameIsLower) {
                    characterCheck = TextUtils::ToLower(characterCheck);
                }
                if (locationIsLower) {
                    locationCheck = TextUtils::ToLower(locationCheck);
                }
                // any pair of character/location must match, not all
                if (characterCheck.contains(TextUtils::StringToWString(nameFilter)) &&
                    locationCheck.contains(locationFilter)) {
                    filterMatch = true;
                    break;
                }
            }
            auto descriptionCheck = descriptionOneLine;
            if (itemIsLower) {
                descriptionCheck = TextUtils::ToLower(descriptionCheck);
            }
            filterMatch &= descriptionCheck.contains(TextUtils::StringToWString(itemFilter));
            filterMatch &= modelIDFilter.empty() || modelIDFilter == std::to_string(iFront->itemPartial.model_id);

            if (!filterMatch) continue;
            filteredItemCount++;

            // render item
            bool clicked = false;
            ImGui::PushID(filteredItemCount);
            int styleCount = 0;
            if (IsChestBag(iFront->bag_id)) {
                styleCount += 3;
                ImGui::PushStyleColor(ImGuiCol_Button, colorChestItem);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorChestItemHovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorChestItemActive);
            } else if (IsOnHero(iFront->hero_id)) {
                styleCount += 3;
                ImGui::PushStyleColor(ImGuiCol_Button, colorHeroItem);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorHeroItemHovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorHeroItemActive);
            }
            if (iFront->account != currentAccount) {
                styleCount += 1;
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (grid_view) {
                const ImVec2 pos = ImGui::GetCursorPos();
                auto w = 3.3f*ImGui::GetTextLineHeight();
                if (iFront->texture && *(iFront->texture)) {
                    clicked = ImGui::IconButton("", *iFront->texture, ImVec2(w, w), ImGuiButtonFlags_None, ImVec2(w, w));
                } else {
                    clicked = ImGui::Button("???", ImVec2(w, w));
                }
                if (quantity > 1) {
                    ImGui::SetCursorPos(ImVec2(pos.x + itemSpacing, pos.y));
                    ImGui::TextColored(ImVec4(0.98f, 0.97f, 0.6f, 1.f), "%d", quantity);
                    ImGui::SetCursorPos(pos);
                    ImGui::Dummy(ImVec2(w, w));
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip([ims]() { AccountInventoryWindow::Instance().OnItemTooltip(ims); });
                }
                ImGui::TableNextColumn();
            } else { // detail view
                std::string suffix = (ims->i.size() > 1) ? " +" : "";
                ImGui::Text("%s%s", TextUtils::WStringToString(iFront->character).c_str(), suffix.c_str());
                ImGui::TableNextColumn();

                ImGui::Text("%s%s", iFront->location.c_str(), suffix.c_str());
                ImGui::TableNextColumn();

                ImGui::Text("%d", iFront->itemPartial.model_id);
                ImGui::TableNextColumn();

                style.ButtonTextAlign = ImVec2(0.f, 0.5f);
                clicked = ImGui::Button(TextUtils::WStringToString(descriptionOneLine).c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip([ims]() { AccountInventoryWindow::Instance().OnItemTooltip(ims); });
                }
                ImGui::TableNextColumn();
            }
            ImGui::PopStyleColor(styleCount);
            ImGui::PopID();

            if (clicked) {
                // reroll to target or open chest
                OnInventoryItemClicked(iFront, ImGui::IsKeyDown(ImGuiMod_Ctrl));
            }
        }
        ImGui::EndTable();
        ImGui::End();
    }
}

void AccountInventoryWindow::DrawSettingsInternal()
{
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::Text("Account Inventory shows a combined view of all player, hero and storage inventories.");
    if (ImGui::Button("Load Inventories")) {
        visible = true;
        ImGui::ConfirmDialog("In order to load all available items, this will cycle\nthrough all characters and all heroes.\nThis will take a few minutes if you have many characters.\nAre you sure?", [](bool result, void*){if (result) AccountInventoryWindow::Instance().LoadAllInventories();});
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Inventories")) {
        inventory.clear();
        inventoryLookup.clear();
        inventorySorted.clear();
        freeSlots.clear();
        for (auto it = iniByCharacter.begin(); it != iniByCharacter.end(); ++it) {
            inventoryDirty.insert(it->first);
        }
        SaveToFiles();
    }
    ImGui::Text("Track Inventory - Disabling deletes all previously gathered inventory data for this account.");
    ImGui::Checkbox("###account_inventory_grid_view", &grid_view);
    ImGui::SameLine();
    ImGui::Text("Grid View - Toggle between detailed list and icon grid view.");
    ImGui::Checkbox("###account_inventory_merge_stacks", &merge_stacks);
    ImGui::SameLine();
    ImGui::Text("Merge Stacks - Combine multiple of the same item, including non-stackable items.");
    ImGui::Checkbox("###account_inventory_hide_other_accounts", &hide_other_accounts);
    ImGui::SameLine();
    ImGui::Text("Hide other Accounts - Hide item which do not belong to the currently active account.");
    ImGui::Checkbox("###account_inventory_hide_equipment", &hide_equipment);
    ImGui::SameLine();
    ImGui::Text("Hide Equipment - Hide items currently equipped or part of a weapon set.");
    ImGui::Checkbox("###account_inventory_hide_equipment_pack", &hide_equipment_pack);
    ImGui::SameLine();
    ImGui::Text("Hide Equipment Packs - Hide contents of equipment packs.");
    ImGui::Checkbox("###account_inventory_hide_hero_armor", &hide_hero_armor);
    ImGui::SameLine();
    ImGui::Text("Hide Hero Armor - Hide armor worn by heroes.");
    ImGui::Checkbox("###account_inventory_hide_unclaimed_items", &hide_unclaimed_items);
    ImGui::SameLine();
    ImGui::Text("Hide unclaimed Items - Hide items from the unclaimed items window.");
    ImGui::PopTextWrapPos();
}

void AccountInventoryWindow::LoadSettings(ToolboxIni* ini)
{
    ToolboxWindow::LoadSettings(ini);
    LOAD_BOOL(grid_view);
    LOAD_BOOL(merge_stacks);
    LOAD_BOOL(hide_other_accounts);
    LOAD_BOOL(hide_equipment);
    LOAD_BOOL(hide_equipment_pack);
    LOAD_BOOL(hide_hero_armor);
    LOAD_BOOL(hide_unclaimed_items);
    needsSorting = true;
    // only LoadFromFiles foreign items here. allowing the user to reload inventory data of the active account, may cause temporary inconsistencies
    LoadFromFiles(true);
}

void AccountInventoryWindow::SaveSettings(ToolboxIni* ini)
{
    SAVE_BOOL(hide_unclaimed_items);
    SAVE_BOOL(hide_hero_armor);
    SAVE_BOOL(hide_equipment_pack);
    SAVE_BOOL(hide_equipment);
    SAVE_BOOL(hide_other_accounts);
    SAVE_BOOL(merge_stacks);
    SAVE_BOOL(grid_view);
    ToolboxWindow::SaveSettings(ini);
    SaveToFiles();
}

// unique section name for item in ini file
std::string AccountInventoryWindow::KeyToString(std::shared_ptr<InventoryItem> i) const
{
    char buf[9];
    std::string out;
    snprintf(buf, sizeof(buf), "%08x", i->hero_id);
    out.append(buf);
    snprintf(buf, sizeof(buf), "%08x", i->bag_id);
    out.append(buf);
    snprintf(buf, sizeof(buf), "%08x", i->slot);
    out.append(buf);
    return out;
}

std::shared_ptr<AccountInventoryWindow::InventoryIni> AccountInventoryWindow::getIni(std::wstring iniID, std::wstring account)
{
    if (!iniByCharacter.contains(iniID)) {
        wchar_t path[MAX_PATH];
        if (0 == GetTempFileNameW(Resources::GetPath(L"inventories").wstring().c_str(), L"inv", 0, path)) {
            Log::Error("Account Inventory: Failed to create inventory ini. Inventory tracking data will be lost.");
            return nullptr;
        }
        auto ini = std::make_shared<InventoryIni>(path);
        ini->iniID = iniID;
        ini->account = account;
        iniByCharacter[iniID] = ini;
        iniByPath[path] = iniByCharacter[iniID];
    }
    return iniByCharacter[iniID];
}

void AccountInventoryWindow::LoadFromFiles(bool onlyForeign)
{
    ToolboxIni::TNamesDepend entries{};
    std::wstring currentAccount = GetAccountEmail();

    Resources::EnsureFolderExists(Resources::GetPath(L"inventories"));
    std::unordered_set<std::filesystem::path> visited;
    std::unordered_set<std::shared_ptr<InventoryItem>, ItemHash, ItemEqual> inventoryRebuild{};
    std::unordered_set<std::shared_ptr<CharacterFreeSlots>, SlotHash, SlotEqual> freeSlotsRebuild{};
    for (auto const &file: std::filesystem::directory_iterator{Resources::GetPath(L"inventories")}) {
        auto path = file.path();
        visited.insert(path);
        if (!iniByPath.contains(path)) {
            iniByPath[path] = std::make_shared<InventoryIni>(path);
        }
        auto ini = iniByPath[path];
        if (onlyForeign && ini->account == currentAccount) continue;
        if (checkIniDirty(ini)) {
            ini->Reset();
            if (ini->LoadFile(path.wstring()) < 0) continue;
        }
        ini->GetAllSections(entries);
        for (const ToolboxIni::Entry& entry : entries) {
            const char* section = entry.pItem;

            // account and character values must exist in both freeslot and item sections
            auto account = TextUtils::Base64DecodeW(ini->GetValue(section, "account", ""));
            auto character = TextUtils::Base64DecodeW(ini->GetValue(section, "character", ""));
            if (ini->iniID.empty()) {
                ini->iniID = getIniID(account, character);
                ini->account = account;
                iniByCharacter[ini->iniID] = ini;
            } else if (ini->iniID != getIniID(account, character)) {
                continue;
            }
            if (onlyForeign && account == currentAccount) continue;
            
            if (std::string_view(section) == "freeslots") {
                auto freeSlot = std::make_shared<CharacterFreeSlots>();
                freeSlot->account = account;
                freeSlot->character = character;
                freeSlot->account_representing_character = TextUtils::Base64DecodeW(ini->GetValue(section, "account_character", ""));
                freeSlot->max_equipment = (int)(ini->GetLongValue(section, "maxequipment", 0));
                freeSlot->max_inventory = (int)(ini->GetLongValue(section, "maxinventory", 0));
                freeSlot->occupied_equipment = (int)(ini->GetLongValue(section, "occupiedequipment", 0));
                freeSlot->occupied_inventory = (int)(ini->GetLongValue(section, "occupiedinventory", 0));
                freeSlot->anniversary_pane_active = ini->GetBoolValue(section, "anniversary_pane_active", false);
                freeSlotsRebuild.insert(freeSlot);
                continue;
            }

            auto i = std::make_shared<InventoryItem>();
            i->account = account;
            i->character = character;
            i->bag_id = (uint32_t)(ini->GetLongValue(section, "bagid", 1));
            i->hero_id = (uint32_t)(ini->GetLongValue(section, "heroid", 0));
            i->slot = (uint32_t)(ini->GetLongValue(section, "slot", 0));

            const char * info_string_b64 = ini->GetValue(section, "info", nullptr);
            if (info_string_b64) {
                i->info_string = TextUtils::Base64DecodeW(info_string_b64);
                i->itemPartial.info_string = i->info_string.data();
            }
            const char * single_item_name_b64 = ini->GetValue(section, "singleitemname", nullptr);
            if (single_item_name_b64) {
                i->single_item_name = TextUtils::Base64DecodeW(single_item_name_b64);
                i->itemPartial.single_item_name = i->single_item_name.data();
            }
            const char * complete_name_enc_b64 = ini->GetValue(section, "completename", nullptr);
            if (complete_name_enc_b64) {
                i->complete_name_enc = TextUtils::Base64DecodeW(complete_name_enc_b64);
                i->itemPartial.complete_name_enc = i->complete_name_enc.data();
            }
            const char * name_enc_b64 = ini->GetValue(section, "name", nullptr);
            if (name_enc_b64) {
                i->name_enc = TextUtils::Base64DecodeW(name_enc_b64);
                i->itemPartial.name_enc = i->name_enc.data();
            }
            const char * customized_b64 = ini->GetValue(section, "customized", nullptr);
            if (customized_b64) {
                i->customized = TextUtils::Base64DecodeW(customized_b64);
                i->itemPartial.customized = i->customized.data();
            }
            i->itemPartial.type = (GW::Constants::ItemType)(ini->GetLongValue(section, "type", 0));
            i->itemPartial.model_id = (uint32_t)(ini->GetLongValue(section, "modelid", 0));
            i->itemPartial.model_file_id = (uint32_t)(ini->GetLongValue(section, "modelfileid", 0));
            i->itemPartial.interaction = (uint32_t)(ini->GetLongValue(section, "interaction", 0));
            i->itemPartial.quantity = (uint16_t)(ini->GetLongValue(section, "quantity", 0));
            i->itemPartial.equipped = (uint8_t)(ini->GetLongValue(section, "equipped", 0));
            i->itemPartial.item_id = 0;
            i->texture = Resources::GetItemImage(&(i->itemPartial));
            i->location = HeroName[i->hero_id];
            if (IsChestBag(i->bag_id)) {
                i->location = BagName[(int)(i->bag_id)];
            }

            inventoryRebuild.insert(i);
        }
    }
    if (onlyForeign) {
        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            if ((*it)->account == currentAccount) inventoryRebuild.insert(*it);
        }
        for (auto it = freeSlots.begin(); it != freeSlots.end(); ++it) {
            if ((*it)->account == currentAccount) freeSlotsRebuild.insert(*it);
        }
    }
    for (auto it = iniByPath.begin(); it != iniByPath.end(); ++it) {
        if (!visited.contains(it->first)) it->second->Reset();
    }
    freeSlots = freeSlotsRebuild;
    inventory = inventoryRebuild;
    needsSorting = true;
}

void AccountInventoryWindow::SaveToFiles()
{
    // to avoid overwriting data from other accounts, reload their entries before saving
    LoadFromFiles(true);

    std::wstring currentAccount = GetAccountEmail();
    std::unordered_set<std::wstring> visited;

    for (auto & freeSlot: freeSlots) {
        auto iniID = getIniID(freeSlot->account, freeSlot->character);
        if (!inventoryDirty.contains(iniID)) continue;
        if (currentAccount.empty() || freeSlot->account != currentAccount) continue; // skip foreign items, only update what belongs to us
        auto ini = getIni(iniID, freeSlot->account);
        if (!ini) return;
        if (!visited.contains(iniID)) {
            ini->Reset();
            visited.insert(iniID);
        }
        auto section = "freeslots";
        if (!freeSlot->account.empty()) {
            ini->SetValue(section, "account", TextUtils::Base64EncodeW(freeSlot->account).c_str());
        }
        if (!freeSlot->character.empty()) {
            ini->SetValue(section, "character", TextUtils::Base64EncodeW(freeSlot->character).c_str());
        }
        if (!freeSlot->account_representing_character.empty()) {
            ini->SetValue(section, "account_character", TextUtils::Base64EncodeW(freeSlot->account_representing_character).c_str());
        }
        ini->SetLongValue(section, "maxequipment", freeSlot->max_equipment);
        ini->SetLongValue(section, "maxinventory", freeSlot->max_inventory);
        ini->SetLongValue(section, "occupiedequipment", freeSlot->occupied_equipment);
        ini->SetLongValue(section, "occupiedinventory", freeSlot->occupied_inventory);
        if (freeSlot->anniversary_pane_active) {
            ini->SetBoolValue(section, "anniversary_pane_active", freeSlot->anniversary_pane_active);
        }
    }

    for (auto & i: inventory) {
        auto iniID = getIniID(i->account, i->character);
        if (!inventoryDirty.contains(iniID)) continue;
        if (currentAccount.empty() || i->account != currentAccount) continue; // skip foreign items, only update what belongs to us
        auto ini = getIni(iniID, i->account);
        if (!ini) return;
        if (!visited.contains(iniID)) {
            ini->Reset();
            visited.insert(iniID);
        }
        auto section = KeyToString(i);
        ini->SetValue(section.c_str(), "account", TextUtils::Base64EncodeW(i->account).c_str());
        ini->SetValue(section.c_str(), "character", TextUtils::Base64EncodeW(i->character).c_str());
        ini->SetLongValue(section.c_str(), "heroid", (long)i->hero_id);
        ini->SetLongValue(section.c_str(), "bagid", (long)i->bag_id);
        ini->SetLongValue(section.c_str(), "slot", (long)i->slot);
        if (i->itemPartial.info_string) {
            ini->SetValue(section.c_str(), "info", TextUtils::Base64EncodeW(i->itemPartial.info_string).c_str());
        }
        if (i->itemPartial.single_item_name) {
            ini->SetValue(section.c_str(), "singleitemname", TextUtils::Base64EncodeW(i->itemPartial.single_item_name).c_str());
        }
        if (i->itemPartial.complete_name_enc) {
            ini->SetValue(section.c_str(), "completename", TextUtils::Base64EncodeW(i->itemPartial.complete_name_enc).c_str());
        }
        if (i->itemPartial.name_enc) {
            ini->SetValue(section.c_str(), "name", TextUtils::Base64EncodeW(i->itemPartial.name_enc).c_str());
        }
        if (i->itemPartial.customized) {
            ini->SetValue(section.c_str(), "customized", TextUtils::Base64EncodeW(i->itemPartial.customized).c_str());
        }
        ini->SetLongValue(section.c_str(), "type", (long)i->itemPartial.type);
        ini->SetLongValue(section.c_str(), "modelid", (long)i->itemPartial.model_id);
        ini->SetLongValue(section.c_str(), "modelfileid", (long)i->itemPartial.model_file_id);
        ini->SetLongValue(section.c_str(), "interaction", (long)i->itemPartial.interaction);
        ini->SetLongValue(section.c_str(), "quantity", (long)i->itemPartial.quantity);
        ini->SetLongValue(section.c_str(), "equipped", (long)i->itemPartial.equipped);
    }
    for (auto it = iniByCharacter.begin(); it != iniByCharacter.end(); ++it) {
        auto iniID = it->first;
        if (it->second->account != currentAccount) continue;
        if (visited.contains(iniID)) {
            if (it->second->SaveFile(it->second->location_on_disk.wstring().c_str()) != SI_OK) {
                Log::Error("Account Inventory: Failed to save inventory ini. Inventory tracking data will be lost.");
            }
        } else if (inventoryDirty.contains(iniID)) {
            // dirty but not visited means there are no more items in this inventory. clean up its ini
            it->second->Reset();
        }
    }
    // separate loop because deleting from iniByCharacter in the above loop gets really unreadable
    for (auto it = iniByPath.begin(); it != iniByPath.end();) {
        if (it->second->IsEmpty()) {
            DeleteFileW(it->first.wstring().c_str());
            iniByCharacter.erase(it->second->iniID);
            it = iniByPath.erase(it);
        } else ++it;
    }
    inventoryDirty.clear();
}

void AccountInventoryWindow::AddItem(uint32_t item_id)
{
    auto item = GW::Items::GetItemById(item_id);
    if (!(item && item->bag)) return;

    // gather information for this items storage location, i.e.:
    // account, player character, hero, bag, slot within bag
    std::wstring currentAccount = GetAccountEmail();
    auto i = std::make_shared<InventoryItem>();
    i->account = currentAccount;
    i->bag_id = (uint32_t)item->bag->bag_id();
    if (IsChestBag(i->bag_id)) {
        i->character = L"(Chest)";
    } else {
        i->character = GW::AccountMgr::GetCurrentPlayerName();
        if (i->character.empty()) {
            i->character = L"Unavailable";
        }
    }
    i->slot = item->slot;

    // this is a workaround because I could not find a way to get a hero_id from an item currently equipped on a hero
    // item->bag->bag_array is a separate array for each hero with only the Equipped_Items bag set, but seemingly no reference back to the hero
    // the workaround uses the fact that heroes only have a very limited and uniquely identifying set of armor pieces
    // ChestModelIDToHeroID maps model_id of all (currently available) hero armor chest pieces to the respective hero_ids
    i->hero_id = GW::Constants::HeroID::NoHero;
    if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipped_Items && (GW::Inventory *)item->bag->bag_array != GW::Items::GetInventory()) {
        // this is part of a heroes equipment. need hero armor piece to identify which one it is
        uint32_t chestModelID;
        ASSERT(item->bag->items.size() >= 5);
        if (item->bag->items[ChestArmorInventorySlot]) {
            // chest is present already, set hero_id and continue
            chestModelID = item->bag->items[ChestArmorInventorySlot]->model_id;
        } else {
            // chest Item not created yet. Skip, since we can not know which hero this belongs to
            return;
        }
        // AllHeroes encodes that we do not know this hero. That only happens if ANet adds a new hero or new hero armor.
        i->hero_id = ChestModelIDToHeroID.contains(chestModelID) ? (ChestModelIDToHeroID[chestModelID]) : GW::Constants::HeroID::AllHeroes;
        if (item->slot == ChestArmorInventorySlot) {
            // this is a chest, add anything from this heroes equipment that might have been skipped before
            for (auto oItem: item->bag->items) {
                if (!oItem || oItem->slot == ChestArmorInventorySlot) continue;
                AddItem(oItem->item_id);
            }
        }
        if (item->slot == 6) {
            // last item to be added when a hero loads in. remove any equipment still missing, in case inventory was modified without us tracking it
            uint32_t slot = 0;
            for (auto oItem: item->bag->items) {
                if (!oItem) {
                    ClearMissingItem(currentAccount, i->character, i->hero_id, i->bag_id, slot);
                }
                slot++;
            }
        }
    }
    // END hero_id workaround

    // gather members of item referenced in ShorthandItemDescription, GetItemImage or used by AccountInventoryWindow internally
    if (item->info_string) {
        i->info_string = item->info_string;
        i->itemPartial.info_string = i->info_string.data();
    }
    if (item->single_item_name) {
        i->single_item_name = item->single_item_name;
        i->itemPartial.single_item_name = i->single_item_name.data();
    }
    if (item->complete_name_enc) {
        i->complete_name_enc = item->complete_name_enc;
        i->itemPartial.complete_name_enc = i->complete_name_enc.data();
    }
    if (item->name_enc) {
        i->name_enc = item->name_enc;
        i->itemPartial.name_enc = i->name_enc.data();
    }
    if (item->customized) {
        i->customized = item->customized;
        i->itemPartial.customized = i->customized.data();
    }
    i->itemPartial.type = item->type;
    i->itemPartial.model_id = item->model_id;
    i->itemPartial.model_file_id = item->model_file_id;
    i->itemPartial.interaction = item->interaction;
    i->itemPartial.quantity = item->quantity;
    i->itemPartial.equipped = item->equipped;
    i->itemPartial.item_id = item->item_id;
    i->texture = Resources::GetItemImage(&(i->itemPartial));
    i->location = HeroName[i->hero_id];
    if (IsChestBag(i->bag_id)) {
        i->location = BagName[(int)(i->bag_id)];
    }

    if (auto it = freeSlots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, i->character})); it != freeSlots.end()) {
        if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
            (*it)->occupied_equipment++;
        } else if (BagCanHoldAnything[i->bag_id]) {
            (*it)->occupied_inventory++;
        }
    }
    if (auto it = inventory.find(i); it != inventory.end()) {
        // found
        auto oitem_id = (*it)->itemPartial.item_id;
        // make sure the lookup entry has not already been overwritten by another item being loaded during map load.
        if (inventoryLookup.contains(oitem_id) && inventoryLookup[oitem_id] == (*it)) {
            inventoryLookup.erase(oitem_id);
        }
    }
    // .insert does not replace the shared_ptr if it is deemed equal by ItemEqual. Erase it first
    inventory.erase(i);
    inventory.insert(i);
    inventoryLookup[item->item_id] = i;
    needsSorting = true;
    inventoryDirty.insert(getIniID(i->account, i->character));

    if (itemToMove && (uint32_t)GW::Constants::HeroID::NoHero != i->hero_id && ItemEqual{}(itemToMove, i)) {
        // If we had to change characters and itemToMove is on a player character,
        // then we get here during loading before MoveItem can work.
        // In this case StepReroll will take care of moving the item.
        MoveItem();
    }
}

void AccountInventoryWindow::ClearMissingItem(std::wstring account, std::wstring character, uint32_t hero_id, uint32_t bag_id, uint32_t slot)
{
    auto i = std::make_shared<InventoryItem>();
    i->account = account;
    i->bag_id = bag_id;
    i->character = character;
    i->slot = slot;
    i->hero_id = hero_id;
    if (auto it = inventory.find(i); it != inventory.end()) {
        // found
        inventoryDirty.insert(getIniID((*it)->account, (*it)->character));
        RemoveItem((*it)->itemPartial.item_id);
        // Most likely the missing item was still in our inifile but removed ingame.
        // In this case we won't know an item_id, but still want to remove it from inventory
        inventory.erase(it);
    }
}

void AccountInventoryWindow::RemoveItem(uint32_t item_id)
{
    if (!inventoryLookup.contains(item_id)) return;

    std::wstring currentAccount = GetAccountEmail();
    auto i = inventoryLookup[item_id];
    if (auto it = freeSlots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{currentAccount, i->character})); it != freeSlots.end()) {
        if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
            (*it)->occupied_equipment--;
        } else if (BagCanHoldAnything[i->bag_id]) {
            (*it)->occupied_inventory--;
        }
    }
    inventory.erase(i);
    inventoryLookup.erase(item_id);
    needsSorting = true;
    inventoryDirty.insert(getIniID(i->account, i->character));
}
