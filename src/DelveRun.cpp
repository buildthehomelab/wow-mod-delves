#include "DelveRun.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Evermore::Delves
{
    namespace
    {
        std::uint64_t Read(std::istream& input)
        {
            std::string token;
            if (!(input >> token))
                throw std::invalid_argument("truncated delve save");
            std::uint64_t value = 0;
            auto const result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc() || result.ptr != token.data() + token.size())
                throw std::invalid_argument("invalid unsigned delve save field");
            return value;
        }

        std::uint32_t Read32(std::istream& input)
        {
            auto const value = Read(input);
            if (value > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("delve save field overflow");
            return static_cast<std::uint32_t>(value);
        }
    }

    Run::Run(std::uint64_t id, std::uint32_t instance, std::uint32_t tier,
        Definition definition, std::vector<Member> members)
        : _id(id), _instance(instance), _tier(tier), _definition(std::move(definition)),
          _members(std::move(members))
    {
        if (!id || !instance || !tier || tier > 11 || !_definition.revision || !_definition.map ||
            _definition.objectives.empty() || _definition.objectives.size() > MaxObjectives ||
            _members.empty() || _members.size() > 5)
            throw std::invalid_argument("invalid delve admission");
        std::uint32_t total = 0;
        for (auto const& objective : _definition.objectives)
        {
            if (!objective.entry || !objective.required || objective.required > MaxCredits ||
                (objective.kind != EventKind::Interact && objective.kind != EventKind::Kill))
                throw std::invalid_argument("invalid delve objective");
            total += objective.required;
        }
        if (total > MaxCredits)
            throw std::invalid_argument("too many delve credits");
        std::set<std::uint32_t> guids;
        for (auto const& member : _members)
            if (!member.guid || !member.account || !member.level || member.level > 80 ||
                !guids.insert(member.guid).second)
                throw std::invalid_argument("invalid delve member");
    }

    bool Run::HasMember(std::uint32_t guid) const
    {
        return std::any_of(_members.begin(), _members.end(), [guid](Member const& member)
        {
            return member.guid == guid;
        });
    }

    bool Run::IsCurrentObjective(EventKind kind, std::uint32_t entry) const
    {
        return _status == RunStatus::Active && _stage < _definition.objectives.size() &&
            _definition.objectives[_stage].kind == kind && _definition.objectives[_stage].entry == entry;
    }

    std::optional<Objective> Run::CurrentObjective() const
    {
        if (_status != RunStatus::Active || _stage >= _definition.objectives.size())
            return std::nullopt;
        return _definition.objectives[_stage];
    }

    bool Run::Credit(Event const& event)
    {
        if (_status != RunStatus::Active || event.map != _definition.map || event.instance != _instance ||
            !event.actor || !HasMember(event.creditedMember))
            return false;
        auto const& objective = _definition.objectives[_stage];
        if (event.kind != objective.kind || event.entry != objective.entry || !_credited.insert(event.actor).second)
            return false;
        if (++_progress == objective.required)
        {
            _progress = 0;
            if (++_stage == _definition.objectives.size())
                _status = RunStatus::Complete;
        }
        return true;
    }

    bool Run::Abandon()
    {
        if (_status != RunStatus::Active)
            return false;
        _status = RunStatus::Abandoned;
        return true;
    }

    std::string Run::RewardKey(std::uint32_t member) const
    {
        if (_status != RunStatus::Complete || !HasMember(member))
            return {};
        return "delve:" + std::to_string(_id) + ":" + std::to_string(member);
    }

    std::string Run::Save() const
    {
        std::ostringstream output;
        output << "EVD1 " << _id << ' ' << _instance << ' ' << _tier << ' ' << _definition.revision << ' '
            << _definition.map << ' ' << static_cast<std::uint32_t>(_status) << ' ' << _stage << ' '
            << _progress << ' ' << _members.size();
        for (auto const& member : _members)
            output << ' ' << member.guid << ' ' << member.account << ' ' << member.level << ' ' << member.progression;
        output << ' ' << _credited.size();
        for (auto const actor : _credited)
            output << ' ' << actor;
        return output.str();
    }

    Run Run::Load(std::string const& saved, Definition const& expected)
    {
        if (saved.size() > 32768)
            throw std::invalid_argument("delve save too large");
        std::istringstream input(saved);
        std::string magic;
        input >> magic;
        if (magic != "EVD1")
            throw std::invalid_argument("unknown delve save format");
        auto const id = Read(input);
        auto const instance = Read32(input);
        auto const tier = Read32(input);
        auto const revision = Read32(input);
        auto const map = Read32(input);
        auto const status = Read32(input);
        auto const stage = Read32(input);
        auto const progress = Read32(input);
        auto const count = Read32(input);
        if (revision != expected.revision || map != expected.map || !count || count > 5 || status < 1 || status > 3)
            throw std::invalid_argument("incompatible delve save");
        std::vector<Member> members;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Member member{};
            member.guid = Read32(input);
            member.account = Read32(input);
            member.level = Read32(input);
            member.progression = Read32(input);
            members.push_back(member);
        }
        Run result(id, instance, tier, expected, std::move(members));
        result._status = static_cast<RunStatus>(status);
        result._stage = stage;
        result._progress = progress;
        bool const complete = result._status == RunStatus::Complete;
        if ((complete && (stage != expected.objectives.size() || progress)) ||
            (!complete && (stage >= expected.objectives.size() || progress >= expected.objectives[stage].required)))
            throw std::invalid_argument("invalid saved delve stage");
        auto const credits = Read32(input);
        std::uint32_t expectedCredits = progress;
        for (std::uint32_t i = 0; i < stage; ++i)
            expectedCredits += expected.objectives[i].required;
        if (credits != expectedCredits || credits > MaxCredits)
            throw std::invalid_argument("invalid saved delve credit count");
        for (std::uint32_t i = 0; i < credits; ++i)
        {
            auto const actor = Read(input);
            if (!actor || !result._credited.insert(actor).second)
                throw std::invalid_argument("invalid saved delve actor");
        }
        std::string extra;
        if (input >> extra)
            throw std::invalid_argument("trailing delve save data");
        return result;
    }
}
