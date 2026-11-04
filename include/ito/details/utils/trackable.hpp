#pragma once

#include <ito/details/utils/linked.hpp>

#include <concepts>
#include <utility>

namespace ito::details::utils
{
    // Owns a T and pairs it with a single weak_ref: the trackable is the link_owner, its weak_ref the link_follower (see
    // linked.hpp). Moving either end re-points the other, and destroying the owner makes the weak_ref see nothing. No
    // special members are declared on purpose (rule of zero): the defaulted ones move the link (which re-points) and T as
    // usual, and copying is deleted through the link.

    struct trackable_link_tag;

    template<typename T>
    class trackable : private link_owner<trackable_link_tag>
    {
        struct inplace
        {
        };

        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        explicit trackable(inplace, Args&&... args)
            : m_object(std::forward<Args>(args)...)
        {
        }

    public:
        [[nodiscard]] T&       get() { return m_object; }
        [[nodiscard]] const T& get() const { return m_object; }
        [[nodiscard]] T*       operator->() { return &m_object; }
        [[nodiscard]] const T* operator->() const { return &m_object; }

        class weak_ref : private link_follower<trackable_link_tag>
        {
            // As a nested class, weak_ref may convert a trackable to its private `linked` base and back.
            explicit weak_ref(trackable& object)
                : link_follower<trackable_link_tag>{static_cast<link_owner<trackable_link_tag>&>(object)}
            {
            }

        public:
            friend class trackable<T>;

            // cppcheck doesn't resolve other() through the templated link_follower base and calls this static-able
            // cppcheck-suppress functionStatic
            [[nodiscard]] T* get() const
            {
                auto* const owner = static_cast<trackable*>(this->other());
                return owner ? &owner->get() : nullptr;
            }
        };

        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        static std::pair<trackable<T>, weak_ref> create(Args&&... args)
        {
            trackable<T> obj{trackable<T>::inplace{}, std::forward<Args>(args)...};
            weak_ref     v{obj};

            return {std::move(obj), std::move(v)};
        }

    private:
        T m_object;
    };
} // namespace ito::details::utils
