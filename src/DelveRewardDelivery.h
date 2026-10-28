#ifndef EVERMORE_DELVE_REWARD_DELIVERY_H
#define EVERMORE_DELVE_REWARD_DELIVERY_H

class Player;
namespace Evermore::Delves
{
    bool InitializeDelveRewards();
    bool RewardsAdmissionAllowed(Player* player);
    void AddDelveRewardDelivery();
}
#endif
