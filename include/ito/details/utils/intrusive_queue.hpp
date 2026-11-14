#pragma once

#include <ito/assert.hpp>

#include <concepts>
#include <cstddef>
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
            if (auto n = std::exchange(o.m_next, {}))
                link_next(n);

            if (auto p = std::exchange(o.m_prev, {}))
                p->link_next(this);
        }

        intrusive_queue_node& operator=(const intrusive_queue_node&)     = delete;
        intrusive_queue_node& operator=(intrusive_queue_node&&) noexcept = delete;

        intrusive_queue_node() = default;
        ~intrusive_queue_node() noexcept { unlink(); }

        friend class intrusive_queue_base;

    private:
        void unlink() noexcept
        {
            if (auto p = m_prev)
                p->link_next(m_next);

            m_next = m_prev = {};
        }

        void link_next(intrusive_queue_node* next) noexcept
        {
            m_next = next;
            if (auto n = m_next)
                n->m_prev = this;
        }

        void insert_before(intrusive_queue_node& next) noexcept
        {
            m_next = &next;
            m_prev = std::exchange(m_next->m_prev, this);
            if (m_prev)
                m_prev->m_next = this;
        }

    private:
        intrusive_queue_node* m_next{};
        intrusive_queue_node* m_prev{};
    };

    class intrusive_queue_base
    {
    public:
        intrusive_queue_base() { m_head.link_next(&m_tail); }

        [[nodiscard]] bool empty() const noexcept { return m_head.m_next == &m_tail; }

    protected:
        void push_back_impl(intrusive_queue_node& node) noexcept { node.insert_before(m_tail); }

        intrusive_queue_node& pop_front_impl() noexcept
        {
            ITO_ASSERT(!empty());

            auto current_next = m_head.m_next;
            current_next->unlink();
            return *current_next;
        }

    private:
        struct bound final : public intrusive_queue_node
        {
        };

        bound m_head{};
        bound m_tail{};
    };

    template<std::derived_from<intrusive_queue_node> T>
    class intrusive_queue : public intrusive_queue_base
    {
    public:
        void push_back(T& node) noexcept { intrusive_queue_base::push_back_impl(node); }
        T&   pop_front() noexcept { return static_cast<T&>(intrusive_queue_base::pop_front_impl()); }
    };


} // namespace ito::details::utils
