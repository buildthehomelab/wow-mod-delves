#include "DelveJournal.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "Field.h"
#include "DelveRewards.h"
#include <stdexcept>

namespace Evermore::Delves
{
    std::uint64_t Journal::Reserve() const
    {
        if (!_instance)
            return 0;
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_delves_run (instance_id, checkpoint) VALUES ({}, '')",
            _instance);
        auto result = CharacterDatabase.Query("SELECT run_id FROM mod_delves_run WHERE instance_id = {} AND checkpoint = ''",
            _instance);
        return result ? result->Fetch()[0].Get<uint64>() : 0;
    }

    std::string Journal::Read() const
    {
        auto result = CharacterDatabase.Query("SELECT checkpoint FROM mod_delves_run WHERE instance_id = {}", _instance);
        return result ? result->Fetch()[0].Get<std::string>() : std::string();
    }

    bool Journal::Replace(std::string const& expected, std::string const& desired) const
    {
        // These are bounded Run serializations, not client strings. Escape anyway;
        // external modules cannot register core prepared-statement enum slots.
        if (desired.empty() || desired.size() > 32768 || expected.size() > 32768)
            return false;
        auto oldState = expected;
        auto newState = desired;
        CharacterDatabase.EscapeString(oldState);
        CharacterDatabase.EscapeString(newState);
        try
        {
            auto const run = Run::Load(desired, _definition);
            if (run.InstanceId() != _instance)
                return false;
            auto transaction = CharacterDatabase.BeginTransaction();
            transaction->Append("UPDATE mod_delves_run SET checkpoint = '{}' WHERE instance_id = {} "
                "AND run_id = {} AND BINARY checkpoint = BINARY '{}'", newState, _instance, run.Id(), oldState);
            auto const awards = CompletionAwards(run);
            for (auto const& award : awards)
                transaction->Append("{}", CompletionAwardInsert(award, _instance, newState));
            CharacterDatabase.DirectCommitTransaction(transaction);
            // DirectCommitTransaction returns void. Confirm checkpoint AND every
            // exact immutable award before acknowledging completion to memory.
            if (Read() != desired)
                return false;
            for (auto const& award : awards)
            {
                auto result = CharacterDatabase.Query("SELECT COUNT(*) FROM mod_delves_reward WHERE run_id={} "
                    "AND guid={} AND account={} AND coins={} AND policy=1",
                    award.run, award.guid, award.account, award.coins);
                if (!result || result->Fetch()[0].Get<uint64>() != 1)
                    return false;
            }
            return true;
        }
        catch (std::invalid_argument const&)
        {
            return false;
        }
    }
}
