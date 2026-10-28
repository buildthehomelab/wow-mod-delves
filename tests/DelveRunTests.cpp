#include "DelveRun.h"
#include "ConfirmedRun.h"
#include "DelveScaling.h"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace Evermore::Delves;

namespace
{
    Definition Story()
    {
        // Synthetic actor entries: this verifies the engine, not retail encounter data.
        return {1, 2680, {{EventKind::Interact, 100, 2}, {EventKind::Kill, 200, 1}}};
    }

    Run Start(std::uint64_t id = 1, std::uint32_t instance = 10)
    {
        return Run(id, instance, 1, Story(), {{1, 10, 20, 0}, {2, 20, 60, 3}});
    }

    void Check(bool condition, char const* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void Reject(std::function<void()> const& action)
    {
        try
        {
            action();
        }
        catch (std::invalid_argument const&)
        {
            return;
        }
        throw std::runtime_error("invalid input accepted");
    }

    void RecoveryAndRewards()
    {
        auto run = Start();
        Check(run.RewardKey(1).empty(), "early reward");
        Event first{2680, 10, 1, EventKind::Interact, 100, 1000};
        Check(run.Credit(first), "first rescue rejected");
        auto recovered = Run::Load(run.Save(), Story());
        Check(!recovered.Credit(first), "duplicate credit after recovery");
        Check(recovered.Credit({2680, 10, 2, EventKind::Interact, 100, 1001}), "second rescue rejected");
        Check(recovered.Stage() == 1, "stage did not advance");
        Check(recovered.Credit({2680, 10, 1, EventKind::Kill, 200, 2000}), "boss rejected");
        Check(recovered.Status() == RunStatus::Complete, "run incomplete");
        auto complete = Run::Load(recovered.Save(), Story());
        Check(complete.RewardKey(1) == "delve:1:1", "unstable reward key");
        Check(complete.RewardKey(2) == "delve:1:2", "missing personal reward key");
        Check(complete.RewardKey(3).empty(), "outsider reward");
        Check(!complete.Credit(first) && !complete.Abandon(), "terminal run changed");
    }

    void EncounterUnlockRecovery()
    {
        auto run = Start();
        Check(!run.IsCurrentObjective(EventKind::Kill, 200), "boss unlocked before objectives");
        run.Credit({2680, 10, 1, EventKind::Interact, 100, 1000});
        auto partial = Run::Load(run.Save(), Story());
        Check(!partial.IsCurrentObjective(EventKind::Kill, 200), "partial rescue unlocked boss");
        partial.Credit({2680, 10, 1, EventKind::Interact, 100, 1001});
        auto unlocked = Run::Load(partial.Save(), Story());
        Check(unlocked.IsCurrentObjective(EventKind::Kill, 200), "recovery lost boss unlock");
        Check(!unlocked.IsCurrentObjective(EventKind::Kill, 201), "wrong boss unlocked");
        unlocked.Credit({2680, 10, 1, EventKind::Kill, 200, 2000});
        Check(!unlocked.IsCurrentObjective(EventKind::Kill, 200), "terminal run allows boss combat");
        partial.Abandon();
        Check(!partial.IsCurrentObjective(EventKind::Kill, 200), "abandoned run allows boss combat");
    }

    void Isolation()
    {
        auto run = Start();
        Check(!run.Credit({2680, 11, 1, EventKind::Interact, 100, 1000}), "cross-instance credit");
        Check(!run.Credit({1, 10, 1, EventKind::Interact, 100, 1000}), "cross-map credit");
        Check(!run.Credit({2680, 10, 3, EventKind::Interact, 100, 1000}), "outsider credit");
        Check(!run.Credit({2680, 10, 1, EventKind::Kill, 200, 2000}), "out-of-order boss credit");
        Check(!run.Credit({2680, 10, 1, EventKind::Interact, 100, 0}), "zero actor credit");
        Check(run.Progress() == 0, "rejected event mutated progress");
        auto other = Start(2, 11);
        Check(other.Credit({2680, 11, 1, EventKind::Interact, 100, 1000}), "other run blocked");
        Check(run.Progress() == 0, "other run mutated this run");
        Check(run.Abandon(), "abandon rejected");
        auto recovered = Run::Load(run.Save(), Story());
        Check(recovered.Status() == RunStatus::Abandoned && recovered.RewardKey(1).empty(), "abandon lost");
        Check(!recovered.Credit({2680, 10, 1, EventKind::Interact, 100, 1000}), "abandoned credit");
    }

    void ConfirmedCheckpoints()
    {
        auto initial = Start();
        auto durable = initial.Save();
        bool acknowledge = true;
        unsigned writes = 0;
        auto writer = [&](std::string const& oldState, std::string const& newState)
        {
            ++writes;
            if (durable != oldState)
                return false;
            // Simulate a committed write whose acknowledgement is lost.
            durable = newState;
            return acknowledge;
        };
        ConfirmedRun run(initial, writer);
        Event first{2680, 10, 1, EventKind::Interact, 100, 1000};
        Check(run.Credit(first), "confirmed checkpoint rejected");
        Check(!run.Credit(first) && writes == 1, "duplicate wrote checkpoint");
        auto const before = run.Current().Save();
        acknowledge = false;
        Check(!run.Credit({2680, 10, 1, EventKind::Interact, 100, 1001}), "uncertain write acknowledged");
        Check(run.Blocked() && run.Current().Save() == before, "uncertain state published");
        Check(!run.Abandon() && writes == 2, "frozen session wrote again");
        ConfirmedRun recovered(Run::Load(durable, Story()), writer);
        acknowledge = true;
        Check(recovered.Current().Stage() == 1, "committed uncertain write not recovered");
        Check(recovered.Credit({2680, 10, 1, EventKind::Kill, 200, 2000}), "completion checkpoint failed");
        Check(recovered.Current().Status() == RunStatus::Complete, "confirmed completion missing");

        ConfirmedRun stale(initial, writer);
        Check(!stale.Abandon() && stale.Blocked(), "stale checkpoint overwrote newer state");
        Check(Run::Load(durable, Story()).Status() == RunStatus::Complete, "stale writer corrupted checkpoint");
    }

    void ScalingPolicy()
    {
        Check(!MakeScaleProfile(61, 7, 1), "Classic cap bypass");
        Check(!MakeScaleProfile(71, 12, 1), "TBC cap bypass");
        Check(!MakeScaleProfile(81, 18, 1), "realm level cap bypass");
        Check(!MakeScaleProfile(9, 0, 1), "minimum level bypass");
        Check(!MakeScaleProfile(60, 4294967295u, 1), "unknown progression treated as Classic");
        Check(!MakeScaleProfile(60, 19, 1), "unknown future progression accepted");
        auto classic = MakeScaleProfile(60, 7, 1);
        auto tbc = MakeScaleProfile(70, 8, 2);
        auto wrath = MakeScaleProfile(80, 13, 3);
        auto alt = MakeScaleProfile(20, 18, 1);
        Check(classic && classic->expansion == 0 && classic->healthPercent == 100, "Classic baseline invalid");
        Check(tbc && tbc->expansion == 1 && tbc->healthPercent > classic->healthPercent, "TBC tier invalid");
        Check(wrath && wrath->expansion == 2 && wrath->damagePercent > tbc->damagePercent, "Wrath tier invalid");
        Check(alt && alt->expansion == 0, "low-level alt used high-era stats");
        Check(!MakeScaleProfile(20, 0, 0) && !MakeScaleProfile(20, 0, 12), "tier bound bypass");
        Check(ScaleAmount(101, 135) == 137, "scaling did not round upwards");
        Check(!ScaleAmount(4294967295u, 450), "scaled health overflow");
        Check(!ScaleAmount(0, 100), "zero health accepted");
        Run scaled(4, 7, 3, Story(), {{1, 10, 80, 18}});
        auto loaded = Run::Load(scaled.Save(), Story());
        Check(loaded.Tier() == 3 && loaded.Members()[0].level == 80 && loaded.Members()[0].progression == 18,
            "scaling inputs changed after restart");
    }

    void Corruption()
    {
        auto run = Start();
        auto const save = run.Save();
        Reject([&] { Run::Load(save + " garbage", Story()); });
        Reject([&] { Run::Load(save.substr(0, save.size() - 2), Story()); });
        Reject([&] { Run::Load("EVD1 -1 10 1", Story()); });
        Reject([&] { Run::Load(std::string(32769, '0'), Story()); });
        auto changed = Story();
        changed.revision = 2;
        Reject([&] { Run::Load(save, changed); });
        Reject([&] { Run(1, 0, 1, Story(), {{1, 1, 20, 0}}); });
        Reject([&] { Run(1, 1, 12, Story(), {{1, 1, 20, 0}}); });
        Reject([&] { Run(1, 1, 1, Story(), {{1, 1, 20, 0}, {1, 2, 60, 3}}); });
        // Complete status with an active stage, and a valid active stage with
        // a fabricated credit count, must both fail closed.
        Reject([&] { Run::Load("EVD1 1 10 1 1 2680 2 0 0 1 1 10 20 0 0", Story()); });
        Reject([&] { Run::Load("EVD1 1 10 1 1 2680 1 0 1 1 1 10 20 0 0", Story()); });
        Reject([&] { Run::Load("EVD1 1 10 1 1 2680 1 0 0 1 4294967296 10 20 0 0", Story()); });
    }
}

int main()
{
    try
    {
        RecoveryAndRewards();
        Isolation();
        EncounterUnlockRecovery();
        Corruption();
        ScalingPolicy();
        ConfirmedCheckpoints();
        std::cout << "delve run recovery, isolation and corruption checks passed\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
