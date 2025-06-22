#include "stdafx.h"
#include <fileapi.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Context/CharContext.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/ItemContext.h>
#include <GWCA/Context/WorldContext.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/ItemMgr.h>

#include <Logger.h>
#include <GWToolbox.h>
#include <Utils/TextUtils.h>

#include <Modules/Resources.h>
#include <Modules/InventoryManager.h>
#include <Windows/CompletionWindow.h>
#include <Windows/RerollWindow.h>
#include <Windows/AccountInventoryWindow.h>

static const float ITEMS_TABLE_MIN_HEIGHT = 220.f;
static const int CHEST_ARMOR_INVENTORY_SLOT = 2;
static const clock_t ADD_HERO_TIMEOUT = 500;
static const clock_t SAVE_HERO_TIMEOUT = 500;
static const clock_t MAP_LOADED_DELAYED_TIMEOUT = 400;

static const std::vector<std::string> HERO_NAME = {
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
    "Devona", "Ghost of Althea"
};

static const std::vector<std::string> BAG_NAME = {
    "", "Backpack", "Belt Pouch", "Bag 1", "Bag 2", "Equipment Pack",
    "Material Storage", "Unclaimed Items", "Storage 1", "Storage 2",
    "Storage 3", "Storage 4", "Storage 5", "Storage 6", "Storage 7",
    "Storage 8", "Storage 9", "Storage 10", "Storage 11", "Storage 12",
    "Storage 13", "Storage 14", "Equipped Items"
};

static std::vector<uint32_t> BAG_MAX_SIZE = {
    0, 20, 10, 15, 15, 20,
    36, 12, 25, 25,
    25, 25, 25, 25, 25,
    25, 25, 25, 25, 25,
    25, 25, 9
};

static std::vector<bool> BAG_CAN_HOLD_ANYTHING = {
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
    return (uint32_t)GW::Constants::HeroID::NoHero < hero_id && hero_id < (uint32_t)GW::Constants::HeroID::Count;
}

static bool GetIsMapReady()
{
    return GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading && GW::Map::GetIsMapLoaded() && GW::Agents::GetControlledCharacter();
}

static std::wstring GetAccountEmail()
{
    const auto c = GW::GetCharContext();
    return c ? c->player_email : L"";
}

static std::wstring GetIniID(std::wstring account, std::wstring character) {
    if (character == L"(Chest)") {
        return account;
    } else {
        return character;
    }
}

static void RightAlignText(std::string text)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x - ImGui::GetScrollX());
    ImGui::Text("%s", text.c_str());
}

static ImVec4 HSVRotate(ImVec4 color, float hue = -1.f, float sat_factor = 1.f)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    if (hue < 0.f) hue = h;
    return (ImVec4)ImColor::HSV(hue, s * sat_factor, v, color.w);
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
        GW::UI::UIMessage::kPartyRemoveHero,
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
                    case GW::UI::UIMessage::kPartyAddHero: {
                        const auto hero = ((struct GW::HeroPartyMember **)wparam)[1];
                        const GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
                        if (!me || !hero) break;
                        if (hero->owner_player_id != me->login_number) break;
                        uint32_t hero_id = ((uint32_t *)wparam)[7];
                        OnPartyAddHero(hero_id);
                        break;
                    }
                    case GW::UI::UIMessage::kPartyRemoveHero: {
                        uint32_t hero_id = ((uint32_t *)wparam)[3];
                        OnPartyRemoveHero(hero_id);
                        break;
                    }
                    case GW::UI::UIMessage::kMapChange:
                        PreMapLoad();
                        break;
                    case GW::UI::UIMessage::kMapLoaded:
                        PostMapLoad();
                        break;
                    case GW::UI::UIMessage::kLogout: {
                        // prepare for potentially changing accounts.
                        SaveToFiles(false);
                        LoadFromFiles(true);
                        show_delete_note = false;
                        // can not reset reroll_stage here, since reroll trigger kLogout.
                        // instead we check whether accounts changed during PreMapLoad.
                        break;
                    }
                }
            }
        );
    }
    initializing = true;
    LoadFromFiles(false);
    auto ic = GW::GetItemContext();
    if (ic) {
        // fake a map load to clear missing items and remove deleted characters.
        PreMapLoad();
        for (auto const &i: ic->item_array) {
            if (i) {
                AddItem(i->item_id);
            }
        }
        PostMapLoad();
    }
    initializing = false;
}

void AccountInventoryWindow::Terminate()
{
    GW::UI::RemoveUIMessageCallback(&OnUIMessage_HookEntry);
    inventory.clear();
    inventory_lookup.clear();
    inventory_sorted.clear();
    while (!hero_bag_generation_order.empty()) hero_bag_generation_order.pop();
    bag_ptr_to_hero_id.clear();
    {
        std::lock_guard<std::mutex> guard(description_decode_lock);
        while(!description_decode_queue.empty()) description_decode_queue.pop();
    }
    ini_by_path.clear();
    ini_by_character.clear();
    inventory_dirty.clear();
    free_slots.clear();
    free_slots_sorted.clear();
    reroll_hero_queue.clear();
    reroll_char_queue.clear();
    last_character = L"";
    last_available_chars.clear();
    reroll_stage = RerollStage::None;
    map_loaded_delayed_trigger = false;
    show_delete_note = false;
    ToolboxWindow::Terminate();
}

void AccountInventoryWindow::Update(float)
{
    {
        std::lock_guard<std::mutex> guard(description_decode_lock);
        while (!description_decode_queue.empty()) {
            auto q = description_decode_queue.front();
            q->i->description = q->description;
            description_decode_queue.pop();
            needs_sorting = true;
        }
    }
    if (map_loaded_delayed_trigger && TIMER_DIFF(map_loaded_delayed_timer) > MAP_LOADED_DELAYED_TIMEOUT) {
        OnMapLoadedDelayed();
    }
    // wait until after OnMapLoadedDelayed to continue rerolling
    if (!map_loaded_delayed_trigger && reroll_stage == RerollStage::DoSaveHeroes && TIMER_DIFF(save_hero_timer) > SAVE_HERO_TIMEOUT) {
        SaveHeroes();
        reroll_stage = RerollStage::DoneCharacterLoad;
        StepReroll();
    }
    if (reroll_stage == RerollStage::WaitForHeroLoad && TIMER_DIFF(add_hero_timer) > ADD_HERO_TIMEOUT) {
        // failed to load hero in time, continue rerolling
        // this happens if mercenary heroes are available but not set up or possibly with koss when he's gone
        reroll_stage = RerollStage::DoneHeroLoad;
        StepReroll();
    }
    if (reroll_stage == RerollStage::WaitForHeroWithItem && TIMER_DIFF(add_hero_timer) > ADD_HERO_TIMEOUT) {
        // failed to load hero in time, forget item_to_move
        item_to_move = nullptr;
        reroll_stage = RerollStage::None;
    }
}

void AccountInventoryWindow::PreMapLoad()
{
    std::wstring current_account = GetAccountEmail();
    std::wstring current_character = GW::AccountMgr::GetCurrentPlayerName();
    inventory_lookup.clear(); // discard now outdated id caches
    while (!hero_bag_generation_order.empty()) hero_bag_generation_order.pop();
    bag_ptr_to_hero_id.clear();
    std::wstring characters[] = {L"(Chest)", current_character};
    for (auto & character: characters) {
        if (character.empty()) continue;
        auto it = free_slots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, character}));
        std::shared_ptr<CharacterFreeSlots> free_slot = nullptr;
        if (it != free_slots.end()) {
            free_slot = (*it);
        } else {
            free_slot = std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, character});
            auto available_characters = GW::AccountMgr::GetAvailableChars();
            if (available_characters->size() > 0) {
                // alphabetically first character name, to be shown in tooltip to distinguish chests from multiple accounts without showing email addresses
                const wchar_t *min = nullptr;
                for (const auto& available_char : *available_characters) {
                    if (!min || wcscmp(available_char.player_name, min) < 0) min = available_char.player_name;
                }
                free_slot->account_representing_character = min;
            }
            free_slots.insert(free_slot);
        }
        if (character == current_character) {
            free_slot->occupied_equipment = 0;
            free_slot->occupied_inventory = 0;
        } else {
            free_slot->occupied_inventory = 0;
        }
    }
    if (!(reroll_stage == RerollStage::None || reroll_stage == RerollStage::WaitForCharacterLoad || reroll_stage >= RerollStage::RerollToItem)) {
        // map load at an inappropriate time during GatherAllInventories, e.g. due to manual intervention or network issues
        // abort rerolling
        reroll_stage = RerollStage::None;
    }
}

