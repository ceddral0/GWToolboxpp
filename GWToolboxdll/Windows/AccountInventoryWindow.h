#pragma once

#include <GWCA/Context/CharContext.h>
#include <Utils/ToolboxUtils.h>
#include <ToolboxWindow.h>
#include <mutex>
#include <memory>
#include <atomic>
#include <Timer.h>
#include <GWCA/Context/CharContext.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/Packets/StoC.h>

#include <windows.h>

class AccountInventoryWindow : public ToolboxWindow {
    inline static const size_t UUID_BYTE_SIZE = 4 * sizeof(uint32_t);
    inline static const float itemsTableMinHeight = 220.f;

    enum ItemColumnID
    {
        ItemColumnID_Character,
        ItemColumnID_Location,
        ItemColumnID_ModelID,
        ItemColumnID_Description,
        ItemColumnID_Max,
    };

    enum SlotColumnID
    {
        SlotColumnID_Character,
        SlotColumnID_Inventory,
        SlotColumnID_InventorySize,
        SlotColumnID_Equipment,
        SlotColumnID_EquipmentSize,
        SlotColumnID_Max,
    };

    struct InventoryItem {
        // identifying attributes
        std::wstring account{};
        std::wstring character{};
        uint32_t hero_id{};
        uint32_t bag_id{};
        uint32_t slot{};

        // further attributes necessary to display items
        GW::Item itemPartial{}; // everything needed to get a ShorthandItemDescription and GetItemImage
        // memory management for allocated itemPartial members
        std::wstring info_string{};
        std::wstring single_item_name{};
        std::wstring complete_name_enc{};
        std::wstring name_enc{};
        std::wstring customized{};

        // caches, do not serialize
        IDirect3DTexture9** texture; // output of GetItemImage
        std::wstring description{}; // output of AsyncDecodeStr(ShorthandItemDescription)
        std::string location{}; // (Player) <Storage Pane> or <Hero Name>

    };
    struct MergeStack;
    struct ItemCompare {
        ImGuiTableSortSpecs* sortSpecs{};
        std::wstring currentAccount{};
        // used for order in Draw
        bool operator()(const std::shared_ptr<MergeStack> lms, const std::shared_ptr<MergeStack> rms) const
        {
            int sortDirection = 1;
            int delta = 0;
            if (rms->i.size() == 0) return false;
            if (lms->i.size() == 0) return true;
            auto l = *(lms->i.begin());
            auto r = *(rms->i.begin());
            if (sortSpecs) {
                for (int n = 0; n < sortSpecs->SpecsCount; n++) {
                    const ImGuiTableColumnSortSpecs* sort_spec = &sortSpecs->Specs[n];
                    sortDirection = (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? 1 : -1;
                    delta = 0;
                    switch (sort_spec->ColumnUserID) {
                        case ItemColumnID_Character:
                            delta = l->character.compare(r->character);
                            break;
                        case ItemColumnID_Location:
                            delta = l->location.compare(r->location);
                            break;
                        case ItemColumnID_ModelID:
                            delta = l->itemPartial.model_id - r->itemPartial.model_id;
                            break;
                        case ItemColumnID_Description:
                            delta = lms->description.compare(rms->description);
                            break;
                    }
                    if (delta != 0) return delta * sortDirection < 0;
                }
            }
            // fallback
            if (delta == 0) delta = l->character.compare(r->character);
            if (delta == 0) delta = l->location.compare(r->location);
            if (delta == 0) delta = l->bag_id - r->bag_id;
            if (delta == 0) delta = l->slot - r->slot;
            if (delta == 0 && l->account < r->account) delta = -1;
            return delta * sortDirection < 0;
        }
        // used for order inside MergeStack, i.e. for interaction and tooltip
        bool operator()(const std::shared_ptr<InventoryItem> l, const std::shared_ptr<InventoryItem> r) const
        {
            if (l->account != r->account) {
                // lowest item is the one that can be interacted with. Make sure it is one on this account if there is one
                if (l->account == currentAccount) return true;
                if (r->account == currentAccount) return false;
            }
            auto lms = std::make_shared<MergeStack>(l->account);
            lms->quantity = l->itemPartial.quantity;
            lms->i.insert(l);
            auto rms = std::make_shared<MergeStack>(r->account);
            rms->quantity = r->itemPartial.quantity;
            rms->i.insert(r);
            return this->operator()(lms, rms);
        }
    };
    struct ItemHash {
        std::size_t operator()(const std::shared_ptr<InventoryItem> i) const noexcept
        {
            std::size_t h1 = std::hash<std::wstring>{}(i->account);
            std::size_t h2 = std::hash<std::wstring>{}(i->character);
            std::size_t h3 = std::hash<uint32_t>{}(i->hero_id);
            std::size_t h4 = std::hash<uint32_t>{}(i->bag_id);
            std::size_t h5 = std::hash<uint32_t>{}(i->slot);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
        }
    };
    struct ItemEqual {
        bool operator()(const std::shared_ptr<InventoryItem> l, const std::shared_ptr<InventoryItem> r) const
        {
            return l->hero_id == r->hero_id && l->bag_id == r->bag_id && l->slot == r->slot && l->account == r->account && l->character == r->character;
        }
    };
    struct MergeStack {
        uint16_t quantity;
        std::wstring description;
        std::set<std::shared_ptr<InventoryItem>, ItemCompare> i;

