#include "stdafx.h"

#include <GWCA/Context/WorldContext.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Packets/StoC.h>

#include <Modules/FavorTrackerModule.h>
#include <ImGuiAddons.h>
#include <Timer.h>
#include <Utils/ToolboxUtils.h>

#define LOAD_BOOL(var) var = ini->GetBoolValue(Name(), #var, var);
#define SAVE_BOOL(var) ini->SetBoolValue(Name(), #var, var);

namespace {
    // --- Settings ---
    bool enabled = false;
    int poll_interval_seconds = 60;

    // --- State ---
    uint32_t favor_minutes = 0;
    bool favor_active = false;
    clock_t last_poll_time = 0;
    clock_t suppress_until = 0; // suppress favor chat messages until this time

    // --- Hooks ---
    GW::HookEntry OnMessageServer_Entry;

    // Extract the numeric value from an encoded message that ends with 0x101 0x100+value
    // Returns 0 if pattern not found
    uint32_t ExtractEncodedValue(const wchar_t* msg)
    {
        if (!msg) return 0;
        for (size_t i = 0; msg[i]; i++) {
            if (msg[i] == 0x101 && msg[i + 1] >= 0x100) {
                return msg[i + 1] - 0x100;
            }
        }
        return 0;
    }

    // Parse a favor message from the message core buffer
    // Returns true if the message was a favor message we handled
    bool ParseFavorMessage(const wchar_t* msg)
    {
        if (!msg || !*msg) return false;

        // /favor command response: 0x8102 0x223F ... 0x101 0x100+minutes
        // "x minutes of favor remaining"
        if (msg[0] == 0x8102 && msg[1] == 0x223F) {
            favor_minutes = ExtractEncodedValue(msg);
            favor_active = favor_minutes > 0;
            return true;
        }

        if (msg[0] == 0x8101) {
            switch (msg[1]) {
                // Broadcast: "x minutes of favor of the gods remaining"
                // 0x8101 0x7B91 0xC686 0xE490 0x6922 0x101 0x100+value
                case 0x7B91:
                    favor_minutes = ExtractEncodedValue(msg);
                    favor_active = favor_minutes > 0;
                    return true;

                // Broadcast: "x more achievements must be performed to earn favor"
                // 0x8101 0x7B92 0x8B0A 0x8DB5 0x5135 0x101 0x100+value
                case 0x7B92:
                    favor_minutes = 0;
                    favor_active = false;
                    return true;
            }
        }

        if (msg[0] == 0x8102) {
            switch (msg[1]) {
                // "The gods have blessed the world with their favor"
                case 0x23E3:
                    favor_active = true;
                    // We don't know exact minutes from this message, query it
                    return true;

                // "The world no longer has the favor of the gods"
                case 0x23E4:
                    favor_minutes = 0;
                    favor_active = false;
                    return true;
            }
        }

        return false;
    }

    void OnMessageServer(GW::HookStatus* status, GW::Packet::StoC::MessageServer*)
    {
        const wchar_t* msg = ToolboxUtils::GetMessageCore();
        if (ParseFavorMessage(msg) && suppress_until && TIMER_INIT() <= suppress_until) {
            status->blocked = true;
        }
    }

} // namespace

void FavorTrackerModule::Initialize()
{
    ToolboxModule::Initialize();

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::MessageServer>(&OnMessageServer_Entry, OnMessageServer);
}

void FavorTrackerModule::Update(float)
{
    if (!enabled) return;
    if (!GW::Map::GetIsMapLoaded()) return;

    if (TIMER_DIFF(last_poll_time) >= poll_interval_seconds * 1000) {
        last_poll_time = TIMER_INIT();
        suppress_until = TIMER_INIT() + 3000; // suppress favor chat for 3 seconds
        GW::Chat::SendChat('/', L"favor");
    }
}

void FavorTrackerModule::SignalTerminate()
{
    ToolboxModule::SignalTerminate();

    GW::StoC::RemoveCallback<GW::Packet::StoC::MessageServer>(&OnMessageServer_Entry);
}

uint32_t FavorTrackerModule::GetFavorMinutes()
{
    return favor_minutes;
}

bool FavorTrackerModule::HasFavor()
{
    return favor_active;
}

void FavorTrackerModule::LoadSettings(ToolboxIni* ini)
{
    ToolboxModule::LoadSettings(ini);
    LOAD_BOOL(enabled);
    poll_interval_seconds = ini->GetLongValue(Name(), "poll_interval_seconds", poll_interval_seconds);
}

void FavorTrackerModule::SaveSettings(ToolboxIni* ini)
{
    ToolboxModule::SaveSettings(ini);
    SAVE_BOOL(enabled);
    ini->SetLongValue(Name(), "poll_interval_seconds", poll_interval_seconds);
}

void FavorTrackerModule::DrawSettingsInternal()
{
    ImGui::Checkbox("Enable Favor Tracking", &enabled);
    ImGui::ShowHelp("Periodically runs /favor to check Favor of the Gods status. Automated queries are hidden from chat.");

    ImGui::InputInt("Poll Interval (seconds)", &poll_interval_seconds);
    if (poll_interval_seconds < 10) poll_interval_seconds = 10;

    ImGui::Separator();
    ImGui::Text("Favor Status: %s", favor_active ? "Active" : "Inactive");
    if (favor_active) {
        ImGui::Text("Minutes Remaining: %u", favor_minutes);
    }

    if (ImGui::Button("Check Now")) {
        GW::Chat::SendChat('/', L"favor");
    }
}