void AccountInventoryWindow::PostMapLoad()
{
    bool character_changed = false;
    map_loaded_delayed_trigger = true;
    map_loaded_delayed_timer = TIMER_INIT();
    std::wstring current_account = GetAccountEmail();
    std::wstring current_character = GW::AccountMgr::GetCurrentPlayerName();
    GW::Inventory* gw_inventory = GW::Items::GetInventory();
    if (last_character != current_character) {
        last_character = current_character;
        character_changed = true;
    }

    const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
    const GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
    if (party_info && me) {
        for (const auto &hero: party_info->heroes) {
            if (hero.owner_player_id != me->login_number) continue;
            HandleHeroBag(hero.hero_id);
        }
    }

    // clear empty slots in case inventory was changed without toolbox running.
    // update item->equipped flags.
    // track inventory size in order to display number of free slots
    if (gw_inventory) {
        uint32_t max_chest = 0;
        uint32_t max_equipment = 0;
        uint32_t max_inventory = 0;
        bool last_chest_pane_contains_any_item = false;
        for (uint32_t bag_id = 1; bag_id < std::size(gw_inventory->bags); ++bag_id) {
            auto bag = gw_inventory->bags[bag_id];
            std::wstring character = current_character;
            if (IsChestBag(bag_id)) {
                character = L"(Chest)";
            }
            if (!bag) {
                // clear slots in case a previously present bag was removed
                for (uint32_t slot = 0; slot < (uint32_t)BAG_MAX_SIZE[bag_id]; ++slot) {
                    ClearMissingItem(current_account, character, GW::Constants::HeroID::NoHero, bag_id, slot);
                }
                continue;
            }
            if (IsChestBag(bag_id)) {
                last_chest_pane_contains_any_item = false;
            }
            for (uint32_t slot = 0; slot < std::size(bag->items); ++slot) {
                auto item = bag->items[slot];
                if (item) {
                    if (IsChestBag(bag_id)) {
                        last_chest_pane_contains_any_item = true;
                    }
                    if (!inventory_lookup.contains(item->item_id)) {
                        AddItem(item->item_id);
                    }
                    // item->equipped is never set when an item triggers InventorySlotUpdated on map load.
                    // manually check and reapply after every map load.
                    auto i = inventory_lookup[item->item_id];
                    if (i->item_partial.equipped != item->equipped) {
                        i->item_partial.equipped = item->equipped;
                        inventory_dirty.insert(GetIniID(i->account, i->character));
                    }
                } else {
                    ClearMissingItem(current_account, character, GW::Constants::HeroID::NoHero, bag_id, slot);
                }
            }
            if (bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
                max_equipment = std::size(bag->items);
            } else if (BAG_CAN_HOLD_ANYTHING[bag_id]) {
                if (IsChestBag(bag_id)) {
                    max_chest += std::size(bag->items);
                } else {
                    max_inventory += std::size(bag->items);
                }
            }
        }
        std::wstring characters[] = {L"(Chest)", current_character};
        for (auto & character: characters) {
            if (auto it = free_slots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, character})); it != free_slots.end()) {
                if (character == current_character) {
                    (*it)->max_equipment = max_equipment;
                    (*it)->max_inventory = max_inventory;
                } else {
                    // Since we do not know whether the anniversary storage pane is actually available,
                    // assume it is not, unless there has been at least one item in it at some point.
                    if ((*it)->anniversary_pane_active || last_chest_pane_contains_any_item) {
                        (*it)->anniversary_pane_active = true;
                    } else {
                        max_chest -= 25;
                    }
                    (*it)->max_inventory = max_chest;
                }
            }
        }
    }

    // erase deleted characters
    if (character_changed) {
        std::set<std::wstring> availableChars{};
        const auto chars = GW::AccountMgr::GetAvailableChars();
        for (const auto& availableCharacter : *chars) {
            availableChars.insert(availableCharacter.player_name);
        }
        if (availableChars != last_available_chars) {
            for (auto it = inventory.begin(); it != inventory.end();) {
                if (!IsChestBag((*it)->bag_id) && (*it)->account == current_account) {
                    if (auto avail = availableChars.find((*it)->character); avail == availableChars.end()) {
                        // not found
                        inventory_dirty.insert(GetIniID((*it)->account, (*it)->character));
                        free_slots.erase(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, (*it)->character}));
                        inventory_lookup.erase((*it)->item_partial.item_id);
                        it = inventory.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
        last_available_chars = availableChars;
    }

    needs_sorting = true;
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        SaveToFiles(false); // save inventory in outposts only to avoid impacting gameplay
    }
    switch (reroll_stage) {
        case RerollStage::None:
            break;
        case RerollStage::WaitForCharacterLoad:
            save_hero_timer = TIMER_INIT();
            reroll_stage = RerollStage::DoSaveHeroes;
            break;
        // RerollStage::RerollToItem is handled in OnMapLoadedDelayed as it might call MoveItem,
        // which does not work immediately after loading into a map
    }
}

void AccountInventoryWindow::OnMapLoadedDelayed()
{
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        // not done loading, retry later
        map_loaded_delayed_timer = TIMER_INIT();
        return;
    }
    map_loaded_delayed_trigger = false;
    if (reroll_stage == RerollStage::RerollToItem) {
        StepReroll();
        return;
    }
}

void AccountInventoryWindow::HandleHeroBag(uint32_t hero_id)
{
    if (initializing) return;
    // If two parties are joined in a way s.t. our hero is in a different position afterwards,
    // it gets added a again, so we readd the hero bag we already know.
    // Otherwise the heroes bag should have been added to hero_bag_generation_order before this,
    // since hero bags are never empty.
    GW::Bag * bag = OnPartyRemoveHero(hero_id);
    if (!bag) {
        ASSERT(!hero_bag_generation_order.empty());
        bag = hero_bag_generation_order.front();
        hero_bag_generation_order.pop();
    }
    std::wstring current_account = GetAccountEmail();
    std::wstring current_character = GW::AccountMgr::GetCurrentPlayerName();
    bag_ptr_to_hero_id[bag] = hero_id;
    for (uint32_t slot = 0; slot < std::size(bag->items); ++slot) {
        auto item = bag->items[slot];
        if (!item) ClearMissingItem(current_account, current_character, hero_id, (uint32_t)GW::Constants::Bag::Equipped_Items, slot);
        else AddItem(item->item_id);
    }
}

