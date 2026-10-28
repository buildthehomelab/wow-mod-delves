#ifndef EVERMORE_DELVE_REWARDS_H
#define EVERMORE_DELVE_REWARDS_H

#include "DelveRun.h"
#include <string>
#include <vector>

namespace Evermore::Delves
{
    struct CompletionAward
    {
        std::uint64_t run;
        std::uint32_t guid;
        std::uint32_t account;
        std::uint32_t coins;
    };

    // Pilot economy policy1: one coin per tier, frozen only at completion.
    // This records intent; delivery requires a separate idempotent wallet caller.
    inline std::vector<CompletionAward> CompletionAwards(Run const& run)
    {
        std::vector<CompletionAward> result;
        if (run.Status() == RunStatus::Complete)
            for (auto const& member : run.Members())
                result.push_back({run.Id(), member.guid, member.account, run.Tier()});
        return result;
    }

    // checkpoint is the database-escaped bounded server serialization. Both the
    // CAS update and this insert execute in one InnoDB transaction. A stale CAS
    // cannot insert intent unless the identical completed state already exists.
    inline std::string CompletionAwardInsert(CompletionAward const& award,
        std::uint32_t instance, std::string const& checkpoint)
    {
        return "INSERT INTO mod_delves_reward (run_id,guid,account,coins,policy) SELECT " +
            std::to_string(award.run) + "," + std::to_string(award.guid) + "," +
            std::to_string(award.account) + "," + std::to_string(award.coins) + ",1 " +
            "FROM mod_delves_run WHERE run_id=" + std::to_string(award.run) +
            " AND instance_id=" + std::to_string(instance) + " AND BINARY checkpoint=BINARY '" + checkpoint +
            "' ON DUPLICATE KEY UPDATE run_id=mod_delves_reward.run_id";
    }
}
#endif
