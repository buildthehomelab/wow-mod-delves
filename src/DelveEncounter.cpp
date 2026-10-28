#include "DelveServerAdapter.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "EventMap.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <array>

namespace Evermore::Delves
{
    namespace
    {
        uint32 bossEntry = 0;
        // These are explicitly configured 3.3.5 test adaptations, never retail IDs.
        std::array<uint32, 3> spells{};
        enum Events : uint32 { WebBolt = 1, Agony = 2, Shackles = 3 };

        class EarthcrawlBossAI : public ScriptedAI
        {
        public:
            explicit EarthcrawlBossAI(Creature* creature) : ScriptedAI(creature) { }

            bool CanAIAttack(Unit const* target) const override
            {
                return Eligible(target);
            }

            void AttackStart(Unit* target) override
            {
                if (Eligible(target))
                    ScriptedAI::AttackStart(target);
            }

            void Reset() override
            {
                events.Reset();
                activeRun = 0;
            }

            void JustEngagedWith(Unit* target) override
            {
                if (!Eligible(target))
                {
                    EnterEvadeMode();
                    return;
                }
                auto* instance = dynamic_cast<InstanceRun*>(me->GetInstanceScript());
                activeRun = instance->CurrentRun()->Id();
                events.ScheduleEvent(WebBolt, Milliseconds(2200));
                events.ScheduleEvent(Agony, Milliseconds(6200));
                events.ScheduleEvent(Shackles, Milliseconds(20200));
            }

            void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*type*/,
                SpellSchoolMask /*school*/) override
            {
                if (!Eligible(attacker))
                    damage = 0;
            }

            void UpdateAI(uint32 diff) override
            {
                if (!me->IsInCombat())
                    return;
                if (!Eligible(me->GetVictim()))
                {
                    EnterEvadeMode();
                    return;
                }
                if (!UpdateVictim())
                    return;
                events.Update(diff);
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
                if (uint32 event = events.ExecuteEvent())
                {
                    switch (event)
                    {
                        case WebBolt:
                            DoCastVictim(spells[0]);
                            events.ScheduleEvent(WebBolt, Milliseconds(6000));
                            break;
                        case Agony:
                            DoCastVictim(spells[1]);
                            events.ScheduleEvent(Agony, Milliseconds(24200));
                            break;
                        case Shackles:
                            DoCastVictim(spells[2]);
                            events.ScheduleEvent(Shackles, Milliseconds(32900));
                            break;
                        default:
                            break;
                    }
                }
                DoMeleeAttackIfReady();
            }

        private:
            bool Eligible(Unit const* target) const
            {
                Player const* player = target ? target->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
                auto* instance = dynamic_cast<InstanceRun*>(me->GetInstanceScript());
                auto run = instance ? instance->CurrentRun() : std::nullopt;
                return target && target->IsInWorld() && target->IsAlive() &&
                    target->GetMap() == me->GetMap() && target->InSamePhase(me) &&
                    me->GetEntry() == bossEntry && me->GetSpawnId() &&
                    player && player->IsInWorld() && player->IsAlive() && !player->GetGroup() &&
                    player->GetMap() == me->GetMap() && player->InSamePhase(me) &&
                    player->GetSession() && !player->GetSession()->IsHeadless() &&
                    run && !instance->Blocked() && run->Status() == RunStatus::Active &&
                    (!activeRun || run->Id() == activeRun) && run->IsCurrentObjective(EventKind::Kill, bossEntry) &&
                    run->Members().size() == 1 &&
                    run->Members()[0].guid == player->GetGUID().GetRawValue() &&
                    run->Members()[0].account == player->GetSession()->GetAccountId() &&
                    run->Members()[0].level == player->GetLevel();
            }

            EventMap events;
            uint64 activeRun = 0;
        };

        class EarthcrawlBossScript : public CreatureScript
        {
        public:
            EarthcrawlBossScript() : CreatureScript("boss_evermore_abenar") { }
            CreatureAI* GetAI(Creature* creature) const override { return new EarthcrawlBossAI(creature); }
        };
    }
}

void AddEvermoreDelveEncounterScripts()
{
    using namespace Evermore::Delves;
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Encounter.Enable", false))
        return;
    bossEntry = sConfigMgr->GetOption<uint32>("Delves.Prototype.BossEntry", 0);
    spells = {sConfigMgr->GetOption<uint32>("Delves.Prototype.Encounter.WebBolt", 0),
        sConfigMgr->GetOption<uint32>("Delves.Prototype.Encounter.Agony", 0),
        sConfigMgr->GetOption<uint32>("Delves.Prototype.Encounter.Shackles", 0)};
    for (uint32 spellId : spells)
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell || spell->IsPositive())
        {
            LOG_ERROR("module.delves", "Earthcrawl encounter not registered: a harmful test spell is missing");
            return;
        }
    }
    new EarthcrawlBossScript();
}
