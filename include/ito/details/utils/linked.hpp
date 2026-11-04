#pragma once

#include <utility>

namespace ito::details::utils
{
    // One end of an intrusive link between exactly two objects: each end points at the other, a move re-points the
    // other end at the new location, and destroying either end nulls the other's pointer. Invariant: both pointers are
    // null, or they point at each other. Meant as a private base; the derived classes decide what each end holds.
    //
    // The ends have roles. An owner (link_owner) starts unlinked and is only default-constructible; a follower
    // (link_follower) can only be created from an owner, which links the two. So a link is only ever made from a
    // fresh owner, never from an end that's already linked elsewhere.
    //
    // The two ends are distinct types on purpose. With one symmetric type, both pointers are the
    // same field of the same type, so the compiler must assume `m_other->m_other = this` may overwrite this->m_other
    // too, and it can no longer see through a create/move/use sequence: that cost ~3x the instructions there, and
    // 5-14 extra instructions per task or future. Distinct types let type-based alias analysis rule that out.
    template<typename Tag, int Side>
    class linked
    {
        static_assert(Side == 0 || Side == 1);

        using other_t = linked<Tag, 1 - Side>;
        friend other_t;

    public:
        linked(const linked&)            = delete;
        linked& operator=(const linked&) = delete;

    protected:
        // An owner starts unlinked.
        linked() noexcept
            requires (Side == 0)
        = default;

        // A follower is created linked to `owner`, which must not have a follower yet. `owner` is written through
        // m_other by assign_self(), which cppcheck doesn't follow.
        // cppcheck-suppress constParameterReference
        explicit linked(other_t& owner) noexcept
            requires (Side == 1)
            : m_other{&owner}
        {
            assign_self();
        }

        ~linked() noexcept { remove_self(); }

        linked(linked&& o) noexcept
            : m_other{std::exchange(o.m_other, nullptr)}
        {
            assign_self();
        }

        linked& operator=(linked&& o) noexcept
        {
            remove_self();
            m_other = std::exchange(o.m_other, nullptr);
            assign_self();
            return *this;
        }

        // The other end, or null once it's gone.
        [[nodiscard]] other_t* other() const noexcept { return m_other; }

    private:
        void remove_self() noexcept
        {
            if (m_other) [[likely]]
                m_other->m_other = nullptr;
        }

        void assign_self() noexcept
        {
            if (m_other) [[likely]]
                m_other->m_other = this;
        }

    private:
        other_t* m_other{};
    };

    // Tag is an empty type naming one kind of link (e.g. `struct trackable_link_tag;`), so the ends of different kinds
    // of links are distinct types too: a function touching two kinds of links can then keep both in registers. It's one
    // tag per kind of link, not per T, and every member is inlined, so the extra instantiations cost nothing in code.
    template<typename Tag>
    using link_owner = linked<Tag, 0>;

    template<typename Tag>
    using link_follower = linked<Tag, 1>;
} // namespace ito::details::utils
