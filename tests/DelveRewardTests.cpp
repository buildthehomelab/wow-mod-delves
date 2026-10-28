#include "DelveRewards.h"
#include <iostream>
#include <stdexcept>

using namespace Evermore::Delves;

int main(int argc, char**)
{
    Definition story{1, 890, {{EventKind::Kill, 200, 1}}};
    Run run(100, 101, 3, story, {{42, 7, 30, 0}});
    if (!CompletionAwards(run).empty())
        throw std::runtime_error("active run created a reward");
    Run abandoned(101, 102, 3, story, {{42, 7, 30, 0}});
    abandoned.Abandon();
    if (!CompletionAwards(abandoned).empty())
        throw std::runtime_error("abandoned run created a reward");
    run.Credit({890, 101, 42, EventKind::Kill, 200, 88});
    auto recovered = Run::Load(run.Save(), story);
    auto awards = CompletionAwards(recovered);
    if (awards.size() != 1 || awards[0].run != 100 || awards[0].guid != 42 ||
        awards[0].account != 7 || awards[0].coins != 3)
        throw std::runtime_error("completion intent identity/amount changed across recovery");
    if (argc > 1)
        std::cout << CompletionAwardInsert(awards[0], 101, "done") << ";\n";
    else
        std::cout << "Completion rewards require a complete run and preserve frozen identity/tier\n";
}
