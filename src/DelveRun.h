#ifndef EVERMORE_DELVE_RUN_H
#define EVERMORE_DELVE_RUN_H

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Evermore::Delves
{
    enum class EventKind : std::uint32_t { Interact = 1, Kill = 2 };
    enum class RunStatus : std::uint32_t { Active = 1, Complete = 2, Abandoned = 3 };

    struct Objective
    {
        EventKind kind;
        std::uint32_t entry;
        std::uint32_t required;
    };

    struct Definition
    {
        std::uint32_t revision;
        std::uint32_t map;
        std::vector<Objective> objectives;
    };

    // Admission inputs are frozen. Level and progression are separate dimensions.
    struct Member
    {
        std::uint32_t guid;
        std::uint32_t account;
        std::uint32_t level;
        std::uint32_t progression;
    };

    struct Event
    {
        std::uint32_t map;
        std::uint32_t instance;
        std::uint32_t creditedMember;
        EventKind kind;
        std::uint32_t entry;
        std::uint64_t actor; // stable, server-authored actor identity within this run
    };

    class Run
    {
    public:
        static constexpr std::uint32_t MaxObjectives = 32;
        static constexpr std::uint32_t MaxCredits = 1024;

        Run(std::uint64_t id, std::uint32_t instance, std::uint32_t tier,
            Definition definition, std::vector<Member> members);

        bool Credit(Event const& event);
        bool Abandon();
        bool IsCurrentObjective(EventKind kind, std::uint32_t entry) const;
        std::optional<Objective> CurrentObjective() const;
        std::uint32_t ObjectiveCount() const { return static_cast<std::uint32_t>(_definition.objectives.size()); }
        std::string Save() const;
        static Run Load(std::string const& saved, Definition const& expected);

        // An outbox key, not an instruction to grant money. A durable wallet
        // integration must atomically deduplicate this key before paying.
        std::string RewardKey(std::uint32_t member) const;
        RunStatus Status() const { return _status; }
        std::uint32_t Stage() const { return _stage; }
        std::uint32_t Progress() const { return _progress; }
        std::uint32_t Tier() const { return _tier; }
        std::uint64_t Id() const { return _id; }
        std::uint32_t InstanceId() const { return _instance; }
        std::vector<Member> const& Members() const { return _members; }

    private:
        bool HasMember(std::uint32_t guid) const;
        std::uint64_t _id;
        std::uint32_t _instance;
        std::uint32_t _tier;
        Definition _definition;
        std::vector<Member> _members;
        RunStatus _status = RunStatus::Active;
        std::uint32_t _stage = 0;
        std::uint32_t _progress = 0;
        std::set<std::uint64_t> _credited;
    };
}
#endif