        MergeStack(std::wstring account): quantity{}, description{}, i(ItemCompare{nullptr, account}) {}
    };
    struct QueueDescription {
        const std::shared_ptr<InventoryItem> i{};
        std::wstring description{};
    };

    class InventoryIni : public ToolboxIni {
    public:
        FILETIME last_change_time{};
        std::wstring account{};
        std::wstring iniID{}; // character name for character/hero inventories, email for xunlai chests
        InventoryIni(std::filesystem::path _location_on_disk) {
            location_on_disk = _location_on_disk;
        }
    };

    struct CharacterFreeSlots {
        std::wstring account{};
        std::wstring character{};
        std::wstring account_representing_character{}; // character name to tell which account a chest inventory belongs to without showing an email address
        uint32_t max_inventory{};
        uint32_t max_equipment{};
        uint32_t occupied_inventory{};
        uint32_t occupied_equipment{};
        bool anniversary_pane_active;
    };
    struct SlotCompare {
        ImGuiTableSortSpecs* sortSpecs{};
        bool operator()(const std::shared_ptr<CharacterFreeSlots> l, const std::shared_ptr<CharacterFreeSlots> r) const
        {
            int sortDirection = 1;
            int delta = 0;
            auto l_free_inventory = l->max_inventory - l->occupied_inventory;
            auto l_free_equipment = l->max_equipment - l->occupied_equipment;
            auto r_free_inventory = r->max_inventory - r->occupied_inventory;
            auto r_free_equipment = r->max_equipment - r->occupied_equipment;
            if (sortSpecs) {
                for (int n = 0; n < sortSpecs->SpecsCount; n++) {
                    const ImGuiTableColumnSortSpecs* sort_spec = &sortSpecs->Specs[n];
                    sortDirection = (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? 1 : -1;
                    delta = 0;
                    switch (sort_spec->ColumnUserID) {
                        case SlotColumnID_Character:
                            delta = l->character.compare(r->character);
                            break;
                        case SlotColumnID_Inventory:
                            delta = l_free_inventory - r_free_inventory;
                            break;
                        case SlotColumnID_InventorySize:
                            delta = l->max_inventory - r->max_inventory;
                            break;
                        case SlotColumnID_Equipment:
                            delta = l_free_equipment - r_free_equipment;
                            break;
                        case SlotColumnID_EquipmentSize:
                            delta = l->max_equipment - r->max_equipment;
                            break;
                    }
                    if (delta != 0) return delta * sortDirection < 0;
                }
            }
            // fallback
            if (delta == 0) delta = l->character.compare(r->character);
            if (delta == 0) delta = l->account.compare(r->account);
            return delta * sortDirection < 0;
        }
    };
    struct SlotHash {
        std::size_t operator()(const std::shared_ptr<CharacterFreeSlots> i) const noexcept
        {
            std::size_t h1 = std::hash<std::wstring>{}(i->account);
            std::size_t h2 = std::hash<std::wstring>{}(i->character);
            return h1 ^ (h2 << 1);
        }
    };
    struct SlotEqual {
        bool operator()(const std::shared_ptr<CharacterFreeSlots> l, const std::shared_ptr<CharacterFreeSlots> r) const
        {
            return l->account == r->account && l->character == r->character;
        }
    };

    AccountInventoryWindow()
    {
        show_menubutton = can_show_in_main_window;
    }

public:
    static AccountInventoryWindow& Instance()
    {
        static AccountInventoryWindow instance;
        return instance;
    }

    [[nodiscard]] const char* Name() const override { return "Account Inventory"; }
    [[nodiscard]] const char* Icon() const override { return ICON_FA_USERS; }

    void Initialize() override;
    void Terminate() override;
    void Update(float delta) override;

    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void DrawSettingsInternal() override;

