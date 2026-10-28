#include "DelveServerAdapter.h"
#include "DelveScaling.h"
#include "DelveRewardDelivery.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Map.h"
#include "MapMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldSession.h"
#include <limits>
#include <optional>

using namespace Acore::ChatCommands;

namespace Evermore::Delves
{
    namespace
    {
        constexpr uint32 TestMap = 890;
        bool controlsReady = false;
        uint32 contentRevision = 0;
        uint32 minimumLevel = 20;
        uint32 maximumLevel = 60;
        uint32 maximumTier = 3;
        WorldLocation entrance;

        struct Visit
        {
            WorldLocation origin;
            uint32 tier;
            uint32 level;
            uint32 revision;
            bool returning;
            uint32 progression;
        };

        uint32 CharacterId(Player const* player)
        {
            uint64 const raw = player->GetGUID().GetRawValue();
            return raw <= std::numeric_limits<uint32>::max() ? static_cast<uint32>(raw) : 0;
        }

        bool IsHuman(Player const* player)
        {
            return player && player->IsInWorld() && player->GetSession() &&
                !player->GetSession()->IsHeadless() && player->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR && CharacterId(player);
        }

        std::optional<Visit> ReadVisit(Player const* player)
        {
            auto result = CharacterDatabase.Query("SELECT source_map, source_x, source_y, source_z, source_o, "
                "tier, level, revision, returning, progression FROM mod_delves_visit WHERE guid = {} AND account = {}",
                CharacterId(player), player->GetSession()->GetAccountId());
            if (!result)
                return std::nullopt;
            Field* row = result->Fetch();
            return Visit{WorldLocation(row[0].Get<uint32>(), row[1].Get<float>(), row[2].Get<float>(),
                row[3].Get<float>(), row[4].Get<float>()), row[5].Get<uint32>(), row[6].Get<uint32>(),
                row[7].Get<uint32>(), row[8].Get<uint8>() != 0, row[9].Get<uint32>()};
        }

        InstanceRun* FindInstance(Player* player)
        {
            return player->GetMapId() == TestMap ? dynamic_cast<InstanceRun*>(player->GetInstanceScript()) : nullptr;
        }

        bool IsOwner(std::optional<Run> const& run, Player* player)
        {
            return run && run->Members().size() == 1 && run->Members()[0].guid == CharacterId(player) &&
                run->Members()[0].account == player->GetSession()->GetAccountId();
        }

        void OnArrival(Player* player)
        {
            if (!controlsReady || !IsHuman(player))
                return;
            auto visit = ReadVisit(player);
            if (!visit)
                return;
            if (visit->returning)
            {
                if (player->GetMapId() == visit->origin.GetMapId() && player->GetDistance(visit->origin) < 10.0f)
                    CharacterDatabase.DirectExecute("DELETE FROM mod_delves_visit WHERE guid = {} AND account = {} AND returning = 1",
                        CharacterId(player), player->GetSession()->GetAccountId());
                return;
            }
            auto* instance = FindInstance(player);
            if (!instance)
                return;
            ChatHandler chat(player->GetSession());
            if (visit->revision != contentRevision || visit->level < minimumLevel || visit->level > maximumLevel || visit->tier < 1 || visit->tier > maximumTier || player->GetGroup() ||
                player->GetLevel() != visit->level || player->GetMap()->GetPlayers().getSize() != 1 ||
                (ScalingEnabled() && (ReadDelveProgression(player) != visit->progression ||
                    !MakeScaleProfile(visit->level, visit->progression, visit->tier))))
            {
                chat.SendSysMessage("Delve test admission refused. Use .delve leave to return safely.");
                return;
            }
            if (!instance->CurrentRun())
            {
                if (!instance->Begin(visit->tier, {{CharacterId(player), player->GetSession()->GetAccountId(), visit->level, ScalingEnabled() ? visit->progression : 0}}))
                {
                    chat.SendSysMessage("Delve checkpoint could not be confirmed. No run started; use .delve leave.");
                    return;
                }
            }
            if (!IsOwner(instance->CurrentRun(), player) || instance->Blocked())
            {
                chat.SendSysMessage("This instance cannot admit this test character. Use .delve leave.");
                return;
            }
            auto const admitted = instance->CurrentRun();
            if (ScalingEnabled() && (admitted->Tier() != visit->tier || admitted->Members()[0].level != visit->level ||
                admitted->Members()[0].progression != visit->progression))
            {
                chat.SendSysMessage("Saved delve scaling inputs differ from this visit. Use .delve leave.");
                return;
            }
            instance->RefreshEncounterScale();
            instance->EnsureCompanion(player);
            chat.SendSysMessage("Delve test checkpoint loaded. Use .delve status for objectives and .delve leave to return.");
        }
    }

