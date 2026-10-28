#include "DelveServerAdapter.h"
#include "DelveScaling.h"
#include "DelveCart.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "EventMap.h"
#include "Log.h"
#include "Map.h"
#include "ModelIgnoreFlags.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
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
        bool cartEnabled = false;
        uint32 cartEntry = 0, cartImpactSpell = 0, cartVisualSpell = 0;
        // Validated native rail centers. These remain controlled-test adaptations.
        std::vector<CartPoint> const cartLane{{-791.20508, -330.61328, 76.4421},
            {-773.48242, -307.92969, 76.4466}};
        // These are explicitly configured 3.3.5 test adaptations, never retail IDs.
        std::array<uint32, 3> spells{};
        enum Events : uint32 { WebBolt = 1, Agony = 2, Shackles = 3, BurningCart = 4 };

        class BurningCartAI : public ScriptedAI
        {
        public:
            explicit BurningCartAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetReactState(REACT_PASSIVE);
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            }
            void Bind(Player* owner, Creature* boss, Run const& run)
            {
                ownerGuid = owner->GetGUID();
                bossGuid = boss->GetGUID();
                runId = run.Id();
                cart.emplace(runId, run.Members()[0].guid, run.Members()[0].account,
                    cartLane, 8, 2.5, 2, 0);
                me->CastSpell(me, cartVisualSpell, true);
            }
            void AttackStart(Unit*) override { }
            bool CanAIAttack(Unit const*) const override { return false; }
            void DamageTaken(Unit*, uint32& damage, DamageEffectType, SpellSchoolMask) override { damage = 0; }
            void UpdateAI(uint32 diff) override
            {
                Player* owner = ObjectAccessor::GetPlayer(*me, ownerGuid);
                Creature* boss = me->GetMap()->GetCreature(bossGuid);
                auto* instance = dynamic_cast<InstanceRun*>(me->GetInstanceScript());
                auto run = instance ? instance->CurrentRun() : std::nullopt;
                if (!cartEnabled || !cart || !owner || !owner->IsInWorld() || !owner->IsAlive() ||
                    owner->GetGroup() || owner->GetMap() != me->GetMap() || !owner->InSamePhase(me) ||
                    !owner->GetSession() || owner->GetSession()->IsHeadless() ||
                    !boss || !boss->IsAlive() || !boss->IsInCombat() || !boss->InSamePhase(me) ||
                    boss->GetEntry() != bossEntry || !boss->GetSpawnId() ||
                    !run || instance->Blocked() || run->Id() != runId || run->Status() != RunStatus::Active ||
                    !run->IsCurrentObjective(EventKind::Kill, bossEntry) || run->Members().size() != 1 ||
                    run->Members()[0].guid != owner->GetGUID().GetRawValue() ||
                    run->Members()[0].account != owner->GetSession()->GetAccountId() ||
                    run->Members()[0].level != owner->GetLevel() ||
                    ReadDelveProgression(owner) != run->Members()[0].progression)
                {
                    me->DespawnOrUnsummon();
                    return;
                }
                if (warning)
                {
                    warning -= std::min(warning, diff);
                    if (!warning)
                    {
                        Movement::MoveSplineInit movement(me);
                        auto end = cartLane.back();
                        movement.MoveTo(end.x, end.y, end.z, false);
                        movement.SetVelocity(8);
                        movement.Launch();
                    }
                    // Movement starts at this tick, so do not advance the hazard
                    // clock by time already spent displaying the warning.
                    return;
                }
                CartVictim victim{runId, run->Members()[0].guid, run->Members()[0].account,
                    {owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ()}};
                auto step = cart->Advance(diff, &victim, [this, owner](CartPoint const& contact, CartPoint const& target)
                {
                    float const height = owner->GetCollisionHeight() * 0.5f;
                    return me->GetMap()->isInLineOfSight(contact.x, contact.y, contact.z + height,
                        target.x, target.y, target.z + height, me->GetPhaseMask(),
                        LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing);
                });
                if (step.impact)
                {
                    auto damage = DelveSpellDamage(boss, run->Members()[0], run->Tier(), false);
                    if (damage)
                    {
                        int32 amount = static_cast<int32>(std::min<uint64>(
                            static_cast<uint64>(*damage) * 2, std::numeric_limits<int32>::max()));
                        boss->CastCustomSpell(cartImpactSpell, SPELLVALUE_BASE_POINT0, amount, owner, true);
                        if (owner->IsInWorld() && owner->IsAlive() && owner->GetMap() == me->GetMap() && owner->InSamePhase(me))
                            owner->KnockbackFrom(step.impactOrigin.x, step.impactOrigin.y, 8, 3);
                    }
                }
                if (step.stage == CartStage::Finished)
                    me->DespawnOrUnsummon();
            }
        private:
            ObjectGuid ownerGuid, bossGuid;
            uint64 runId = 0;
            uint32 warning = 2000;
            std::optional<Cart> cart;
        };

        class BurningCartScript : public CreatureScript
        {
        public:
            BurningCartScript() : CreatureScript("npc_evermore_burning_cart") { }
            CreatureAI* GetAI(Creature* creature) const override { return new BurningCartAI(creature); }
        };

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
                ClearCart();
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
                auto const run = instance->CurrentRun();
                if (!run || !ApplyDelveScale(me, run->Members()[0], run->Tier()))
                {
                    EnterEvadeMode();
                    return;
                }
                activeRun = run->Id();
                events.ScheduleEvent(WebBolt, Milliseconds(2200));
                events.ScheduleEvent(Agony, Milliseconds(6200));
                events.ScheduleEvent(Shackles, Milliseconds(20200));
                if (cartEnabled)
                    events.ScheduleEvent(BurningCart, Milliseconds(12100));
            }

            void JustDied(Unit*) override { ClearCart(); events.Reset(); activeRun = 0; }

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
                            CastDamage(spells[0], false);
                            events.ScheduleEvent(WebBolt, Milliseconds(6000));
                            break;
                        case Agony:
                            CastDamage(spells[1], true);
                            events.ScheduleEvent(Agony, Milliseconds(24200));
                            break;
                        case Shackles:
                            DoCastVictim(spells[2]);
                            events.ScheduleEvent(Shackles, Milliseconds(32900));
                            break;
                        case BurningCart:
                            SummonCart();
                            events.ScheduleEvent(BurningCart, Milliseconds(35200));
                            break;
                        default:
                            break;
                    }
                }
                DoMeleeAttackIfReady();
            }

        private:
            void ClearCart()
            {
                if (Creature* cart = me->GetMap()->GetCreature(cartGuid))
                    cart->DespawnOrUnsummon();
                cartGuid.Clear();
            }
            void SummonCart()
            {
                ClearCart();
                auto* instance = dynamic_cast<InstanceRun*>(me->GetInstanceScript());
                auto run = instance ? instance->CurrentRun() : std::nullopt;
                Player* owner = me->GetVictim() ? me->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
                if (!run || !owner || !Eligible(owner))
                    return;
                auto start = cartLane.front();
                Position position(start.x, start.y, start.z, std::atan2(
                    cartLane.back().y - start.y, cartLane.back().x - start.x));
                if (Creature* cart = me->SummonCreature(cartEntry, position, TEMPSUMMON_MANUAL_DESPAWN))
                {
                    if (auto* ai = dynamic_cast<BurningCartAI*>(cart->AI()))
                    {
                        cartGuid = cart->GetGUID();
                        ai->Bind(owner, me, *run);
                    }
                    else
                        cart->DespawnOrUnsummon();
                }
            }
            void CastDamage(uint32 spell, bool periodic)
            {
                if (!ScalingEnabled())
                {
                    DoCastVictim(spell);
                    return;
                }
                auto* instance = dynamic_cast<InstanceRun*>(me->GetInstanceScript());
                auto run = instance ? instance->CurrentRun() : std::nullopt;
                auto damage = run ? DelveSpellDamage(me, run->Members()[0], run->Tier(), periodic) : std::nullopt;
                if (damage)
                    me->CastCustomSpell(spell, SPELLVALUE_BASE_POINT0, static_cast<int32>(*damage), me->GetVictim(), false);
            }

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
                    run->Members()[0].level == player->GetLevel() &&
                    (!ScalingEnabled() || ReadDelveProgression(player) == run->Members()[0].progression);
            }

            EventMap events;
            uint64 activeRun = 0;
            ObjectGuid cartGuid;
        };

        class EarthcrawlBossScript : public CreatureScript
        {
        public:
            EarthcrawlBossScript() : CreatureScript("boss_evermore_abenar") { }
            CreatureAI* GetAI(Creature* creature) const override { return new EarthcrawlBossAI(creature); }
        };
    }
}

