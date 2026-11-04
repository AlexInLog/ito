#pragma once

#include <concepts>
#include <utility>

namespace ito::details::utils
{
    template<typename T>
    class trackable
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
        ~trackable() noexcept
        {
            if (m_view)
                m_view->m_object = nullptr;
        }

        trackable(trackable&& o) noexcept
            : m_object(std::move(o.m_object))
            , m_view(std::exchange(o.m_view, nullptr))
        {
            if (m_view)
                m_view->m_object = this;
        }

        trackable(const trackable&)            = delete;
        trackable& operator=(const trackable&) = delete;
        trackable& operator=(trackable&& o) noexcept
        {
            m_object = std::move(o.m_object);
            if (m_view)
                m_view->m_object = nullptr;

            m_view = std::exchange(o.m_view, nullptr);
            if (m_view)
                m_view->m_object = this;

            return *this;
        }

        [[nodiscard]] T&       get() { return m_object; }
        [[nodiscard]] const T& get() const { return m_object; }
        [[nodiscard]] T*       operator->() { return &m_object; }
        [[nodiscard]] const T* operator->() const { return &m_object; }

        class weak_ref
        {
            explicit weak_ref(trackable<T>& object)
                : m_object{&object}
            {
                m_object->m_view = this;
            }

        public:
            friend class trackable<T>;

            ~weak_ref() noexcept
            {
                if (m_object)
                    m_object->m_view = nullptr;
            }

            weak_ref(weak_ref&& o) noexcept
                : m_object(std::exchange(o.m_object, nullptr))
            {
                if (m_object)
                    m_object->m_view = this;
            }

            weak_ref(const weak_ref&)            = delete;
            weak_ref& operator=(const weak_ref&) = delete;
            weak_ref& operator=(weak_ref&& o) noexcept
            {
                if (m_object)
                    m_object->m_view = nullptr;

                m_object = std::exchange(o.m_object, nullptr);
                if (m_object)
                    m_object->m_view = this;

                return *this;
            }

            [[nodiscard]] T* get() const { return m_object ? &m_object->get() : nullptr; }

        private:
            trackable<T>* m_object{};
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
        T         m_object;
        weak_ref* m_view{};
    };
} // namespace ito::details::utils
