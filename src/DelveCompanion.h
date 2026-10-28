#ifndef EVERMORE_DELVE_COMPANION_H
#define EVERMORE_DELVE_COMPANION_H
#include <cstdint>
class Creature;
class Player;
namespace Evermore::Delves
{
    Creature* SummonCompanion(Player* owner, std::uint64_t runId);
}
void AddEvermoreDelveCompanionScripts();
bool InitializeEvermoreDelveCompanion();
#endif
