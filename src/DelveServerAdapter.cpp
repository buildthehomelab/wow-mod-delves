#include "DelveServerAdapter.h"
#include "Config.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectScript.h"
#include "InstanceMapScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldSession.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace Evermore::Delves
{
    namespace
    {
        constexpr uint32 PilotMapId = 890; // provisional; production ID reservation is still required

        uint32 CharacterId(Player const* player)
        {
            uint64 const value = player->GetGUID().GetRawValue();
            return value <= std::numeric_limits<uint32>::max() ? static_cast<uint32>(value) : 0;
        }

        InstanceRun* FindRun(Player* player)
        {
            if (!player || !player->IsInWorld() || player->GetMapId() != PilotMapId)
                return nullptr;
            return dynamic_cast<InstanceRun*>(player->GetInstanceScript());
        }
    }

    InstanceRun::InstanceRun(Map* map, Definition definition)
        : InstanceScript(map), _definition(std::move(definition)), _journal(map->GetInstanceId()) { }

    void InstanceRun::Initialize()
    {
        // Core omits Load when its own asynchronous save was empty. The journal
        // may already contain a confirmed checkpoint after a crash in that gap.
        Load(nullptr);
    }

    std::optional<Run> InstanceRun::CurrentRun() const
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        return _run ? std::optional<Run>(_run->Current()) : std::nullopt;
    }

    bool InstanceRun::Blocked() const
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        return _invalidSave || (_run && _run->Blocked());
    }

    bool InstanceRun::IsRealMemberPresent(Player const* player) const
    {
        return player && player->IsInWorld() && player->GetMap() == instance &&
            player->GetSession() && !player->GetSession()->IsHeadless() && CharacterId(player);
    }

    bool InstanceRun::Begin(std::uint32_t tier, std::vector<Member> const& members)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (_invalidSave || _run || !instance->GetInstanceId() || !instance->IsDungeon() ||
            instance->GetId() != _definition.map)
            return false;
        for (auto const& member : members)
        {
            bool present = false;
            for (auto const& reference : instance->GetPlayers())
            {
                Player* player = reference.GetSource();
                if (IsRealMemberPresent(player) && CharacterId(player) == member.guid &&
                    player->GetSession()->GetAccountId() == member.account && player->GetLevel() == member.level)
                {
                    present = true;
                    break;
                }
            }
            if (!present)
                return false;
        }
        try
        {
            // An existing checkpoint belongs to the existing run, never a reset.
            if (!_journal.Read().empty())
                return false;
            auto const allocatedRunId = _journal.Reserve();
            if (!allocatedRunId)
                return false;
            Run initial(allocatedRunId, instance->GetInstanceId(), tier, _definition, members);
            if (!_journal.Replace("", initial.Save()))
            {
                _invalidSave = true;
                return false;
            }
            _run = std::make_unique<ConfirmedRun>(std::move(initial), [this](auto const& oldState, auto const& newState)
            {
                return _journal.Replace(oldState, newState);
            });
        }
        catch (std::invalid_argument const&)
        {
            return false;
        }
        SaveToDB();
        return true;
    }

    bool InstanceRun::Accept(Event const& event)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (!_run || _invalidSave || !_run->Credit(event))
            return false;
        // The module journal is authoritative; core's asynchronous instance save
        // is a compatibility mirror and cannot roll a recovered run backwards.
        SaveToDB();
        return true;
    }

    bool InstanceRun::Abandon()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (!_run || _invalidSave || !_run->Abandon())
            return false;
        SaveToDB();
        return true;
    }

    bool InstanceRun::CreditKill(Player* player, Creature* creature)
    {
        if (!IsRealMemberPresent(player) || !creature || creature->GetMap() != instance ||
            creature->IsAlive() || !creature->GetSpawnId())
            return false;
        return Accept({instance->GetId(), instance->GetInstanceId(), CharacterId(player), EventKind::Kill,
            creature->GetEntry(), static_cast<uint64>(creature->GetSpawnId()) << 1});
    }

    bool InstanceRun::CreditInteraction(Player* player, GameObject* object)
    {
        if (!IsRealMemberPresent(player) || !player->IsAlive() || !object || !object->IsInWorld() ||
            object->GetMap() != instance || !object->GetSpawnId() ||
            !player->IsWithinDistInMap(object, INTERACTION_DISTANCE))
            return false;
        return Accept({instance->GetId(), instance->GetInstanceId(), CharacterId(player), EventKind::Interact,
            object->GetEntry(), (static_cast<uint64>(object->GetSpawnId()) << 1) | 1});
    }

    std::string InstanceRun::GetSaveData()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        return _run ? _run->Current().Save() : _invalidSaveData;
    }

    void InstanceRun::Load(char const* data)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        // Core's compatibility mirror may be older, newer or corrupt. Once the
        // authoritative journal was loaded, no mirror may replace it.
        if (_run || _invalidSave)
            return;
        try
        {
            auto const checkpoint = _journal.Read();
            if (checkpoint.empty())
            {
                if (!data || !*data)
                    return;
                throw std::invalid_argument("core save has no authoritative delve journal");
            }
            auto recovered = Run::Load(checkpoint, _definition);
            // The save carries its original instance ID. A cross-instance save
            // cannot be transplanted and then earn credit in the new instance.
            if (recovered.InstanceId() != instance->GetInstanceId())
                throw std::invalid_argument("delve instance identity mismatch");
            _run = std::make_unique<ConfirmedRun>(std::move(recovered), [this](auto const& oldState, auto const& newState)
            {
                return _journal.Replace(oldState, newState);
            });
        }
        catch (std::invalid_argument const& error)
        {
            _invalidSave = true;
            _invalidSaveData = data ? data : "";
            LOG_ERROR("module.delves", "Refusing invalid delve save for instance {}: {}",
                instance->GetInstanceId(), error.what());
        }
    }

    class DelveInstanceScript : public InstanceMapScript
    {
    public:
        explicit DelveInstanceScript(Definition definition)
            : InstanceMapScript("instance_evermore_delves", PilotMapId), _definition(std::move(definition)) { }

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new InstanceRun(map, _definition);
        }
    private:
        Definition _definition;
    };

    class DelvePlayerScript : public PlayerScript
    {
    public:
        DelvePlayerScript() : PlayerScript("evermore_delve_credit") { }
        void OnPlayerCreatureKillCredit(Player* player, Creature* creature) override
        {
            if (auto* run = FindRun(player))
                run->CreditKill(player, creature);
        }
    };

    class DelveObjectiveScript : public GameObjectScript
    {
    public:
        DelveObjectiveScript() : GameObjectScript("go_evermore_delve_objective") { }
        bool OnGossipHello(Player* player, GameObject* object) override
        {
            if (auto* run = FindRun(player))
                run->CreditInteraction(player, object);
            return true;
        }
    };
}

