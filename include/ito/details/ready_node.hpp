#pragma once

#include <ito/details/utils/intrusive_queue.hpp>

namespace ito::details
{
    // An entry in the loop's ready queue: a coroutine frame (its promise) or a scheduled callback. Whoever owns the
    // node owns its lifetime; destroying it unlinks it, so the loop never sees a dead entry. The loop unlinks a node
    // before calling run(), so run() may queue it again.
    class ready_node : public utils::intrusive_queue_node
    {
    public:
        ready_node(const ready_node&)            = delete;
        ready_node& operator=(const ready_node&) = delete;
        ready_node& operator=(ready_node&&)      = delete;

        virtual void run() noexcept = 0;

    protected:
        ready_node() noexcept             = default;
        ready_node(ready_node&&) noexcept = default;
        // never destroyed through this base, so the destructor needn't be virtual
        ~ready_node() noexcept = default;
    };
} // namespace ito::details
