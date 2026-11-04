#pragma once

#include <ito/details/utils/linked.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace ito::details::utils
{
    struct anchor_link_tag;

    class anchor : private link_follower<anchor_link_tag>
    {
        template<typename>
        friend class anchored; // befriending a class template is fine

        explicit anchor(link_owner<anchor_link_tag>& owner)
            : link_follower<anchor_link_tag>{owner}
        {
        }
    };

    template<typename T>
    class anchored : private link_owner<anchor_link_tag>
    {
        struct inplace
        {
        };

        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        explicit anchored(inplace, Args&&... args)
            : m_value(std::forward<Args>(args)...)
        {
        }

    public:
        static_assert(std::is_nothrow_move_constructible_v<T>, "T should be nothrow movable inside anchored");

        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        static std::pair<anchored<T>, anchor> create(Args&&... args)
        {
            anchored<T> obj{anchored<T>::inplace{}, std::forward<Args>(args)...};
            anchor      v{static_cast<link_owner<anchor_link_tag>&>(obj)};

            return {std::move(obj), std::move(v)};
        }

        // The value, or null once its anchor is gone.
        [[nodiscard]] T* get() noexcept { return this->other() ? &m_value : nullptr; }

    private:
        T m_value;
    };
} // namespace ito::details::utils
