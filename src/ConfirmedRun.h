#ifndef EVERMORE_CONFIRMED_RUN_H
#define EVERMORE_CONFIRMED_RUN_H

#include "DelveRun.h"
#include <functional>
#include <utility>

namespace Evermore::Delves
{
    // The writer must compare the complete old checkpoint and acknowledge the
    // complete new checkpoint. An uncertain write freezes this loaded session.
    class ConfirmedRun
    {
    public:
        using Writer = std::function<bool(std::string const&, std::string const&)>;
        ConfirmedRun(Run run, Writer writer) : _run(std::move(run)), _writer(std::move(writer)) { }
        Run const& Current() const { return _run; }
        bool Blocked() const { return _blocked; }
        bool Credit(Event const& event) { return Change([&event](Run& run) { return run.Credit(event); }); }
        bool Abandon() { return Change([](Run& run) { return run.Abandon(); }); }

    private:
        bool Change(std::function<bool(Run&)> const& mutate)
        {
            if (_blocked)
                return false;
            Run candidate = _run;
            if (!mutate(candidate))
                return false;
            if (!_writer(_run.Save(), candidate.Save()))
            {
                _blocked = true;
                return false;
            }
            _run = std::move(candidate);
            return true;
        }
        Run _run;
        Writer _writer;
        bool _blocked = false;
    };
}
#endif