void AccountInventoryWindow::GatherAllInventories()
{
    reroll_hero_queue.clear();
    reroll_char_queue.clear();
    cached_heroes.clear();
    auto available_characters = GW::AccountMgr::GetAvailableChars();
    for (const auto& available_char : *available_characters) {
        const auto char_select_info = GW::AccountMgr::GetAvailableCharacter(available_char.player_name);
        if (!char_select_info) {
            continue;
        }
        if (wcscmp(available_char.player_name, GW::AccountMgr::GetCurrentPlayerName()) == 0) {
            // process current char last, so we automatically end up back where we started.
            reroll_char_queue.insert(reroll_char_queue.begin(), available_char);
        } else {
            reroll_char_queue.push_back(available_char);
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
    reroll_stage = RerollStage::NextCharacter;
    StepReroll();
}

void AccountInventoryWindow::OnRerollPromptReply()
{
    auto next_char = reroll_char_queue.back();
    reroll_char_queue.clear();
    RerollWindow::Instance().Reroll(next_char.player_name, false, false, true, true);
}

void AccountInventoryWindow::OnPartyAddHero(uint32_t hero_id)
{
    HandleHeroBag(hero_id);
    if (reroll_stage == RerollStage::None) return;
    switch (reroll_stage) {
        case RerollStage::WaitForHeroLoad:
            reroll_stage = RerollStage::DoneHeroLoad;
            StepReroll();
            return;
        case RerollStage::DoRestoreHeroes:
            const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
            if (party_info) {
                if (party_info->heroes.size() < cached_heroes.size()) return;
            }
            cached_heroes.clear();
            reroll_stage = RerollStage::NextCharacter;
            StepReroll();
            return;
    }
}

GW::Bag * AccountInventoryWindow::OnPartyRemoveHero(uint32_t hero_id)
{
    GW::Bag * bag = nullptr;
    for (auto it = bag_ptr_to_hero_id.begin(); it != bag_ptr_to_hero_id.end();) {
        if (it->second == hero_id) {
            bag = it->first;
            it = bag_ptr_to_hero_id.erase(it);
        } else {
            ++it;
        }
    }
    return bag;
}

void AccountInventoryWindow::SaveHeroes()
{
    if (!cached_heroes.empty()) return;
    const GW::PartyInfo* party_info = GW::PartyMgr::GetPartyInfo();
    const GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
    if (!(party_info && me)) return;
    for (const auto &hero: party_info->heroes) {
        if (hero.owner_player_id != me->login_number) continue;
        cached_heroes.push_back(hero.hero_id);
    }
}

void AccountInventoryWindow::RestoreHeroes()
{
    GW::PartyMgr::KickAllHeroes();
    if (cached_heroes.empty()) {
        reroll_stage = RerollStage::NextCharacter;
        StepReroll();
        return;
    }
    for (auto hero_id: cached_heroes) {
        GW::PartyMgr::AddHero((GW::Constants::HeroID)hero_id);
    }
}

void AccountInventoryWindow::StepReroll()
{
    GW::AvailableCharacterInfo next_char{};
    uint32_t next_hero{};
    switch (reroll_stage) {
        case RerollStage::NextCharacter:
            if (reroll_char_queue.empty()) {
                reroll_stage = RerollStage::None;
                SaveToFiles(false);
                return;
            }
            reroll_stage = RerollStage::WaitForCharacterLoad;
            next_char = reroll_char_queue.back();
            reroll_char_queue.pop_back();
            reroll_hero_queue = CompletionWindow::GetCharacterCompletion(next_char.player_name)->heroes;
            if (!RerollWindow::Instance().Reroll(next_char.player_name, false, false, true, false)) {
                reroll_stage = RerollStage::None;
            }
            return;
        case RerollStage::DoneHeroLoad:
            if (reroll_hero_queue.empty()) {
                reroll_stage = RerollStage::DoRestoreHeroes;
                RestoreHeroes();
                return;
            }
            // fall through
        case RerollStage::DoneCharacterLoad:
            if (reroll_hero_queue.empty()) {
                reroll_stage = RerollStage::NextCharacter;
                StepReroll();
                return;
            }
            reroll_stage = RerollStage::WaitForHeroLoad;
            next_hero = reroll_hero_queue.back();
            reroll_hero_queue.pop_back();
            GW::PartyMgr::KickAllHeroes();
            GW::PartyMgr::AddHero((GW::Constants::HeroID)next_hero);
            add_hero_timer = TIMER_INIT();
            return;
        case RerollStage::RerollToItem:
            reroll_stage = RerollStage::None;
            if (cached_heroes.empty()) {
                // no cached_heroes means we rerolled for an item on the player character
                // move the item, but skip any hero related setup
                MoveItem();
                return;
            }
            auto hero_id = cached_heroes.front();
            // do not kick heroes, if the one we need is already added
            bool hero_is_present = false;
            for (auto it = bag_ptr_to_hero_id.begin(); it != bag_ptr_to_hero_id.end(); ++it) {
                if (it->second == hero_id) {
                    hero_is_present = true;
                    break;
                }
            }
            if (hero_is_present) {
                // hero is already in the party, just move the item
                MoveItem();
            } else {
                // MoveItem will be triggered through AddItem, once the item is available
                reroll_stage = RerollStage::WaitForHeroWithItem;
                GW::PartyMgr::KickAllHeroes();
                GW::PartyMgr::AddHero((GW::Constants::HeroID)hero_id);
                add_hero_timer = TIMER_INIT();
            }
            cached_heroes.clear();
            return;
    }
}

void AccountInventoryWindow::MoveItem()
{
    if (!item_to_move) return;
    std::shared_ptr<InventoryItem> i{};
    auto it = inventory.find(item_to_move);
    item_to_move = nullptr;
    if (it == inventory.end()) return;
    i = *it;

    // can only move from current player or from chest
    if (i->character != std::wstring(GW::AccountMgr::GetCurrentPlayerName()) && !IsChestBag(i->bag_id)) return;

    GW::GameThread::Enqueue([i=i] {
        auto item = GW::Items::GetItemById(i->item_partial.item_id);
        // plausibilize that our item_id was up to date, this should only be false immediately after loading a new map
        if (item && item->bag && item->slot == i->slot && (uint32_t)item->bag->bag_id() == i->bag_id && item->model_id == i->item_partial.model_id) {
            if (!IsChestBag(i->bag_id)) GW::Items::OpenXunlaiWindow();
            InventoryManager::MoveItem((InventoryManager::Item *)item);
        }
    });
}

// jump to location of clicked item, i.e. open chest/add hero/change character
// with Ctrl: move item to/from chest after jump
void AccountInventoryWindow::OnInventoryItemClicked(std::shared_ptr<InventoryItem> i, bool move) {
    if (map_loaded_delayed_trigger || reroll_stage != RerollStage::None || GW::Map::GetInstanceType() != GW::Constants::InstanceType::Outpost) return;
    std::wstring current_account = GetAccountEmail();
    if (i->account != current_account) return;
    if (move) {
        if (IsHeroArmor(i->hero_id, i->slot)) return; // can not unequip hero armor
        item_to_move = i;
    } else {
        item_to_move = nullptr;
    }
    bool item_is_in_chest = IsChestBag(i->bag_id);
    bool item_is_on_current_character = i->character == std::wstring(GW::AccountMgr::GetCurrentPlayerName());
    bool item_is_on_hero = (uint32_t)GW::Constants::HeroID::NoHero != i->hero_id;
    if (item_is_on_hero) {
        cached_heroes.clear();
        cached_heroes.push_back(i->hero_id);
    }

    if (item_is_in_chest && item_to_move) {
        MoveItem();
    } else if (item_is_in_chest && !item_to_move) {
        GW::GameThread::Enqueue([]() { GW::Items::OpenXunlaiWindow(); });
    } else if (item_is_on_current_character && item_is_on_hero) {
        // MoveItem will be triggered through StepReroll if the hero is already present.
        // Otherwise through AddItem, once the heroes inventory has been added by GW
        reroll_stage = RerollStage::RerollToItem;
        StepReroll();
    } else if (item_is_on_current_character && !item_is_on_hero && item_to_move) {
        MoveItem();
    } else if (!item_is_on_current_character) {
        // MoveItem will be triggered through StepReroll if the item is on a player character or
        // if the corresponding hero is already present.
        // Otherwise through AddItem, once the heroes inventory has been added by GW
        reroll_stage = RerollStage::RerollToItem;
        if (!RerollWindow::Instance().Reroll(i->character.c_str(), false, false, true, false)) {
            // reroll failed, abort
            reroll_stage = RerollStage::None;
            item_to_move = nullptr;
        }
    }
}

void AccountInventoryWindow::OnItemTooltip(std::shared_ptr<MergeStack> ms)
{
    std::wstring current_account = GetAccountEmail();
    std::wstring prev_character{};
    std::wstring prev_account_representing_character{};
    std::string prev_location{};
    for (auto it = ms->i.begin(); it != ms->i.end(); it++) {
        int style_count = 0;
        if ((*it)->account != current_account) {
            style_count = 1;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        std::wstring account_representing_character;
        if (auto fs_it = free_slots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{(*it)->account, (*it)->character})); fs_it != free_slots.end()) {
            account_representing_character = (*fs_it)->account_representing_character;
        }
        bool reprint = (*it)->character != prev_character || account_representing_character != prev_account_representing_character;
        if (reprint) {
            std::string suffix = "";
            if ((*it)->account != current_account && (*it)->character == L"(Chest)" && !account_representing_character.empty()) {
                suffix = " [" + TextUtils::WStringToString(account_representing_character) + "]";
            }
            ImGui::Text("%s%s", TextUtils::WStringToString((*it)->character).c_str(), suffix.c_str());
        }
        reprint |= (*it)->location != prev_location;
        if (reprint) {
            ImGui::Text("- %s", (*it)->location.c_str());
        }
        ImGui::PopStyleColor(style_count);
        prev_account_representing_character = account_representing_character;
        prev_character = (*it)->character;
        prev_location = (*it)->location;
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(440.f * ImGui::GetIO().FontGlobalScale);
    std::string quantity_str = "";
    if (ms->quantity > 1) {
        quantity_str = std::to_string(ms->quantity) + " ";
    }
    ImGui::Text("%s%s", quantity_str.c_str(), TextUtils::WStringToString(ms->description).c_str());
    ImGui::PopTextWrapPos();
}

void AccountInventoryWindow::SortSlots(ImGuiTableSortSpecs* sort_specs)
{
    std::wstring current_account = GetAccountEmail();
    free_slots_sorted = std::set<std::shared_ptr<CharacterFreeSlots>, SlotCompare>(SlotCompare{sort_specs});
    for (auto & free_slot: free_slots) {
        if (hide_other_accounts && free_slot->account != current_account) {
            continue;
        }
        free_slots_sorted.insert(free_slot);
    }
}

void AccountInventoryWindow::SortInventory(ImGuiTableSortSpecs* sort_specs)
{
    std::wstring current_account = GetAccountEmail();
    std::string merc_hero_name[8] = {};
    const auto ctx = GW::GetGameContext();
    auto& hero_array = ctx->world->hero_info;
    for (const auto& hero : hero_array) {
        if ((uint32_t)GW::Constants::HeroID::Merc1 <= hero.hero_id && hero.hero_id <= (uint32_t)GW::Constants::HeroID::Merc8) {
            merc_hero_name[hero.hero_id - (uint32_t)GW::Constants::HeroID::Merc1] = TextUtils::WStringToString(hero.name);
        }
    }
    inventory_sorted.clear();
    std::unordered_map<std::wstring, std::shared_ptr<MergeStack>> merged_stacks{};
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        auto i = *it;
        if (i->description.empty()) {
            QueueDescriptionDecode(i);
        }
        if ((uint32_t)GW::Constants::HeroID::Merc1 <= i->hero_id && i->hero_id <= (uint32_t)GW::Constants::HeroID::Merc8) {
            i->location = merc_hero_name[i->hero_id - (uint32_t)GW::Constants::HeroID::Merc1];
        }
        if (hide_other_accounts && i->account != current_account) {
            continue;
        }
        if (hide_equipment && (i->bag_id == (uint32_t)GW::Constants::Bag::Equipped_Items || i->item_partial.equipped)) {
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
        auto merge_id = std::to_wstring(i->item_partial.model_id) + i->description;
        std::shared_ptr<MergeStack> ms;
        if (merge_stacks && merged_stacks.contains(merge_id)) {
            ms = merged_stacks[merge_id];
        } else {
            ms = std::make_shared<MergeStack>(current_account);
            inventory_sorted.push_back(ms);
            if (i->description.empty()) {
                // do not merge stacks if their description still waits in description_decode_queue
                ms->description = L"loading...";
            } else {
                merged_stacks[merge_id] = ms;
                ms->description = i->description;
            }
        }
        ms->quantity += i->item_partial.quantity;
        ms->i.insert(i);
    }
    if (inventory_sorted.size() > 1) {
        std::sort(inventory_sorted.begin(), inventory_sorted.end(), ItemCompare{sort_specs, current_account});
    }
    if (sort_specs) sort_specs->SpecsDirty = false;
    needs_sorting = false;
}

void AccountInventoryWindow::Draw(IDirect3DDevice9*)
{
    auto font_scale = ImGui::GetIO().FontGlobalScale;
    std::wstring current_account = GetAccountEmail();
    auto style = ImGui::GetStyle();
    const float item_spacing = style.ItemInnerSpacing.x;
    float checkbox_max_width = 160.f * font_scale;
    ImVec4 color_chest_item                 = HSVRotate(style.Colors[ImGuiCol_Button], 0.333f);
    ImVec4 color_chest_item_hovered         = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.333f);
    ImVec4 color_chest_item_active          = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.333f);
    ImVec4 color_hero_item                  = HSVRotate(style.Colors[ImGuiCol_Button], 0.166f);
    ImVec4 color_hero_item_hovered          = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.166f);
    ImVec4 color_hero_item_active           = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.166f);
    ImVec4 color_item_foreign               = HSVRotate(style.Colors[ImGuiCol_Button], -1.f, 0.4f);
    ImVec4 color_item_hovered_foreign       = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], -1.f, 0.4f);
    ImVec4 color_item_active_foreign        = HSVRotate(style.Colors[ImGuiCol_ButtonActive], -1.f, 0.4f);
    ImVec4 color_chest_item_foreign         = HSVRotate(style.Colors[ImGuiCol_Button], 0.333f, 0.4f);
    ImVec4 color_chest_item_hovered_foreign = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.333f, 0.4f);
    ImVec4 color_chest_item_active_foreign  = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.333f, 0.4f);
    ImVec4 color_hero_item_foreign          = HSVRotate(style.Colors[ImGuiCol_Button], 0.166f, 0.4f);
    ImVec4 color_hero_item_hovered_foreign  = HSVRotate(style.Colors[ImGuiCol_ButtonHovered], 0.166f, 0.4f);
    ImVec4 color_hero_item_active_foreign   = HSVRotate(style.Colors[ImGuiCol_ButtonActive], 0.166f, 0.4f);

    if (RerollStage::None < reroll_stage && reroll_stage < RerollStage::RerollToItem) {
        ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.f * font_scale, 0.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Account Inventory loading in progress")) {
            ImGui::TextWrapped("Please do not interrupt inventory loading.");
            if (ImGui::Button("Abort!")) {
                reroll_stage = RerollStage::None;
            }
        }
        ImGui::End();
    }
    if (visible) {
        ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(760.f * font_scale, 400.f * font_scale), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags()) || ImGui::IsWindowCollapsed()) {
            ImGui::End();
            return;
        }

        // view related settings
        ImGui::Checkbox("Detailed View", &detailed_view);
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Merge Stacks", &merge_stacks)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Hide other Accounts", &hide_other_accounts)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Equipment", &hide_equipment)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Equipment Packs", &hide_equipment_pack)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Hide Hero Armor", &hide_hero_armor)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < checkbox_max_width) ImGui::NewLine();
        if (ImGui::Checkbox("Hide unclaimed Items", &hide_unclaimed_items)) needs_sorting = true;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < 110.f * font_scale) ImGui::NewLine();
        if (ImGui::Button("Gather Inventories")) {
            ImGui::ConfirmDialog("In order to load all available items, this will cycle\nthrough all characters and all heroes.\nThis will take a few minutes if you have many characters.\nAre you sure?", [](bool result, void*){if (result) AccountInventoryWindow::Instance().GatherAllInventories();});
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
            ImGuiTableSortSpecs* slot_sort_specs = ImGui::TableGetSortSpecs();
            if (slot_sort_specs && slot_sort_specs->SpecsDirty) {
                SortSlots(slot_sort_specs);
            }
            for (auto & free_slot: free_slots_sorted) {
                auto free_equipment = free_slot->max_equipment - free_slot->occupied_equipment;
                auto free_inventory = free_slot->max_inventory - free_slot->occupied_inventory;
                bool is_chest = free_slot->character == L"(Chest)";
                std::string suffix = "";
                int style_count = 0;
                if (free_slot->account != current_account) {
                    style_count = 1;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (is_chest && !free_slot->account_representing_character.empty()) {
                        suffix = " [" + TextUtils::WStringToString(free_slot->account_representing_character) + "]";
                    }
                }
                ImGui::TableNextColumn();
                if (free_slot->account == current_account) {
                    if (ImGui::Button(TextUtils::WStringToString(free_slot->character).c_str())) {
                        // reroll to target or open chest
                        OnInventoryItemClicked(std::make_shared<InventoryItem>(InventoryItem{free_slot->account, free_slot->character, 0, is_chest ? (uint32_t)GW::Constants::Bag::Storage_1 : 0, 0}), false);
                    }
                } else {
                    ImGui::Text("%s%s", TextUtils::WStringToString(free_slot->character).c_str(), suffix.c_str());
                }

                ImGui::TableNextColumn();
                if (free_slot->max_inventory) RightAlignText(std::to_string(free_inventory)+"/");

                ImGui::TableNextColumn();
                if (free_slot->max_inventory) ImGui::Text("%d", free_slot->max_inventory);

                ImGui::TableNextColumn();
                if (free_slot->max_equipment) RightAlignText(std::to_string(free_equipment)+"/");

                ImGui::TableNextColumn();
                if (free_slot->max_equipment) ImGui::Text("%d", free_slot->max_equipment);
                ImGui::PopStyleColor(style_count);
            }
            ImGui::EndTable();
        }

        float items_table_height = std::max(ImGui::GetContentRegionAvail().y, ITEMS_TABLE_MIN_HEIGHT);
        float inner_width = ImGui::GetContentRegionAvail().x - item_spacing;
        // sorting/filtering header
        ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody;
        if (detailed_view) {
            flags |= ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg;
        } else {
            items_table_height = 2 * ImGui::GetFrameHeight();
            flags |= ImGuiTableFlags_SizingFixedFit;
        }
        if (!ImGui::BeginTable("###itemstable", ItemColumnID_Max, flags, ImVec2(inner_width, items_table_height))) {
            ImGui::End();
            return;
        }
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Character);
        ImGui::TableSetupColumn("Location / Hero", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Location);
        ImGui::TableSetupColumn("Model ID", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_ModelID);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 0.f, ItemColumnID_Description);
        ImGui::TableSetupScrollFreeze(3, 2);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###name_filter", name_filter_buf, BUFFER_SIZE);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###location_filter", location_filter_buf, BUFFER_SIZE);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("###model_ID_filter", model_ID_filter_buf, BUFFER_SIZE);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(300.f * font_scale);
        ImGui::InputText("###item_filter", item_filter_buf, BUFFER_SIZE);
        ImGui::SameLine();
        ImGui::Text("Filter   %d/%d Items", filtered_item_count, inventory_sorted.size());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGuiTableSortSpecs* item_sort_specs = ImGui::TableGetSortSpecs();
        if (needs_sorting || (item_sort_specs && item_sort_specs->SpecsDirty)) {
            SortInventory(item_sort_specs);
        }


        if (!detailed_view) {
            // detailed view follows the structure of the sorting header
            // grid view needs its own table
            ImGui::EndTable();
            if (!ImGui::BeginTable("###itemgrid", std::max(1, (int)(inner_width / (3.3f*ImGui::GetTextLineHeight() + item_spacing))), ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY, ImVec2(inner_width, std::max(ImGui::GetContentRegionAvail().y, ITEMS_TABLE_MIN_HEIGHT)))) {
                ImGui::End();
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
        }

        auto name_filter = std::string(name_filter_buf);
        auto location_filter = std::string(location_filter_buf);
        auto model_ID_filter = std::string(model_ID_filter_buf);
        auto item_filter = std::string(item_filter_buf);
        bool name_is_lower = std::all_of(name_filter.begin(), name_filter.end(), [](unsigned char c){ return !std::isupper(c); });
        bool location_is_lower = std::all_of(location_filter.begin(), location_filter.end(), [](unsigned char c){ return !std::isupper(c); });
        bool item_is_lower = std::all_of(item_filter.begin(), item_filter.end(), [](unsigned char c){ return !std::isupper(c); });
        filtered_item_count = 0;
        for (auto & ims: inventory_sorted) {
            auto i_front = *(ims->i.begin());
            std::wstring description = ims->description;
            if (ims->quantity > 1) {
                description = std::to_wstring(ims->quantity) + L" " + description;
            }
            auto description_one_line = TextUtils::ctre_regex_replace<L"\n", L" - ">(description);
            uint16_t quantity = ims->quantity;

            // filter item
            bool filter_match = false;
            uint16_t filtered_quantity = 0;
            for (auto it = ims->i.begin(); it != ims->i.end(); it++) {
                std::wstring character_check = (*it)->character;
                std::string location_check = (*it)->location;
                if (name_is_lower) {
                    character_check = TextUtils::ToLower(character_check);
                }
                if (location_is_lower) {
                    location_check = TextUtils::ToLower(location_check);
                }
                // any pair of character/location must match, not all
                if (character_check.contains(TextUtils::StringToWString(name_filter)) &&
                    location_check.contains(location_filter)) {
                    if (!filter_match) {
                        // update interactable item to first item in merged stack matching the filter
                        i_front = *it;
                    }
                    filter_match = true;
                    filtered_quantity += (*it)->item_partial.quantity;
                }
            }
            if (filtered_quantity != quantity) {
                // If merge_stacks is enabled, it is possible that only some items in a merge stack
                // match the filters. Show what portion matched in the detailed view.
                description_one_line = std::to_wstring(filtered_quantity) + L"/" + description_one_line;
            }
            auto description_check = description_one_line;
            if (item_is_lower) {
                description_check = TextUtils::ToLower(description_check);
            }
            filter_match &= description_check.contains(TextUtils::StringToWString(item_filter));
            filter_match &= model_ID_filter.empty() || model_ID_filter == std::to_string(i_front->item_partial.model_id);

            if (!filter_match) continue;
            filtered_item_count++;

            // render item
            bool clicked = false;
            ImGui::PushID(filtered_item_count);
            int style_count = 0;
            if (i_front->account != current_account) {
                style_count += 1;
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (IsChestBag(i_front->bag_id)) {
                    style_count += 3;
                    ImGui::PushStyleColor(ImGuiCol_Button, color_chest_item_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_chest_item_hovered_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_chest_item_active_foreign);
                } else if (IsOnHero(i_front->hero_id)) {
                    style_count += 3;
                    ImGui::PushStyleColor(ImGuiCol_Button, color_hero_item_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_hero_item_hovered_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_hero_item_active_foreign);
                } else {
                    style_count += 3;
                    ImGui::PushStyleColor(ImGuiCol_Button, color_item_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_item_hovered_foreign);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_item_active_foreign);
                }
            } else {
                if (IsChestBag(i_front->bag_id)) {
                    style_count += 3;
                    ImGui::PushStyleColor(ImGuiCol_Button, color_chest_item);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_chest_item_hovered);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_chest_item_active);
                } else if (IsOnHero(i_front->hero_id)) {
                    style_count += 3;
                    ImGui::PushStyleColor(ImGuiCol_Button, color_hero_item);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_hero_item_hovered);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_hero_item_active);
                }
            }
            if (detailed_view) {
                std::string suffix = (ims->i.size() > 1) ? " +" : "";
                ImGui::Text("%s%s", TextUtils::WStringToString(i_front->character).c_str(), suffix.c_str());
                ImGui::TableNextColumn();

                ImGui::Text("%s%s", i_front->location.c_str(), suffix.c_str());
                ImGui::TableNextColumn();

                ImGui::Text("%d", i_front->item_partial.model_id);
                ImGui::TableNextColumn();

                style.ButtonTextAlign = ImVec2(0.f, 0.5f);
                clicked = ImGui::Button(TextUtils::WStringToString(description_one_line).c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip([ims]() { AccountInventoryWindow::Instance().OnItemTooltip(ims); });
                }
                ImGui::TableNextColumn();
            } else { // grid view
                const ImVec2 pos = ImGui::GetCursorPos();
                auto w = 3.3f*ImGui::GetTextLineHeight();
                if (i_front->texture && *(i_front->texture)) {
                    clicked = ImGui::IconButton("", *i_front->texture, ImVec2(w, w), ImGuiButtonFlags_None, ImVec2(w, w));
                } else {
                    clicked = ImGui::Button("???", ImVec2(w, w));
                }
                if (quantity > 1) {
                    ImGui::SetCursorPos(ImVec2(pos.x + item_spacing, pos.y));
                    ImGui::TextColored(ImVec4(0.98f, 0.97f, 0.6f, 1.f), "%d", quantity);
                    ImGui::SetCursorPos(pos);
                    ImGui::Dummy(ImVec2(w, w));
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip([ims]() { AccountInventoryWindow::Instance().OnItemTooltip(ims); });
                }
                ImGui::TableNextColumn();
            }
            ImGui::PopStyleColor(style_count);
            ImGui::PopID();

            if (clicked) {
                // reroll to target or open chest
                OnInventoryItemClicked(i_front, ImGui::IsKeyDown(ImGuiMod_Ctrl));
            }
        }
        ImGui::EndTable();
        ImGui::End();
    }
}

