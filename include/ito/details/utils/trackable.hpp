#pragma once

#include <concepts>
#include <utility>

namespace ito::details::utils
{
    template<typename T>
    class trackable
    {
        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        explicit trackable(Args&&... args)
            : m_object(std::forward<Args>(args)...)
        {
        }

    public:
        ~trackable() noexcept
        {
            if (m_view) m_view->m_object = nullptr;
        }

        trackable(trackable&& o) noexcept
            : m_object(std::move(o.m_object))
            , m_view(std::exchange(o.m_view, nullptr))
        {
            if (m_view) m_view->m_object = this;
        }

        trackable(const trackable&)            = delete;
        trackable& operator=(const trackable&) = delete;
        trackable& operator=(trackable&&)      = delete;

        T& get() { return m_object; }
        T* operator->() { return &m_object; }

        class weak_view
        {
            explicit weak_view(trackable<T>& object)
                : m_object{&object}
            {
                m_object->m_view = this;
            }

        public:
            friend class trackable<T>;

            ~weak_view() noexcept
            {
                if (m_object) m_object->m_view = nullptr;
            }

            weak_view(weak_view&& o) noexcept
                : m_object(std::exchange(o.m_object, nullptr))
            {
                if (m_object) m_object->m_view = this;
            }

            weak_view(const weak_view&)            = delete;
            weak_view& operator=(const weak_view&) = delete;
            weak_view& operator=(weak_view&&)      = delete;

            [[nodiscard]] T* get() const { return m_object ? &m_object->get() : nullptr; }

        private:
            trackable<T>* m_object{};
        };

        template<typename... Args>
            requires std::constructible_from<T, Args&&...>
        static std::pair<trackable<T>, weak_view> create(Args&&... args)
        {
            trackable<T> obj{std::forward<Args>(args)...};
            weak_view    v{obj};

            return {std::move(obj), std::move(v)};
        }

    private:
        T          m_object;
        weak_view* m_view{};
    };
} // namespace ito::details::utils
