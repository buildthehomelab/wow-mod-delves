#include "DelveJournal.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "Field.h"

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
        CharacterDatabase.DirectExecute("UPDATE mod_delves_run SET checkpoint = '{}' WHERE instance_id = {} "
            "AND BINARY checkpoint = BINARY '{}'", newState, _instance, oldState);
        // DirectExecute has no success result. A synchronous read-back establishes
        // acknowledgement, including the case that an identical retry committed.
        return Read() == desired;
    }
}