void AccountInventoryWindow::DrawSettingsInternal()
{
    auto font_scale = ImGui::GetIO().FontGlobalScale;
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::Text("Account Inventory shows a combined view of all player, hero and storage inventories.");
    if (ImGui::Button("Gather Inventories")) {
        visible = true;
        ImGui::ConfirmDialog("In order to load all available items, this will cycle\nthrough all characters and all heroes.\nThis will take a few minutes if you have many characters.\nAre you sure?", [](bool result, void*){if (result) AccountInventoryWindow::Instance().GatherAllInventories();});
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Account Inventory")) {
        show_delete_note = true;
        inventory.clear();
        inventory_lookup.clear();
        inventory_sorted.clear();
        free_slots.clear();
        for (auto it = ini_by_character.begin(); it != ini_by_character.end(); ++it) {
            inventory_dirty.insert(it->first);
        }
        SaveToFiles(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete All Inventories")) {
        show_delete_note = true;
        LoadFromFiles(false); // reload everything first, so we are aware of all inventory inis currently on disk
        inventory.clear();
        inventory_lookup.clear();
        inventory_sorted.clear();
        free_slots.clear();
        for (auto it = ini_by_path.begin(); it != ini_by_path.end(); ++it) {
            it->second->Reset();
        }
        SaveToFiles(true);
    }
    ImGui::Checkbox("###account_inventory_detailed_view", &detailed_view);
    ImGui::SameLine();
    ImGui::Text("Detailed View - Toggle between detailed list and icon grid view.");
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
    if (show_delete_note) {
        // we could just disable this module ourselves, if ToolboxSettings' ModuleToggle.enabled was part of ToolboxModule
        ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.f * font_scale, 0.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Deleting Inventories", &show_delete_note)) {
            ImGui::TextWrapped("Make sure to disable Account Inventory in Toolbox Settings -> Windows to stop it from regathering inventory data.");
            if (ImGui::Button("Ok")) {
                show_delete_note = false;
            }
        }
        ImGui::End();
    }
}

