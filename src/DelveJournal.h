#ifndef EVERMORE_DELVE_JOURNAL_H
#define EVERMORE_DELVE_JOURNAL_H

#include <cstdint>
#include <string>

namespace Evermore::Delves
{
    // Synchronous development journal. The instance ID is unique for the
    // lifetime of a journal row; recycling an ID fails closed, never resets it.
    class Journal
    {
    public:
        explicit Journal(std::uint32_t instance) : _instance(instance) { }
        std::uint64_t Reserve() const;
        std::string Read() const;
        bool Replace(std::string const& expected, std::string const& desired) const;
    private:
        std::uint32_t _instance;
    };
}
#endif
