#ifndef EVERMORE_DELVE_SERVER_ADAPTER_H
#define EVERMORE_DELVE_SERVER_ADAPTER_H

#include "DelveRun.h"
#include "InstanceScript.h"
#include <memory>

class Creature;
class GameObject;
class Player;

namespace Evermore::Delves
{
    // Server-internal entry point. A future admission service must allocate the
    // durable run ID and validate progression/tier access before calling Begin.
    class InstanceRun : public InstanceScript
    {
    public:
        InstanceRun(Map* map, Definition definition);
        bool Begin(std::uint64_t allocatedRunId, std::uint32_t tier, std::vector<Member> const& members);
        bool CreditKill(Player* player, Creature* creature);
        bool CreditInteraction(Player* player, GameObject* object);
        std::string GetSaveData() override;
        void Load(char const* data) override;
        Run const* CurrentRun() const { return _run.get(); }

    private:
        bool IsRealMemberPresent(Player const* player) const;
        bool Accept(Event const& event);
        Definition _definition;
        std::unique_ptr<Run> _run;
        bool _invalidSave = false;
        std::string _invalidSaveData;
    };
}
#endif