void AccountInventoryWindow::LoadSettings(ToolboxIni* ini)
{
    ToolboxWindow::LoadSettings(ini);
    LOAD_BOOL(detailed_view);
    LOAD_BOOL(merge_stacks);
    LOAD_BOOL(hide_other_accounts);
    LOAD_BOOL(hide_equipment);
    LOAD_BOOL(hide_equipment_pack);
    LOAD_BOOL(hide_hero_armor);
    LOAD_BOOL(hide_unclaimed_items);
    needs_sorting = true;
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
    SAVE_BOOL(detailed_view);
    ToolboxWindow::SaveSettings(ini);
    SaveToFiles(false);
}

// unique section name for item in ini file
std::string AccountInventoryWindow::ItemToSectionName(std::shared_ptr<InventoryItem> i) const
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

bool AccountInventoryWindow::CheckIniDirty(std::shared_ptr<InventoryIni> ini)
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

std::shared_ptr<AccountInventoryWindow::InventoryIni> AccountInventoryWindow::GetIni(std::wstring ini_ID, std::wstring account)
{
    if (!ini_by_character.contains(ini_ID)) {
        wchar_t path[MAX_PATH];
        if (0 == GetTempFileNameW(Resources::GetPath(L"inventories").wstring().c_str(), L"inv", 0, path)) {
            Log::Error("Account Inventory: Failed to create inventory ini. Inventory tracking data will be lost.");
            return nullptr;
        }
        auto ini = std::make_shared<InventoryIni>(path);
        ini->ini_ID = ini_ID;
        ini->account = account;
        ini_by_character[ini_ID] = ini;
        ini_by_path[path] = ini_by_character[ini_ID];
    }
    return ini_by_character[ini_ID];
}

