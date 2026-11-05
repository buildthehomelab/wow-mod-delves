#include "DelveObjective.h"
#include "ConfirmedRun.h"
#include <iostream>
#include <stdexcept>

using namespace Evermore::Delves;

void Check(bool value, char const* why)
{
    if (!value)
        throw std::runtime_error(why);
}

int main()
{
    Definition definition{1, 890, {{EventKind::Interact, 100, 2}, {EventKind::Kill, 200, 1}}};
    Run run(7, 8, 1, definition, {{42, 9, 30, 0}});
    auto const initialSave = run.Save();
    Check(DescribeObjective(run, "Rescue Marker") ==
        "Objective 1/2: Interact with Rescue Marker (0/2).", "initial guidance incorrect");
    Check(run.Save() == initialSave, "reading guidance mutated run");
    auto objective = run.CurrentObjective();
    objective->required = 999;
    Check(run.CurrentObjective()->required == 2, "objective accessor exposed mutable definition");
    Check(DescribeObjective(run, "") == "Objective 1/2: Interact with object 100 (0/2).",
        "missing object template has no fallback");
    Check(run.Credit({890, 8, 42, EventKind::Interact, 100, 1}), "first rescue rejected");
    auto restored = Run::Load(run.Save(), definition);
    Check(DescribeObjective(restored, "Rescue Marker") ==
        "Objective 1/2: Interact with Rescue Marker (1/2).", "reconnect lost displayed progress");
    Check(!restored.Credit({890, 8, 42, EventKind::Interact, 100, 1}), "duplicate rescue accepted");
    Check(DescribeObjective(restored, "Rescue Marker").find("(1/2)") != std::string::npos,
        "duplicate credit changed displayed progress");
    Check(restored.Credit({890, 8, 42, EventKind::Interact, 100, 3}), "second rescue rejected");
    Check(DescribeObjective(restored, "Ab'enar") == "Objective 2/2: Defeat Ab'enar (0/1).",
        "stage transition did not identify boss");
    Check(DescribeObjective(restored, "") == "Objective 2/2: Defeat creature 200 (0/1).",
        "missing creature template has no fallback");
    Check(restored.Credit({890, 8, 42, EventKind::Kill, 200, 4}), "boss rejected");
    Check(!restored.CurrentObjective(), "completed run exposes active objective");
    auto const completed = DescribeObjective(Run::Load(restored.Save(), definition), "");
    Check(completed == "Delve complete. Use .delve status to check reward delivery or .delve leave to return.",
        "completion guidance confused completion with reward delivery");
    Check(run.Abandon(), "abandon rejected");
    Check(!run.CurrentObjective(), "abandoned run exposes active objective");
    Check(DescribeObjective(Run::Load(run.Save(), definition), "") ==
        "Delve abandoned. Use .delve leave to return; another attempt needs a fresh instance.",
        "abandon guidance implies restart in terminal instance");

    Run pending(9, 8, 1, definition, {{42, 9, 30, 0}});
    ConfirmedRun failed(pending, [](auto const&, auto const&) { return false; });
    Check(!failed.Credit({890, 8, 42, EventKind::Interact, 100, 1}) && failed.Blocked(),
        "failed persistence was accepted");
    Check(failed.Current().Progress() == 0, "unconfirmed progress exposed");
    Check(DescribeObjective(failed.Current(), "Rescue Marker", failed.Blocked()) ==
        "Delve checkpoint blocked. Progress cannot be confirmed; use .delve leave to return safely.",
        "blocked checkpoint still advertised objective progress");
    ConfirmedRun confirmed(pending, [](auto const&, auto const&) { return true; });
    Check(confirmed.Credit({890, 8, 42, EventKind::Interact, 100, 1}), "confirmed write rejected");
    Check(DescribeObjective(confirmed.Current(), "Rescue Marker").find("(1/2)") != std::string::npos,
        "confirmed progress not displayed");
    Check(DescribeObjective(restored, "", true).find("checkpoint blocked") != std::string::npos,
        "quarantined complete run still advertises completion");
    std::cout << "objective guidance, recovery, duplicate and persistence-failure checks passed\n";
}