    class DelveTestCommands : public CommandScript
    {
    public:
        DelveTestCommands() : CommandScript("evermore_delve_test_commands") { }
        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable subcommands =
            {
                { "enter", Enter, SEC_ADMINISTRATOR, Console::No },
                { "leave", Leave, SEC_ADMINISTRATOR, Console::No },
                { "status", Status, SEC_ADMINISTRATOR, Console::No },
            };
            static ChatCommandTable commands = {{ "delve", subcommands }};
            return commands;
        }

        static bool Enter(ChatHandler* handler, uint32 tier)
        {
            if (!controlsReady)
            {
                handler->SendSysMessage("Delve test controls are disabled or startup validation failed.");
                return true;
            }
            Player* player = handler->GetSession()->GetPlayer();
            if (!IsHuman(player) || tier < 1 || tier > maximumTier || !player->IsAlive() || player->IsInCombat() ||
                player->GetGroup() || player->IsInFlight() || player->GetTransport() || player->GetVehicle() ||
                player->IsBeingTeleported() || (player->GetMapId() != 0 && player->GetMapId() != 1 && player->GetMapId() != 530 && player->GetMapId() != 571) ||
                player->GetLevel() < minimumLevel || player->GetLevel() > maximumLevel)
            {
                handler->PSendSysMessage("Test entry requires a solo, living level {}-{} administrator on a continent, out of combat and transport. Tier must be 1-{}.",
                    minimumLevel, maximumLevel, maximumTier);
                return true;
            }
            if (!RewardsAdmissionAllowed(player))
            {
                handler->SendSysMessage("Entry refused: the coin wallet is unavailable or this character is Self-Found. "
                    "Self-Found personal rewards are not supported yet. No return visit was created.");
                return true;
            }
            auto progression = ReadDelveProgression(player);
            if (ScalingEnabled() && (!progression || !MakeScaleProfile(player->GetLevel(), *progression, tier)))
            {
                handler->SendSysMessage("Your level/progression snapshot is not eligible for this delve test.");
                return true;
            }
            OnArrival(player); // Clears an acknowledged same-map return after its teleport has finished.
            if (ReadVisit(player))
            {
                handler->SendSysMessage("A saved delve visit already exists. Use .delve leave before another test.");
                return true;
            }
            CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_delves_visit "
                "(guid, account, source_map, source_x, source_y, source_z, source_o, tier, level, revision, progression) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})", CharacterId(player),
                player->GetSession()->GetAccountId(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), player->GetOrientation(), tier, player->GetLevel(), contentRevision, progression.value_or(4294967295u));
            auto visit = ReadVisit(player);
            if (!visit || visit->returning || visit->tier != tier || visit->level != player->GetLevel() ||
                (ScalingEnabled() && visit->progression != progression) ||
                visit->revision != contentRevision || visit->origin.GetMapId() != player->GetMapId() ||
                player->GetDistance(visit->origin) >= 1.0f)
            {
                handler->SendSysMessage("Return location could not be confirmed. Entry refused.");
                return true;
            }
            // Far teleport completes later. Only the arrival hook may start the
            // run, after checking the real destination instance and occupant.
            if (!player->TeleportTo(entrance))
                handler->SendSysMessage("Teleport was refused. Your return location is retained; use .delve leave.");
            return true;
        }

        static bool Leave(ChatHandler* handler)
        {
            if (!controlsReady)
            {
                handler->SendSysMessage("Delve test controls are disabled or startup validation failed.");
                return true;
            }
            Player* player = handler->GetSession()->GetPlayer();
            if (!IsHuman(player) || player->IsBeingTeleported())
                return true;
            auto visit = ReadVisit(player);
            if (!visit || !MapMgr::IsValidMapCoord(visit->origin))
            {
                handler->SendSysMessage("No valid saved delve return location exists.");
                return true;
            }
            if (auto* instance = FindInstance(player))
                if (IsOwner(instance->CurrentRun(), player) && instance->CurrentRun()->Status() == RunStatus::Active)
                    instance->Abandon(); // A failed checkpoint must never prevent escape.
            CharacterDatabase.DirectExecute("UPDATE mod_delves_visit SET returning = 1 WHERE guid = {} AND account = {}",
                CharacterId(player), player->GetSession()->GetAccountId());
            auto marked = ReadVisit(player);
            if (!marked || !marked->returning)
            {
                handler->SendSysMessage("Return marker could not be confirmed. Saved location retained; retry .delve leave.");
                return true;
            }
            if (!player->TeleportTo(visit->origin))
                handler->SendSysMessage("Return teleport refused. Saved location retained; retry .delve leave.");
            // Same-map near teleports have no map-change hook. The marker is
            // cleaned on login or before the next entry once arrival is proven.
            return true;
        }

        static bool Status(ChatHandler* handler)
        {
            if (!controlsReady)
            {
                handler->SendSysMessage("Delve test controls are disabled or startup validation failed.");
                return true;
            }
            Player* player = handler->GetSession()->GetPlayer();
            auto* instance = IsHuman(player) ? FindInstance(player) : nullptr;
            if (!instance || !IsOwner(instance->CurrentRun(), player))
            {
                handler->SendSysMessage("No admitted test run in this instance.");
                return true;
            }
            auto const run = instance->CurrentRun();
            char const* state = run->Status() == RunStatus::Active ? "active" :
                run->Status() == RunStatus::Complete ? "complete" : "abandoned";
            handler->PSendSysMessage("Delve run {}: tier {}, {}, stage {}, progress {}, checkpoint blocked {}.",
                run->Id(), run->Tier(), state, run->Stage(), run->Progress(), instance->Blocked());
            handler->SendSysMessage(DescribeReward(player, *run));
            return true;
        }
    };

    class DelveTestArrivals : public PlayerScript
    {
    public:
        DelveTestArrivals() : PlayerScript("evermore_delve_test_arrivals", {PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_LOGIN}) { }
        void OnPlayerMapChanged(Player* player) override { OnArrival(player); }
        void OnPlayerLogin(Player* player) override { OnArrival(player); }
    };
}

