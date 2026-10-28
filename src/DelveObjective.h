#ifndef EVERMORE_DELVE_OBJECTIVE_H
#define EVERMORE_DELVE_OBJECTIVE_H

#include "DelveRun.h"

namespace Evermore::Delves
{
    // Read-only guidance from a confirmed checkpoint. Template names are resolved
    // by the server adapter; the formatter also works when a template is absent.
    inline std::string DescribeObjective(Run const& run, std::string targetName, bool blocked = false)
    {
        if (blocked)
            return "Delve checkpoint blocked. Progress cannot be confirmed; use .delve leave to return safely.";
        if (run.Status() == RunStatus::Complete)
            return "Delve complete. Use .delve status to check reward delivery or .delve leave to return.";
        if (run.Status() == RunStatus::Abandoned)
            return "Delve abandoned. Use .delve leave to return; another attempt needs a fresh instance.";
        auto const objective = run.CurrentObjective();
        if (!objective)
            return "Delve objective unavailable. Use .delve status to inspect the checkpoint.";
        if (targetName.empty())
            targetName = std::string(objective->kind == EventKind::Kill ? "creature " : "object ") +
                std::to_string(objective->entry);
        return "Objective " + std::to_string(run.Stage() + 1) + "/" + std::to_string(run.ObjectiveCount()) +
            ": " + (objective->kind == EventKind::Kill ? "Defeat " : "Interact with ") + targetName +
            " (" + std::to_string(run.Progress()) + "/" + std::to_string(objective->required) + ").";
    }
}
#endif