void AccountInventoryWindow::LoadFromFiles(bool only_foreign)
{
    ToolboxIni::TNamesDepend entries{};
    std::wstring current_account = GetAccountEmail();

    Resources::EnsureFolderExists(Resources::GetPath(L"inventories"));
    std::unordered_set<std::filesystem::path> visited;
    std::unordered_set<std::shared_ptr<InventoryItem>, ItemHash, ItemEqual> inventory_rebuild{};
    std::unordered_set<std::shared_ptr<CharacterFreeSlots>, SlotHash, SlotEqual> free_slots_rebuild{};
    for (auto const &file: std::filesystem::directory_iterator{Resources::GetPath(L"inventories")}) {
        auto path = file.path();
        visited.insert(path);
        if (!ini_by_path.contains(path)) {
            ini_by_path[path] = std::make_shared<InventoryIni>(path);
        }
        auto ini = ini_by_path[path];
        if (only_foreign && ini->account == current_account) continue;
        if (CheckIniDirty(ini)) {
            ini->Reset();
            if (ini->LoadFile(path.wstring()) < 0) continue;
        }
        ini->GetAllSections(entries);
        for (const ToolboxIni::Entry& entry : entries) {
            const char* section = entry.pItem;

            // account and character values must exist in both freeslot and item sections
            auto account = TextUtils::Base64Decode<wchar_t>(ini->GetValue(section, "account", ""));
            auto character = TextUtils::Base64Decode<wchar_t>(ini->GetValue(section, "character", ""));
            if (ini->ini_ID.empty()) {
                ini->ini_ID = GetIniID(account, character);
                ini->account = account;
                ini_by_character[ini->ini_ID] = ini;
            } else if (ini->ini_ID != GetIniID(account, character)) {
                continue;
            }
            if (only_foreign && account == current_account) continue;
            
            if (std::string_view(section) == "freeslots") {
                auto free_slot = std::make_shared<CharacterFreeSlots>();
                free_slot->account = account;
                free_slot->character = character;
                free_slot->account_representing_character = TextUtils::Base64Decode<wchar_t>(ini->GetValue(section, "account_character", ""));
                free_slot->max_equipment = (int)(ini->GetLongValue(section, "maxequipment", 0));
                free_slot->max_inventory = (int)(ini->GetLongValue(section, "maxinventory", 0));
                free_slot->occupied_equipment = (int)(ini->GetLongValue(section, "occupiedequipment", 0));
                free_slot->occupied_inventory = (int)(ini->GetLongValue(section, "occupiedinventory", 0));
                free_slot->anniversary_pane_active = ini->GetBoolValue(section, "anniversary_pane_active", false);
                free_slots_rebuild.insert(free_slot);
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
                i->info_string = TextUtils::Base64Decode<wchar_t>(info_string_b64);
                i->item_partial.info_string = i->info_string.data();
            }
            const char * single_item_name_b64 = ini->GetValue(section, "singleitemname", nullptr);
            if (single_item_name_b64) {
                i->single_item_name = TextUtils::Base64Decode<wchar_t>(single_item_name_b64);
                i->item_partial.single_item_name = i->single_item_name.data();
            }
            const char * complete_name_enc_b64 = ini->GetValue(section, "completename", nullptr);
            if (complete_name_enc_b64) {
                i->complete_name_enc = TextUtils::Base64Decode<wchar_t>(complete_name_enc_b64);
                i->item_partial.complete_name_enc = i->complete_name_enc.data();
            }
            const char * name_enc_b64 = ini->GetValue(section, "name", nullptr);
            if (name_enc_b64) {
                i->name_enc = TextUtils::Base64Decode<wchar_t>(name_enc_b64);
                i->item_partial.name_enc = i->name_enc.data();
            }
            const char * customized_b64 = ini->GetValue(section, "customized", nullptr);
            if (customized_b64) {
                i->customized = TextUtils::Base64Decode<wchar_t>(customized_b64);
                i->item_partial.customized = i->customized.data();
            }
            i->item_partial.type = (GW::Constants::ItemType)(ini->GetLongValue(section, "type", 0));
            i->item_partial.model_id = (uint32_t)(ini->GetLongValue(section, "modelid", 0));
            i->item_partial.model_file_id = (uint32_t)(ini->GetLongValue(section, "modelfileid", 0));
            i->item_partial.interaction = (uint32_t)(ini->GetLongValue(section, "interaction", 0));
            i->item_partial.quantity = (uint16_t)(ini->GetLongValue(section, "quantity", 0));
            i->item_partial.equipped = (uint8_t)(ini->GetLongValue(section, "equipped", 0));
            i->item_partial.item_id = 0;
            i->texture = Resources::GetItemImage(&(i->item_partial));
            i->location = HERO_NAME[i->hero_id];
            if (IsChestBag(i->bag_id)) {
                i->location = BAG_NAME[(int)(i->bag_id)];
            }

            inventory_rebuild.insert(i);
        }
    }
    if (only_foreign) {
        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            if ((*it)->account == current_account) inventory_rebuild.insert(*it);
        }
        for (auto it = free_slots.begin(); it != free_slots.end(); ++it) {
            if ((*it)->account == current_account) free_slots_rebuild.insert(*it);
        }
    }
    for (auto it = ini_by_path.begin(); it != ini_by_path.end(); ++it) {
        if (!visited.contains(it->first)) it->second->Reset();
    }
    free_slots = free_slots_rebuild;
    inventory = inventory_rebuild;
    needs_sorting = true;
}