void InitializeEvermoreDelveTestControls(uint32 revision)
{
    using namespace Evermore::Delves;
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.TestCommands.Enable", false) ||
        !sConfigMgr->GetOption<bool>("Delves.Prototype.TestCommands.EntryConfigured", false))
        return;
    minimumLevel = sConfigMgr->GetOption<uint32>("Delves.Prototype.TestCommands.MinLevel", 20);
    maximumLevel = sConfigMgr->GetOption<uint32>("Delves.Prototype.TestCommands.MaxLevel", 60);
    maximumTier = sConfigMgr->GetOption<uint32>("Delves.Prototype.TestCommands.MaxTier", 3);
    if (minimumLevel < (ScalingEnabled() ? 10u : 20u) || maximumLevel > (ScalingEnabled() ? 80u : 60u) || minimumLevel > maximumLevel || !maximumTier || maximumTier > 3)
        return;
    entrance = WorldLocation(890,
        sConfigMgr->GetOption<float>("Delves.Prototype.TestCommands.EntryX", 0.0f),
        sConfigMgr->GetOption<float>("Delves.Prototype.TestCommands.EntryY", 0.0f),
        sConfigMgr->GetOption<float>("Delves.Prototype.TestCommands.EntryZ", 0.0f),
        sConfigMgr->GetOption<float>("Delves.Prototype.TestCommands.EntryO", 0.0f));
    if (!MapMgr::IsValidMapCoord(entrance))
        return;
    contentRevision = revision;
    controlsReady = true;
}

void AddEvermoreDelveTestControls()
{
    using namespace Evermore::Delves;
    new DelveTestCommands();
    new DelveTestArrivals();
}
