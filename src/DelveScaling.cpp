#include "DelveScaling.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include <cmath>
#include <limits>

namespace Evermore::Delves
{
    namespace
    {
        bool enabled = false;
        bool progressionCatalog = false;

        std::optional<uint32> Bounded(double value)
        {
            if (!std::isfinite(value) || value < 1 || value > std::numeric_limits<int32>::max())
                return std::nullopt;
            return static_cast<uint32>(std::ceil(value));
        }
    }

    bool InitializeDelveScaling()
    {
        enabled = sConfigMgr->GetOption<bool>("Delves.Prototype.Scaling.Enable", false);
        if (!enabled)
            return true;
        if (!sConfigMgr->GetOption<bool>("IndividualProgression.Enable", true))
        {
            LOG_ERROR("module.delves", "Delve scaling requires enabled Individual Progression");
            return false;
        }
        // Data contract: IP stages use rewarded hidden quest IDs 66001-66018.
        // Stage11 is unused by the current catalog. No IP implementation is copied.
        for (uint32 stage = 1; stage <= 18; ++stage)
            if (stage != 11 && !sObjectMgr->GetQuestTemplate(66000 + stage))
            {
                LOG_ERROR("module.delves", "Delve scaling requires the complete IP quest catalog");
                return false;
            }
        progressionCatalog = true;
        return true;
    }

    bool ScalingEnabled() { return enabled; }

    std::optional<uint32> ReadDelveProgression(Player const* player)
    {
        if (!enabled || !progressionCatalog || !player || !player->IsInWorld())
            return std::nullopt;
        for (uint32 stage = 18; stage > 0; --stage)
            if (player->GetQuestStatus(66000 + stage) == QUEST_STATUS_REWARDED)
                return stage;
        return 0; // A verified Classic character, not an unknown development snapshot.
    }

    bool ApplyDelveScale(Creature* creature, Member const& member, uint32 tier, bool companion)
    {
        if (!enabled)
            return true;
        auto const profile = MakeScaleProfile(member.level, member.progression, tier);
        if (!profile || !creature)
            return false;
        auto const* info = creature->GetCreatureTemplate();
        auto const* stats = sObjectMgr->GetCreatureBaseStats(profile->level, info->unit_class);
        auto const baseHealth = Bounded(stats->BaseHealth[profile->expansion] * static_cast<double>(info->ModHealth));
        auto const health = baseHealth ? ScaleAmount(*baseHealth, companion ? 100 : profile->healthPercent) : std::nullopt;
        double const damage = stats->BaseDamage[profile->expansion] * (companion ? 1.0 : profile->damagePercent / 100.0);
        double const mana = stats->BaseMana * static_cast<double>(info->ModMana);
        double const armor = stats->BaseArmor * static_cast<double>(info->ModArmor);
        if (!health || *health > std::numeric_limits<int32>::max() || !std::isfinite(damage) || damage < 0 ||
            damage > std::numeric_limits<int32>::max() || !std::isfinite(mana) || mana < 0 ||
            mana > std::numeric_limits<int32>::max() || !std::isfinite(armor) || armor < 0 || armor > std::numeric_limits<int32>::max())
            return false;
        creature->SetLevel(profile->level);
        creature->SetCreateHealth(*health);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, static_cast<float>(*health));
        creature->SetCreateMana(static_cast<uint32>(mana));
        creature->SetStatFlatModifier(UNIT_MOD_MANA, BASE_VALUE, static_cast<float>(mana));
        creature->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, static_cast<float>(armor));
        creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, stats->AttackPower);
        creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, stats->RangedAttackPower);
        for (auto attack : {BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK})
        {
            creature->SetBaseWeaponDamage(attack, MINDAMAGE, static_cast<float>(damage));
            creature->SetBaseWeaponDamage(attack, MAXDAMAGE, static_cast<float>(damage * 1.5));
        }
        creature->UpdateAllStats();
        creature->SetHealth(creature->GetMaxHealth());
        creature->SetPower(POWER_MANA, creature->GetMaxPower(POWER_MANA));
        creature->ResetPlayerDamageReq();
        return true;
    }

    std::optional<uint32> DelveSpellDamage(Creature const* creature, Member const& member, uint32 tier, bool periodic)
    {
        auto const profile = MakeScaleProfile(member.level, member.progression, tier);
        if (!enabled || !profile || !creature)
            return std::nullopt;
        auto const* stats = sObjectMgr->GetCreatureBaseStats(profile->level, creature->GetCreatureTemplate()->unit_class);
        // First test balance: a bolt uses 2x base damage, each agony tick 0.15x.
        return Bounded(stats->BaseDamage[profile->expansion] * (periodic ? 0.15 : 2.0) * profile->damagePercent / 100.0);
    }
}