bool InitializeEvermoreDelveEncounter()
{
    using namespace Evermore::Delves;
    cartEnabled = false;
    bool const requestedCart = sConfigMgr->GetOption<bool>("Delves.Prototype.Encounter.Cart.Enable", false);
    if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Encounter.Enable", false))
    {
        if (requestedCart)
            LOG_ERROR("module.delves", "Burning Cart requires the Earthcrawl encounter to be enabled");
        return !requestedCart;
    }
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
            return false;
        }
    }
    if (ScalingEnabled() &&
        (sSpellMgr->GetSpellInfo(spells[0])->Effects[EFFECT_0].Effect != SPELL_EFFECT_SCHOOL_DAMAGE ||
         sSpellMgr->GetSpellInfo(spells[1])->Effects[EFFECT_0].Effect != SPELL_EFFECT_APPLY_AURA ||
         sSpellMgr->GetSpellInfo(spells[1])->Effects[EFFECT_0].ApplyAuraName != SPELL_AURA_PERIODIC_DAMAGE))
    {
        LOG_ERROR("module.delves", "Scaled Earthcrawl encounter requires direct bolt and periodic agony effect0");
        return false;
    }
    if (requestedCart)
    {
        cartEntry = sConfigMgr->GetOption<uint32>("Delves.Prototype.Encounter.Cart.Entry", 0);
        uint32 display = sConfigMgr->GetOption<uint32>("Delves.Prototype.Encounter.Cart.Display", 0);
        auto const* creature = sObjectMgr->GetCreatureTemplate(cartEntry);
        auto const* visual = sSpellMgr->GetSpellInfo(28330);
        auto const* impact = sSpellMgr->GetSpellInfo(20832);
        auto const* model = sCreatureDisplayInfoStore.LookupEntry(display);
        if (!ScalingEnabled() || sConfigMgr->GetOption<uint32>("Delves.Prototype.ContentRevision", 0) != 2026102803 ||
            cartEntry != 1499002 || display != 149990 || !creature || creature->Models.size() != 1 ||
            creature->Models[0].CreatureDisplayID != display || !model || model->ModelId != 149990 ||
            !sCreatureModelDataStore.LookupEntry(model->ModelId) || !creature->ScriptID ||
            creature->ScriptID != sObjectMgr->GetScriptId("npc_evermore_burning_cart") ||
            !visual || !impact || impact->IsPositive() ||
            impact->Effects[EFFECT_0].Effect != SPELL_EFFECT_SCHOOL_DAMAGE ||
            impact->Effects[EFFECT_0].TargetA.GetTarget() != TARGET_UNIT_TARGET_ENEMY ||
            impact->Effects[EFFECT_0].TargetB.GetTarget() != Targets(0) ||
            impact->Effects[EFFECT_1].Effect != 0 ||
            impact->Effects[EFFECT_2].Effect != 0 ||
            visual->Effects[EFFECT_0].Effect != SPELL_EFFECT_APPLY_AURA ||
            visual->Effects[EFFECT_0].ApplyAuraName != SPELL_AURA_DUMMY ||
            visual->Effects[EFFECT_0].TargetA.GetTarget() != TARGET_UNIT_CASTER ||
            visual->Effects[EFFECT_0].TargetB.GetTarget() != Targets(0) ||
            visual->Effects[EFFECT_1].Effect != 0 ||
            visual->Effects[EFFECT_2].Effect != 0)
        {
            LOG_ERROR("module.delves", "Burning Cart requires scaling and matching loaded cart template/display/spells");
            return false;
        }
        cartImpactSpell = 20832;
        cartVisualSpell = 28330;
        cartEnabled = true;
    }
    return true;
}

void AddEvermoreDelveEncounterScripts()
{
    using namespace Evermore::Delves;
    new EarthcrawlBossScript();
    new BurningCartScript();
}
