#pragma once

#include <ito/assert.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace ito::details::utils
{
    class intrusive_queue_base;

    class intrusive_queue_node
    {
    public:
        intrusive_queue_node(const intrusive_queue_node&) = delete;
        intrusive_queue_node(intrusive_queue_node&& o) noexcept
        {
            if (!o.is_linked())
                return; // the member initializers already made this node self-linked

            m_next = std::exchange(o.m_next, &o);
            m_prev = std::exchange(o.m_prev, &o);

            m_next->m_prev = m_prev->m_next = this;
        }
        intrusive_queue_node& operator=(const intrusive_queue_node&) = delete;
        intrusive_queue_node& operator=(intrusive_queue_node&&)      = delete;

        intrusive_queue_node() noexcept = default;
        ~intrusive_queue_node() noexcept { unlink(); }

        [[nodiscard]] bool is_linked() const noexcept { return m_next != this; }

        friend class intrusive_queue_base;

    private:
        void unlink() noexcept
        {
            m_next->m_prev = m_prev;
            m_prev->m_next = m_next;
            m_next = m_prev = this;
        }

        void insert_before(intrusive_queue_node& pos) noexcept
        {
            m_next = &pos;
            m_prev = std::exchange(pos.m_prev, this);

            m_prev->m_next = this;
        }

    private:
        // Never null: an unlinked node, like an empty queue's sentinel, points at itself.
        intrusive_queue_node* m_next = this;
        intrusive_queue_node* m_prev = this;
    };

    class intrusive_queue_base
    {
    public:
        intrusive_queue_base(const intrusive_queue_base&)            = delete;
        intrusive_queue_base(intrusive_queue_base&& o) noexcept      = default;
        intrusive_queue_base& operator=(const intrusive_queue_base&) = delete;
        intrusive_queue_base& operator=(intrusive_queue_base&&)      = delete;

        intrusive_queue_base() noexcept = default;
        ~intrusive_queue_base() noexcept
        {
            while (!empty()) pop_front_impl();
        }

        // queue: m_sentinel.m_next is the front, m_sentinel.m_prev the back; empty means the sentinel points at itself
        [[nodiscard]] bool empty() const noexcept { return !m_sentinel.is_linked(); }

    protected:
        void push_back_impl(intrusive_queue_node& n) noexcept
        {
            ITO_ASSERT(!n.is_linked());
            n.insert_before(m_sentinel);
        }

        intrusive_queue_node& pop_front_impl() noexcept
        {
            // on an empty queue this would return the sentinel, which the typed pop_front() casts to a T it isn't
            ITO_ASSERT(!empty());

            auto& n = *m_sentinel.m_next;
            n.unlink();
            return n;
        }

    private:
        intrusive_queue_node m_sentinel{};
    };

    template<std::derived_from<intrusive_queue_node> T>
    class intrusive_queue : public intrusive_queue_base
    {
    public:
        void push_back(T& node) noexcept { intrusive_queue_base::push_back_impl(node); }
        T&   pop_front() noexcept { return static_cast<T&>(intrusive_queue_base::pop_front_impl()); }

        // Pops and passes to fn, in order, only the nodes queued when consume() was called; nodes pushed meanwhile, including a
        // re-pushed one, stay queued for the next call. A node is unlinked before fn sees it, so returning false (or throwing)
        // stops after that node and leaves the rest at the front. fn must not pop, consume or move this queue: the marker
        // would be popped or left behind.
        template<typename Fn>
            requires std::is_invocable_r_v<bool, Fn, T&>
        void consume(Fn&& fn) // NOLINT(cppcoreguidelines-missing-std-forward)
        {
            if (empty())
                return;

            // the marker isn't a T, so compare before casting; on early exit or a throw its destructor unlinks it
            intrusive_queue_node marker{};
            push_back_impl(marker);

            while (true)
            {
                auto& node = pop_front_impl();
                if (&node == &marker)
                    return;

                if (!fn(static_cast<T&>(node)))
                    return;
            }
        }
    };

} // namespace ito::details::utils