    void LoadSettings(ToolboxIni* ini) override;
    void SaveSettings(ToolboxIni* ini) override;

    void AddItem(uint32_t item_id);
    void RemoveItem(uint32_t item_id);
    void LoadAllInventories();
    void PreMapLoad();
    void PostMapLoad();
    void OnMapLoadedDelayed();
    void OnPartyAddHero();
    void OnRerollPromptReply();
    void OnItemTooltip(std::shared_ptr<MergeStack> ms);

private:
    std::shared_ptr<InventoryIni> getIni(std::wstring iniID, std::wstring account);
    void LoadFromFiles(bool onlyForeign);
    void SaveToFiles();
    std::string KeyToString(std::shared_ptr<InventoryItem> i) const;
    void QueueDescriptionDecode(std::shared_ptr<InventoryItem> i);
    void StepReroll();
    void SaveHeroes();
    void RestoreHeroes();
    void SortSlots(ImGuiTableSortSpecs* sortSpecs);
    void SortInventory(ImGuiTableSortSpecs* sortSpecs);
    void OnInventoryItemClicked(std::shared_ptr<InventoryItem> i, bool move);
    void MoveItem();
    void ClearMissingItem(std::wstring account, std::wstring character, uint32_t hero_id, uint32_t bag_id, uint32_t slot);

    static std::wstring getIniID(std::wstring account, std::wstring character);
    static bool checkIniDirty(std::shared_ptr<InventoryIni> ini);

    static std::unordered_map<int, uint32_t> ChestModelIDToHeroID;
    static inline int ChestArmorInventorySlot = 2;
    static std::vector<std::string> HeroName;
    static std::vector<std::string> BagName;
    static std::vector<uint32_t> BagMaxSize;
    static std::vector<bool> BagCanHoldAnything;
    enum RerollStage {
        None,
        NextCharacter,
        WaitForCharacterLoad,
        DoSaveHeroes,
        DoneCharacterLoad,
        WaitForHeroLoad,
        DoneHeroLoad,
        DoRestoreHeroes,
        RerollToItem
    };

    std::unordered_set<std::shared_ptr<InventoryItem>, ItemHash, ItemEqual> inventory{};
    // On*SlotCleared send an item_id, but the information which bag and slot it was in
    // is already removed. In order to remove items from inventory without iterating,
    // we keep track of the item_id->InventoryItem mapping
    std::unordered_map<uint32_t, std::shared_ptr<InventoryItem>> inventoryLookup{};
    std::vector<std::shared_ptr<MergeStack>> inventorySorted{};
    std::unordered_map<std::filesystem::path, std::shared_ptr<InventoryIni>> iniByPath{};
    std::unordered_map<std::wstring, std::shared_ptr<InventoryIni>> iniByCharacter{};
    std::unordered_set<std::wstring> inventoryDirty{};
    std::unordered_set<std::shared_ptr<CharacterFreeSlots>, SlotHash, SlotEqual> freeSlots{};
    std::set<std::shared_ptr<CharacterFreeSlots>, SlotCompare> freeSlotsSorted{};

    bool needsSorting = true;

    std::mutex descriptionDecodeLock{};
    std::queue<std::shared_ptr<QueueDescription>> descriptionDecodeQueue;
    std::vector<GW::AvailableCharacterInfo> rerollCharQueue{};
    std::vector<uint32_t> rerollHeroQueue{};
    std::vector<uint32_t> cachedHeroes{};

    bool grid_view = false;
    bool merge_stacks = false;
    bool hide_other_accounts = false;
    bool hide_equipment = false;
    bool hide_equipment_pack = false;
    bool hide_hero_armor = false;
    bool hide_unclaimed_items = false;

    size_t filteredItemCount = 0;
    std::wstring lastCharacter{};
    std::set<std::wstring> lastAvailableChars{};

    static const size_t BUFFER_SIZE = 128;
    char nameFilterBuf[BUFFER_SIZE]{};
    char locationFilterBuf[BUFFER_SIZE]{};
    char modelIDFilterBuf[BUFFER_SIZE]{};
    char itemFilterBuf[BUFFER_SIZE]{};

    RerollStage rerollStage = RerollStage::None;
    clock_t saveHeroTimer{};
    const clock_t saveHeroTimeout = 500;
    clock_t mapLoadedDelayedTimer{};
    const clock_t mapLoadedDelayedTimeout = 400;
    bool mapLoadedDelayedTrigger = false;
    std::shared_ptr<InventoryItem> itemToMove{};

    GW::HookEntry OnUIMessage_HookEntry{};
};
