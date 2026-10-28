#include "DelveCompanion.h"
#include "DelveServerAdapter.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "EventMap.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <array>
#include <cmath>

namespace Evermore::Delves
{
    namespace
    {
        uint32 companionEntry = 0;
        uint32 potionEntry = 0;
        uint32 healSpell = 0;
        uint32 healPercent = 0;
        enum Events : uint32 { CheckOwner = 1, ThrowPotions = 2, CheckPickup = 3 };
        enum Guids : int32 { OwnerGuid = 1, HealerGuid = 2 };

        bool OwnsRun(Player* owner, WorldObject* actor, uint64 runId)
        {
            if (!owner || !owner->IsInWorld() || !owner->GetSession() || owner->GetSession()->IsHeadless() ||
                owner->GetMap() != actor->GetMap() || !owner->InSamePhase(actor) || owner->GetGroup())
                return false;
            auto* instance = dynamic_cast<InstanceRun*>(owner->GetInstanceScript());
            auto run = instance ? instance->CurrentRun() : std::nullopt;
            return run && !instance->Blocked() && run->Id() == runId && run->Status() == RunStatus::Active &&
                run->Members().size() == 1 && run->Members()[0].guid == owner->GetGUID().GetRawValue() &&
                run->Members()[0].account == owner->GetSession()->GetAccountId();
        }

        class PotionAI : public GameObjectAI
        {
        public:
            explicit PotionAI(GameObject* object) : GameObjectAI(object) { events.ScheduleEvent(CheckPickup, Milliseconds(200)); }
            void SetGUID(ObjectGuid const& guid, int32 id) override
            {
                if (id == OwnerGuid)
                {
                    ownerGuid = guid;
                    if (Player* owner = ObjectAccessor::GetPlayer(*me, guid))
                        if (auto* instance = dynamic_cast<InstanceRun*>(owner->GetInstanceScript()))
                            if (auto run = instance->CurrentRun())
                                runId = run->Id();
                }
                else if (id == HealerGuid)
                    healerGuid = guid;
            }
            void UpdateAI(uint32 diff) override
            {
                events.Update(diff);
                if (events.ExecuteEvent() != CheckPickup || used)
                    return;
                Player* owner = ObjectAccessor::GetPlayer(*me, ownerGuid);
                Creature* healer = me->GetMap()->GetCreature(healerGuid);
                if (!OwnsRun(owner, me, runId) || !healer || !healer->IsAlive() || !owner->InSamePhase(healer))
                {
                    used = true;
                    me->Delete();
                    return;
                }
                if (owner->IsAlive() && owner->GetHealth() < owner->GetMaxHealth() &&
                    owner->IsWithinDistInMap(me, 1.5f) && owner->IsWithinLOSInMap(me))
                {
                    used = true; // Consume before callbacks; neither clicks nor repeated updates may heal twice.
                    uint32 const amount = static_cast<uint32>(static_cast<uint64>(owner->GetMaxHealth()) * healPercent / 100);
                    auto const* spell = sSpellMgr->GetSpellInfo(healSpell);
                    if (spell)
                    {
                        HealInfo healing(healer, owner, amount, spell, spell->GetSchoolMask());
                        healer->HealBySpell(healing);
                    }
                    me->Delete();
                    return;
                }
                events.ScheduleEvent(CheckPickup, Milliseconds(200));
            }
        private:
            EventMap events;
            ObjectGuid ownerGuid;
            ObjectGuid healerGuid;
            uint64 runId = 0;
            bool used = false;
        };

