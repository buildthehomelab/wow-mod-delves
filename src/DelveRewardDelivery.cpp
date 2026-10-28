#include "DelveRewardDelivery.h"
#include "DelveRewards.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldSession.h"
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if __has_include("BossCoins.h") && __has_include("DurableAward.h")
#include "BossCoins.h"
#define EVERMORE_HAS_DURABLE_WALLET 1
#endif

namespace Evermore::Delves
{
    namespace
    {
        bool enabled = false; // Written during loaded-world startup, before players.
        std::mutex deliveryLock;
        std::unordered_map<uint32, uint32> elapsed;
        std::unordered_set<uint32> quarantined;

        uint32 MemberId(Player const* player)
        {
            if (!player || !player->IsInWorld() || !player->GetSession() || player->GetSession()->IsHeadless())
                return 0;
            uint64 const guid = player->GetGUID().GetRawValue();
            return guid <= std::numeric_limits<uint32>::max() ? static_cast<uint32>(guid) : 0;
        }

        void Deliver(Player* player, uint32 diff)
        {
            if (!enabled)
                return;
            uint32 const guid = MemberId(player);
            if (!guid)
                return;
            {
                std::lock_guard<std::mutex> guard(deliveryLock);
                if (quarantined.count(guid))
                    return;
                auto& time = elapsed[guid];
                // Bound work to one durable intent per player per ten seconds.
                if (diff < 10000 && time < 10000 - diff)
                {
                    time += diff;
                    return;
                }
                time = 0;
            }
#ifdef EVERMORE_HAS_DURABLE_WALLET
            if (!BossCoins::Wallet::DelveAwardsReady())
                return; // Coin wallet loads later in startup; never call before ready.
            uint32 const account = player->GetSession()->GetAccountId();
            auto pending = CharacterDatabase.Query("SELECT run_id,coins,policy FROM mod_delves_reward "
                "WHERE guid={} AND account={} AND delivered=0 ORDER BY run_id LIMIT 1", guid, account);
            if (!pending)
                return; // Empty or unavailable: retain intent and retry later.
            Field* fields = pending->Fetch();
            uint64 const run = fields[0].Get<uint64>();
            uint32 const coins = fields[1].Get<uint32>();
            uint32 const policy = fields[2].Get<uint32>();
            auto const status = policy == 1 && coins >= 1 && coins <= 11 && run ?
                BossCoins::Wallet::AwardDelveOnce(player, run, coins) : BossCoins::Wallet::AwardStatus::Rejected;
            if (status == BossCoins::Wallet::AwardStatus::Paid || status == BossCoins::Wallet::AwardStatus::AlreadyPaid)
            {
                // A crash/failure here leaves intent pending. The wallet's exact
                // receipt acknowledges the retry without another credit.
                CharacterDatabase.DirectExecute(CompletionAwardAcknowledge({run, guid, account, coins}));
            }
            else if (status == BossCoins::Wallet::AwardStatus::Rejected || status == BossCoins::Wallet::AwardStatus::Uncertain)
            {
                std::lock_guard<std::mutex> guard(deliveryLock);
                if (quarantined.insert(guid).second)
                    LOG_ERROR("module.delves", "Delve award run {} member {} stopped for investigation; "
                        "intent retained, status {}", run, guid, static_cast<uint32>(status));
            }
#endif
        }

        class RewardPlayerScript : public PlayerScript
        {
        public:
            RewardPlayerScript() : PlayerScript("evermore_delve_reward_delivery",
                {PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT}) { }
            void OnPlayerUpdate(Player* player, uint32 diff) override { Deliver(player, diff); }
            void OnPlayerLogout(Player* player) override
            {
                if (!player)
                    return;
                uint64 const guid = player->GetGUID().GetRawValue();
                if (guid > std::numeric_limits<uint32>::max())
                    return;
                std::lock_guard<std::mutex> guard(deliveryLock);
                elapsed.erase(static_cast<uint32>(guid));
                // Rejected/uncertain intent remains quarantined until controlled
                // startup recovery. Relogging cannot bypass a wallet freeze.
            }
        };
    }

    bool InitializeDelveRewards()
    {
        if (!sConfigMgr->GetOption<bool>("Delves.Prototype.Rewards.Enable", false))
            return true;
#ifdef EVERMORE_HAS_DURABLE_WALLET
        enabled = true;
        return true;
#else
        LOG_ERROR("module.delves", "Delve rewards require the reviewed durable mod-boss-coins API; pilot not armed");
        return false;
#endif
    }

    bool RewardsAdmissionAllowed(Player* player)
    {
#ifdef EVERMORE_HAS_DURABLE_WALLET
        return !enabled || (MemberId(player) && !BossCoins::IsSelfFound(MemberId(player)));
#else
        return !enabled;
#endif
    }

    void AddDelveRewardDelivery() { new RewardPlayerScript(); }
}