void AddEvermoreDelveTestControls(uint32 revision);

void AddEvermoreDelveScripts()
{
    using namespace Evermore::Delves;
    // Enablement is intentionally startup-only. A reload may not replace the
    // definition of a run that is already saved or active.
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Enable", false))
        return;
    if (!CharacterDatabase.Query("SELECT COUNT(*) FROM mod_delves_run WHERE 1 = 0") ||
        !CharacterDatabase.Query("SELECT COUNT(*) FROM mod_delves_visit WHERE 1 = 0"))
    {
        LOG_ERROR("module.delves", "Delve prototype not registered: required development journal schema is missing");
        return;
    }
    auto const* map = sMapStore.LookupEntry(PilotMapId);
    uint32 const objective = sConfigMgr->GetOption<uint32>("Delves.Prototype.ObjectiveEntry", 0);
    uint32 const boss = sConfigMgr->GetOption<uint32>("Delves.Prototype.BossEntry", 0);
    uint32 const count = sConfigMgr->GetOption<uint32>("Delves.Prototype.ObjectiveCount", 0);
    uint32 const revision = sConfigMgr->GetOption<uint32>("Delves.Prototype.ContentRevision", 0);
    if (!map || !map->IsNonRaidDungeon() || !objective || !boss || !count || count >= Run::MaxCredits || !revision)
    {
        LOG_ERROR("module.delves", "Delve prototype not registered: map/catalog/revision is not configured");
        return;
    }
    Definition definition{revision, PilotMapId, {{EventKind::Interact, objective, count}, {EventKind::Kill, boss, 1}}};
    new DelveInstanceScript(std::move(definition));
    new DelvePlayerScript();
    new DelveObjectiveScript();
    AddEvermoreDelveTestControls(revision);
}