        class CompanionAI : public ScriptedAI
        {
        public:
            explicit CompanionAI(Creature* creature) : ScriptedAI(creature) { }
            void Bind(Player* owner, uint64 run)
            {
                ownerGuid = owner->GetGUID();
                runId = run;
                me->SetReactState(REACT_PASSIVE);
                me->GetMotionMaster()->MoveFollow(owner, 2.5f, 1.0f);
                events.Reset();
                events.ScheduleEvent(CheckOwner, Milliseconds(1000));
                wasInCombat = false;
            }
            void AttackStart(Unit*) override { }
            void UpdateAI(uint32 diff) override
            {
                events.Update(diff);
                while (uint32 event = events.ExecuteEvent())
                {
                    Player* owner = ObjectAccessor::GetPlayer(*me, ownerGuid);
                    if (!OwnsRun(owner, me, runId) || !me->IsAlive())
                    {
                        ClearPotions();
                        me->DespawnOrUnsummon();
                        return;
                    }
                    if (event == CheckOwner)
                    {
                        bool const inCombat = owner->IsAlive() && owner->IsInCombat();
                        if (inCombat && !wasInCombat)
                            events.RescheduleEvent(ThrowPotions, Milliseconds(1000));
                        else if (!inCombat && wasInCombat)
                        {
                            events.CancelEvent(ThrowPotions);
                            ClearPotions();
                        }
                        wasInCombat = inCombat;
                        events.ScheduleEvent(CheckOwner, Milliseconds(1000));
                    }
                    else if (event == ThrowPotions)
                    {
                        if (owner->IsAlive() && owner->IsInCombat() && me->IsWithinDistInMap(owner, 30.0f) && me->IsWithinLOSInMap(owner))
                        {
                            ClearPotions();
                            for (unsigned i = 0; i < potions.size(); ++i)
                            {
                                float const angle = owner->GetOrientation() + i * 2.0943951f;
                                float const x = owner->GetPositionX() + 3.0f * std::cos(angle);
                                float const y = owner->GetPositionY() + 3.0f * std::sin(angle);
                                float const z = owner->GetMap()->GetHeight(owner->GetPhaseMask(), x, y,
                                    owner->GetPositionZ() + 3.0f, true, 6.0f);
                                if (!std::isfinite(z) || std::abs(z - owner->GetPositionZ()) > 3.0f ||
                                    !owner->IsWithinLOS(x, y, z + 0.5f))
                                    continue;
                                if (GameObject* potion = me->SummonGameObject(potionEntry, x, y, z + 0.05f,
                                    0, 0, 0, 0, 1, 30))
                                {
                                    potions[i] = potion->GetGUID();
                                    potion->AI()->SetGUID(ownerGuid, OwnerGuid);
                                    potion->AI()->SetGUID(me->GetGUID(), HealerGuid);
                                }
                            }
                        }
                        events.ScheduleEvent(ThrowPotions, Milliseconds(30000));
                    }
                }
            }
            void JustDied(Unit*) override
            {
                ClearPotions();
                me->DespawnOrUnsummon(Milliseconds(1000));
            }
        private:
            void ClearPotions()
            {
                for (auto& guid : potions)
                {
                    if (GameObject* potion = me->GetMap()->GetGameObject(guid))
                        potion->Delete();
                    guid.Clear();
                }
            }
            EventMap events;
            ObjectGuid ownerGuid;
            uint64 runId = 0;
            std::array<ObjectGuid, 3> potions;
            bool wasInCombat = false;
        };

        class CompanionScript : public CreatureScript
        {
        public:
            CompanionScript() : CreatureScript("npc_evermore_delve_brann") { }
            CreatureAI* GetAI(Creature* creature) const override { return new CompanionAI(creature); }
        };
        class PotionScript : public GameObjectScript
        {
        public:
            PotionScript() : GameObjectScript("go_evermore_delve_potion") { }
            GameObjectAI* GetAI(GameObject* object) const override { return new PotionAI(object); }
            bool OnGossipHello(Player*, GameObject*) override { return true; } // proximity pickup only
        };
    }

    Creature* SummonCompanion(Player* owner, std::uint64_t runId)
    {
        if (!companionEntry || !OwnsRun(owner, owner, runId))
            return nullptr;
        Creature* companion = owner->SummonCreature(companionEntry, owner->GetPosition(), TEMPSUMMON_MANUAL_DESPAWN);
        auto* ai = companion ? dynamic_cast<CompanionAI*>(companion->AI()) : nullptr;
        if (!ai || !companion->IsFriendlyTo(owner))
        {
            if (companion)
                companion->DespawnOrUnsummon();
            return nullptr;
        }
        ai->Bind(owner, runId);
        return companion;
    }
}

void AddEvermoreDelveCompanionScripts()
{
    using namespace Evermore::Delves;
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Brann.Enable", false))
        return;
    uint32 const npc = sConfigMgr->GetOption<uint32>("Delves.Prototype.Brann.Entry", 0);
    uint32 const potion = sConfigMgr->GetOption<uint32>("Delves.Prototype.Brann.PotionEntry", 0);
    uint32 const spellId = sConfigMgr->GetOption<uint32>("Delves.Prototype.Brann.HealSpell", 2061);
    uint32 const percent = sConfigMgr->GetOption<uint32>("Delves.Prototype.Brann.HealPercent", 20);
    auto const* creature = sObjectMgr->GetCreatureTemplate(npc);
    auto const* object = sObjectMgr->GetGameObjectTemplate(potion);
    auto const* spell = sSpellMgr->GetSpellInfo(spellId);
    if (!creature || !object || !creature->ScriptID || !object->ScriptId || object->type != GAMEOBJECT_TYPE_GENERIC || !spell || spell->Effects[EFFECT_0].Effect != SPELL_EFFECT_HEAL ||
        creature->ScriptID != sObjectMgr->GetScriptId("npc_evermore_delve_brann") ||
        object->ScriptId != sObjectMgr->GetScriptId("go_evermore_delve_potion") || !percent || percent > 30)
    {
        LOG_ERROR("module.delves", "Brann prototype not registered: templates, script bindings or heal metadata invalid");
        return;
    }
    companionEntry = npc;
    potionEntry = potion;
    healSpell = spellId;
    healPercent = percent;
    new CompanionScript();
    new PotionScript();
}