void AccountInventoryWindow::SaveToFiles(bool include_foreign)
{
    std::wstring current_account = GetAccountEmail();
    std::unordered_set<std::wstring> visited;

    for (auto & free_slot: free_slots) {
        auto ini_ID = GetIniID(free_slot->account, free_slot->character);
        if (!inventory_dirty.contains(ini_ID)) continue;
        if (current_account.empty() || free_slot->account != current_account) continue; // skip foreign items, only update what belongs to us
        auto ini = GetIni(ini_ID, free_slot->account);
        if (!ini) return;
        if (!visited.contains(ini_ID)) {
            ini->Reset();
            visited.insert(ini_ID);
        }
        auto section = "freeslots";
        if (!free_slot->account.empty()) {
            ini->SetValue(section, "account", TextUtils::Base64Encode<wchar_t>(free_slot->account).c_str());
        }
        if (!free_slot->character.empty()) {
            ini->SetValue(section, "character", TextUtils::Base64Encode<wchar_t>(free_slot->character).c_str());
        }
        if (!free_slot->account_representing_character.empty()) {
            ini->SetValue(section, "account_character", TextUtils::Base64Encode<wchar_t>(free_slot->account_representing_character).c_str());
        }
        ini->SetLongValue(section, "maxequipment", free_slot->max_equipment);
        ini->SetLongValue(section, "maxinventory", free_slot->max_inventory);
        ini->SetLongValue(section, "occupiedequipment", free_slot->occupied_equipment);
        ini->SetLongValue(section, "occupiedinventory", free_slot->occupied_inventory);
        if (free_slot->anniversary_pane_active) {
            ini->SetBoolValue(section, "anniversary_pane_active", free_slot->anniversary_pane_active);
        }
    }

    for (auto & i: inventory) {
        auto ini_ID = GetIniID(i->account, i->character);
        if (!inventory_dirty.contains(ini_ID)) continue;
        if (current_account.empty() || i->account != current_account) continue; // skip foreign items, only update what belongs to us
        auto ini = GetIni(ini_ID, i->account);
        if (!ini) return;
        if (!visited.contains(ini_ID)) {
            ini->Reset();
            visited.insert(ini_ID);
        }
        auto section = ItemToSectionName(i);
        ini->SetValue(section.c_str(), "account", TextUtils::Base64Encode<wchar_t>(i->account).c_str());
        ini->SetValue(section.c_str(), "character", TextUtils::Base64Encode<wchar_t>(i->character).c_str());
        ini->SetLongValue(section.c_str(), "heroid", (long)i->hero_id);
        ini->SetLongValue(section.c_str(), "bagid", (long)i->bag_id);
        ini->SetLongValue(section.c_str(), "slot", (long)i->slot);
        if (i->item_partial.info_string) {
            ini->SetValue(section.c_str(), "info", TextUtils::Base64Encode<wchar_t>(i->item_partial.info_string).c_str());
        }
        if (i->item_partial.single_item_name) {
            ini->SetValue(section.c_str(), "singleitemname", TextUtils::Base64Encode<wchar_t>(i->item_partial.single_item_name).c_str());
        }
        if (i->item_partial.complete_name_enc) {
            ini->SetValue(section.c_str(), "completename", TextUtils::Base64Encode<wchar_t>(i->item_partial.complete_name_enc).c_str());
        }
        if (i->item_partial.name_enc) {
            ini->SetValue(section.c_str(), "name", TextUtils::Base64Encode<wchar_t>(i->item_partial.name_enc).c_str());
        }
        if (i->item_partial.customized) {
            ini->SetValue(section.c_str(), "customized", TextUtils::Base64Encode<wchar_t>(i->item_partial.customized).c_str());
        }
        ini->SetLongValue(section.c_str(), "type", (long)i->item_partial.type);
        ini->SetLongValue(section.c_str(), "modelid", (long)i->item_partial.model_id);
        ini->SetLongValue(section.c_str(), "modelfileid", (long)i->item_partial.model_file_id);
        ini->SetLongValue(section.c_str(), "interaction", (long)i->item_partial.interaction);
        ini->SetLongValue(section.c_str(), "quantity", (long)i->item_partial.quantity);
        ini->SetLongValue(section.c_str(), "equipped", (long)i->item_partial.equipped);
    }
    for (auto it = ini_by_character.begin(); it != ini_by_character.end(); ++it) {
        auto ini_ID = it->first;
        if (!include_foreign && it->second->account != current_account) continue;
        if (include_foreign || visited.contains(ini_ID)) {
            if (it->second->SaveFile(it->second->location_on_disk.wstring().c_str()) != SI_OK) {
                Log::Error("Account Inventory: Failed to save inventory ini. Inventory tracking data will be lost.");
            }
        } else if (inventory_dirty.contains(ini_ID)) {
            // dirty but not visited means there are no more items in this inventory. clean up its ini
            it->second->Reset();
        }
    }
    // separate loop because deleting from ini_by_character in the above loop gets really unreadable
    for (auto it = ini_by_path.begin(); it != ini_by_path.end();) {
        if (it->second->IsEmpty()) {
            DeleteFileW(it->first.wstring().c_str());
            ini_by_character.erase(it->second->ini_ID);
            it = ini_by_path.erase(it);
        } else ++it;
    }
    inventory_dirty.clear();
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
        description_decode_lock,
        description_decode_queue,
    };
    switch (i->item_partial.type) {
        case GW::Constants::ItemType::Headpiece:
        case GW::Constants::ItemType::Boots:
        case GW::Constants::ItemType::Chestpiece:
        case GW::Constants::ItemType::Gloves:
        case GW::Constants::ItemType::Leggings:
            // ShorthandItemDescription includes item name for these
            break;
        default:
            // Default to single_item_name so merge_stacks can combine stacks of single and multiple items.
            if (i->item_partial.single_item_name) {
                sync->q->description += i->item_partial.single_item_name;
            } else if (i->item_partial.complete_name_enc) {
                sync->q->description += i->item_partial.complete_name_enc;
            } else if (i->item_partial.name_enc) {
                sync->q->description += i->item_partial.name_enc;
            }
    }
    if (i->item_partial.info_string) {
        auto shorthand_description = ToolboxUtils::ShorthandItemDescription(&(i->item_partial));
        // If item info_string starts with "Value:", ShorthandItemDescription doesn't filter the "Value:" part out.
        // Since "Value:" is typically at the end of the description, there is nothing left that we care about anyway.
        // Add description only if it does not start with "Value:".
        if (shorthand_description.find(L"\xA3E\x10A\xA8A\x10A\xA59\x1\x10B") != 0) {
            if (!sync->q->description.empty()) {
                sync->q->description += L"\x2\x102\x2";
            }
            sync->q->description += shorthand_description;
        }
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

void AccountInventoryWindow::AddItem(uint32_t item_id)
{
    auto item = GW::Items::GetItemById(item_id);
    if (!(item && item->bag)) return;

    // gather information for this items storage location, i.e.:
    // account, player character, hero, bag, slot within bag
    std::wstring current_account = GetAccountEmail();
    auto i = std::make_shared<InventoryItem>();
    i->account = current_account;
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

    // This is a workaround because I could not find a way to get a hero_id from an item currently equipped on a hero.
    // item->bag->bag_array is a separate array for each hero with only the Equipped_Items bag set, but seemingly no reference back to the hero.
    // The workaround uses the fact that items are added by GW in the order of the respective heroes in the party.
    i->hero_id = GW::Constants::HeroID::NoHero;
    if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipped_Items && (GW::Inventory *)item->bag->bag_array != GW::Items::GetInventory()) {
        // If we are loaded on a map when this module gets initialized, we will visit items in an arbitrary order
        // and therefore we are unable to guess which hero an item belongs to.
        // In this case we can ass items on heroes only once we load into a new map or if the heroes are
        // removed and added again.
        if (initializing) return;
        // Outside of initialization, we get here when a hero is added or when a map is loaded.
        // In both cases items are added in the order of the respective heroes in the party.
        if (!bag_ptr_to_hero_id.contains(item->bag)) {
            // Queue hero bag for later.
            // Items will be added through HandleHeroBag once GW created the hero.
            if (item->slot == CHEST_ARMOR_INVENTORY_SLOT) {
                hero_bag_generation_order.push(item->bag);
            }
            return;
        } else {
            i->hero_id = bag_ptr_to_hero_id[item->bag];
        }
    }
    // END hero_id workaround

    // gather members of item referenced in ShorthandItemDescription, GetItemImage or used by AccountInventoryWindow internally
    if (item->info_string) {
        i->info_string = item->info_string;
        i->item_partial.info_string = i->info_string.data();
    }
    if (item->single_item_name) {
        i->single_item_name = item->single_item_name;
        i->item_partial.single_item_name = i->single_item_name.data();
    }
    if (item->complete_name_enc) {
        i->complete_name_enc = item->complete_name_enc;
        i->item_partial.complete_name_enc = i->complete_name_enc.data();
    }
    if (item->name_enc) {
        i->name_enc = item->name_enc;
        i->item_partial.name_enc = i->name_enc.data();
    }
    if (item->customized) {
        i->customized = item->customized;
        i->item_partial.customized = i->customized.data();
    }
    i->item_partial.type = item->type;
    i->item_partial.model_id = item->model_id;
    i->item_partial.model_file_id = item->model_file_id;
    i->item_partial.interaction = item->interaction;
    i->item_partial.quantity = item->quantity;
    i->item_partial.equipped = item->equipped;
    i->item_partial.item_id = item->item_id;
    i->texture = Resources::GetItemImage(&(i->item_partial));
    i->location = HERO_NAME[i->hero_id];
    if (IsChestBag(i->bag_id)) {
        i->location = BAG_NAME[(int)(i->bag_id)];
    }

    if (auto it = free_slots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, i->character})); it != free_slots.end()) {
        if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
            (*it)->occupied_equipment++;
        } else if (BAG_CAN_HOLD_ANYTHING[i->bag_id]) {
            (*it)->occupied_inventory++;
        }
    }
    if (auto it = inventory.find(i); it != inventory.end()) {
        // found
        auto o_item_id = (*it)->item_partial.item_id;
        // make sure the lookup entry has not already been overwritten by another item being loaded during map load.
        if (inventory_lookup.contains(o_item_id) && inventory_lookup[o_item_id] == (*it)) {
            inventory_lookup.erase(o_item_id);
        }
    }
    // .insert does not replace the shared_ptr if it is deemed equal by ItemEqual. Erase it first
    inventory.erase(i);
    inventory.insert(i);
    inventory_lookup[item->item_id] = i;
    needs_sorting = true;
    inventory_dirty.insert(GetIniID(i->account, i->character));

    if (item_to_move && (uint32_t)GW::Constants::HeroID::NoHero != i->hero_id && ItemEqual{}(item_to_move, i)) {
        // If we had to change characters and item_to_move is on a player character,
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
        inventory_dirty.insert(GetIniID((*it)->account, (*it)->character));
        RemoveItem((*it)->item_partial.item_id);
        // Most likely the missing item was still in our inifile but removed ingame.
        // In this case we won't know an item_id, but still want to remove it from inventory
        inventory.erase(it);
    }
}

void AccountInventoryWindow::RemoveItem(uint32_t item_id)
{
    if (!inventory_lookup.contains(item_id)) return;

    std::wstring current_account = GetAccountEmail();
    auto i = inventory_lookup[item_id];
    if (auto it = free_slots.find(std::make_shared<CharacterFreeSlots>(CharacterFreeSlots{current_account, i->character})); it != free_slots.end()) {
        if (i->bag_id == (uint32_t)GW::Constants::Bag::Equipment_Pack) {
            (*it)->occupied_equipment--;
        } else if (BAG_CAN_HOLD_ANYTHING[i->bag_id]) {
            (*it)->occupied_inventory--;
        }
    }
    inventory.erase(i);
    inventory_lookup.erase(item_id);
    needs_sorting = true;
    inventory_dirty.insert(GetIniID(i->account, i->character));
}
