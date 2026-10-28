#include "DelveServerAdapter.h"
#include "DelveCompanion.h"
#include "DelveScaling.h"
#include "Config.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectScript.h"
#include "InstanceMapScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldSession.h"
#include "WorldScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace Evermore::Delves
{
    void InitializeDelveRuntime();
    namespace
    {
        std::optional<Definition> pilotDefinition;
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

    bool InstanceRun::IsSnapshotMemberPresent(Player const* player) const
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (!IsRealMemberPresent(player) || !_run || Blocked())
            return false;
        for (auto const& member : _run->Current().Members())
            if (member.guid == CharacterId(player) && member.account == player->GetSession()->GetAccountId() &&
                member.level == player->GetLevel() &&
                (!ScalingEnabled() || ReadDelveProgression(player) == member.progression))
                return true;
        return false;
    }

    bool InstanceRun::Begin(std::uint32_t tier, std::vector<Member> const& members)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (_invalidSave || _run || !instance->GetInstanceId() || !instance->IsDungeon() ||
            instance->GetId() != _definition.map)
            return false;
        for (auto const& member : members)
        {
            if (ScalingEnabled() && !MakeScaleProfile(member.level, member.progression, tier))
                return false;
            bool present = false;
            for (auto const& reference : instance->GetPlayers())
            {
                Player* player = reference.GetSource();
                if (IsRealMemberPresent(player) && CharacterId(player) == member.guid &&
                    player->GetSession()->GetAccountId() == member.account && player->GetLevel() == member.level &&
                    (!ScalingEnabled() || ReadDelveProgression(player) == member.progression))
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

    void InstanceRun::ScaleEncounterCreature(Creature* creature)
    {
        if (!ScalingEnabled() || !_run || Blocked() || _run->Current().Members().size() != 1 ||
            _run->Current().Status() != RunStatus::Active || !creature || !creature->IsAlive() ||
            creature->IsInCombat() || !creature->GetSpawnId() || _definition.objectives.empty())
            return;
        auto const& boss = _definition.objectives.back();
        if (boss.kind == EventKind::Kill && boss.entry == creature->GetEntry() &&
            creature->GetCreatureTemplate()->ScriptID == sObjectMgr->GetScriptId("boss_evermore_abenar"))
            ApplyDelveScale(creature, _run->Current().Members()[0], _run->Current().Tier());
    }

    void InstanceRun::OnCreatureCreate(Creature* creature)
    {
        InstanceScript::OnCreatureCreate(creature);
        std::lock_guard<std::recursive_mutex> guard(_lock);
        ScaleEncounterCreature(creature);
    }

    void InstanceRun::RefreshEncounterScale()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        for (auto const& entry : instance->GetCreatureBySpawnIdStore())
            ScaleEncounterCreature(entry.second);
    }

    void InstanceRun::EnsureCompanion(Player* player)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (!IsRealMemberPresent(player) || !_run || Blocked() || _run->Current().Status() != RunStatus::Active ||
            _run->Current().Members().size() != 1 ||
            _run->Current().Members()[0].guid != CharacterId(player))
            return;
        if (Creature* existing = instance->GetCreature(_companionGuid))
        {
            if (existing->IsAlive())
                return;
            existing->DespawnOrUnsummon();
        }
        if (Creature* companion = SummonCompanion(player, _run->Current().Id()))
            _companionGuid = companion->GetGUID();
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
        if (!IsSnapshotMemberPresent(player) || !creature || creature->GetMap() != instance ||
            creature->IsAlive() || !creature->GetSpawnId())
            return false;
        return Accept({instance->GetId(), instance->GetInstanceId(), CharacterId(player), EventKind::Kill,
            creature->GetEntry(), static_cast<uint64>(creature->GetSpawnId()) << 1});
    }

    bool InstanceRun::CreditInteraction(Player* player, GameObject* object)
    {
        if (!IsSnapshotMemberPresent(player) || !player->IsAlive() || !object || !object->IsInWorld() ||
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
        DelveInstanceScript() : InstanceMapScript("instance_evermore_delves", PilotMapId) { }

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return pilotDefinition ? new InstanceRun(map, *pilotDefinition) : nullptr;
        }
    };

    class DelveStartup : public WorldScript
    {
    public:
        DelveStartup() : WorldScript("evermore_delve_startup", {WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED}) { }
        void OnBeforeWorldInitialized() override { InitializeDelveRuntime(); }
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

void AddEvermoreDelveTestControls();
void InitializeEvermoreDelveTestControls(uint32 revision);
bool InitializeEvermoreDelveEncounter();
void AddEvermoreDelveEncounterScripts();

void Evermore::Delves::InitializeDelveRuntime()
{
    using namespace Evermore::Delves;
    // Enablement is intentionally startup-only. A reload may not replace the
    // definition of a run that is already saved or active.
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Enable", false))
        return;
    if (!CharacterDatabase.Query("SELECT COUNT(*) FROM mod_delves_run WHERE 1 = 0") ||
        !CharacterDatabase.Query("SELECT COUNT(progression) FROM mod_delves_visit WHERE 1 = 0"))
    {
        LOG_ERROR("module.delves", "Delve prototype not registered: required development journal schema is missing");
        return;
    }
    if (!InitializeDelveScaling())
        return;
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
    auto const* instanceTemplate = sObjectMgr->GetInstanceTemplate(PilotMapId);
    auto const* objectiveTemplate = sObjectMgr->GetGameObjectTemplate(objective);
    auto const* bossTemplate = sObjectMgr->GetCreatureTemplate(boss);
    if (!instanceTemplate || !instanceTemplate->ScriptId ||
        instanceTemplate->ScriptId != sObjectMgr->GetScriptId("instance_evermore_delves") ||
        !objectiveTemplate || !objectiveTemplate->ScriptId || objectiveTemplate->type != GAMEOBJECT_TYPE_GOOBER ||
        objectiveTemplate->ScriptId != sObjectMgr->GetScriptId("go_evermore_delve_objective") ||
        !bossTemplate || !bossTemplate->ScriptID ||
        bossTemplate->ScriptID != sObjectMgr->GetScriptId("boss_evermore_abenar"))
    {
        LOG_ERROR("module.delves", "Delve startup refused: dedicated world script bindings missing or mismatched");
        return;
    }
    if (!InitializeEvermoreDelveCompanion() || !InitializeEvermoreDelveEncounter())
        return;
    pilotDefinition = Definition{revision, PilotMapId, {{EventKind::Interact, objective, count}, {EventKind::Kill, boss, 1}}};
    InitializeEvermoreDelveTestControls(revision);
    LOG_INFO("module.delves", "Delve prototype initialized after world data; revision {}", revision);
}

void AddEvermoreDelveScripts()
{
    using namespace Evermore::Delves;
    // ScriptMgr registers modules before StartDB. Register inert scripts only;
    // database/DBC/template validation runs in the loaded-world startup hook.
    new DelveStartup();
    new DelveInstanceScript();
    new DelvePlayerScript();
    new DelveObjectiveScript();
    AddEvermoreDelveTestControls();
    AddEvermoreDelveCompanionScripts();
    AddEvermoreDelveEncounterScripts();
}
