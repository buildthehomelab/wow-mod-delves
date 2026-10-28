#ifndef EVERMORE_DELVE_SCALING_H
#define EVERMORE_DELVE_SCALING_H

#include "DelveRun.h"
#include <limits>
#include <optional>

class Creature;
class Player;

namespace Evermore::Delves
{
    struct ScaleProfile
    {
        std::uint32_t level;
        std::uint32_t expansion;
        std::uint32_t healthPercent;
        std::uint32_t damagePercent;
    };

    // Evermore design balance, not retail tuning. Progression is a separate cap.
    inline std::optional<ScaleProfile> MakeScaleProfile(std::uint32_t level,
        std::uint32_t progression, std::uint32_t tier)
    {
        if (level < 10 || level > 80 || progression > 18 || !tier || tier > 11)
            return std::nullopt;
        std::uint32_t const era = progression >= 13 ? 2 : progression >= 8 ? 1 : 0;
        std::uint32_t const cap = era == 2 ? 80 : era == 1 ? 70 : 60;
        if (level > cap)
            return std::nullopt;
        // Advanced progression on a low-level alt must not use empty high-era rows.
        std::uint32_t const expansion = level > 70 ? 2 : level > 60 ? 1 : 0;
        return ScaleProfile{level, expansion, 100 + 35 * (tier - 1), 100 + 20 * (tier - 1)};
    }

    inline std::optional<std::uint32_t> ScaleAmount(std::uint32_t base, std::uint32_t percent)
    {
        std::uint64_t const value = (static_cast<std::uint64_t>(base) * percent + 99) / 100;
        return value && value <= std::numeric_limits<std::uint32_t>::max()
            ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(value)) : std::nullopt;
    }

    bool InitializeDelveScaling();
    bool ScalingEnabled();
    std::optional<std::uint32_t> ReadDelveProgression(Player const* player);
    bool ApplyDelveScale(Creature* creature, Member const& member, std::uint32_t tier, bool companion = false);
    std::optional<std::uint32_t> DelveSpellDamage(Creature const* creature, Member const& member,
        std::uint32_t tier, bool periodic);
}
#endif
