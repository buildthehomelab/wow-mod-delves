#ifndef EVERMORE_DELVE_SERVER_ADAPTER_H
#define EVERMORE_DELVE_SERVER_ADAPTER_H

#include "DelveRun.h"
#include "ConfirmedRun.h"
#include "DelveJournal.h"
#include "InstanceScript.h"
#include <memory>
#include <mutex>
#include <optional>

class Creature;
class GameObject;
class Player;

namespace Evermore::Delves
{
    // The first admission path is an explicitly enabled administrator test tool.
    class InstanceRun : public InstanceScript
    {
    public:
        InstanceRun(Map* map, Definition definition);
        bool Begin(std::uint32_t tier, std::vector<Member> const& members);
        bool Abandon();
        void EnsureCompanion(Player* player);
        void RefreshEncounterScale();
        void OnCreatureCreate(Creature* creature) override;
        bool CreditKill(Player* player, Creature* creature);
        bool CreditInteraction(Player* player, GameObject* object);
        void Initialize() override;
        std::string GetSaveData() override;
        void Load(char const* data) override;
        std::optional<Run> CurrentRun() const;
        bool Blocked() const;
        std::string ObjectiveText() const;

    private:
        bool IsRealMemberPresent(Player const* player) const;
        bool IsSnapshotMemberPresent(Player const* player) const;
        bool Accept(Event const& event);
        void ScaleEncounterCreature(Creature* creature);
        // Core saves re-enter GetSaveData while a confirmed mutation holds the lock.
        mutable std::recursive_mutex _lock;
        Definition _definition;
        Journal _journal;
        ObjectGuid _companionGuid;
        std::unique_ptr<ConfirmedRun> _run;
        bool _invalidSave = false;
        std::string _invalidSaveData;
    };
}
#endif
