#ifndef EVERMORE_DELVE_REWARD_DELIVERY_H
#define EVERMORE_DELVE_REWARD_DELIVERY_H

#include <string>

class Player;
namespace Evermore::Delves
{
    class Run;
    bool InitializeDelveRewards();
    bool RewardsAdmissionAllowed(Player* player);
    std::string DescribeReward(Player* player, Run const& run);
    void AddDelveRewardDelivery();
}
#endif
