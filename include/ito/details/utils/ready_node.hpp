#pragma once

#include <ito/details/utils/intrusive_queue.hpp>

namespace ito::details::utils
{
    // An entry in the loop's ready queue: a coroutine frame (its promise) or a scheduled callback. Whoever owns the
    // node owns its lifetime; destroying it unlinks it, so the loop never sees a dead entry. The loop unlinks a node
    // before calling run(), so run() may queue it again.
    class intrusive_ready_node : public utils::intrusive_queue_node
    {
    public:
        intrusive_ready_node(const intrusive_ready_node&)            = delete;
        intrusive_ready_node& operator=(const intrusive_ready_node&) = delete;
        intrusive_ready_node& operator=(intrusive_ready_node&&)      = delete;

        virtual void run() noexcept = 0;

    protected:
        intrusive_ready_node() noexcept                       = default;
        intrusive_ready_node(intrusive_ready_node&&) noexcept = default;

        // never destroyed through this base, so the destructor needn't be virtual
        ~intrusive_ready_node() noexcept = default;
    };
} // namespace ito::details::utils
