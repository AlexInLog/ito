#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace ito::details::utils
{
    // A move-only `void() noexcept` callable. Unlike std::function it accepts move-only callables, so a queued
    // callback can own what it touches (e.g. a promise) instead of referencing it, and the no-throw requirement
    // is part of the type. Small nothrow-movable callables are stored inline, others on the heap.
    class move_only_callback
    {
        struct alignas(void*) storage
        {
            std::array<std::byte, 2 * sizeof(void*)> bytes;
        };

        template<typename F>
        static constexpr bool fits_inline() noexcept
        {
            return sizeof(F) <= sizeof(storage) && alignof(F) <= alignof(storage) && std::is_nothrow_move_constructible_v<F>;
        }

        // Heap fallback for callables that don't fit inline: owns the callable and forwards the call, so every stored
        // type is constructible from the callable and invocable, and the operations below handle both paths alike.
        template<typename F>
        struct heap_stored
        {
            template<typename U>
                requires (!std::same_as<std::remove_cvref_t<U>, heap_stored>)
            explicit heap_stored(U&& f)
                : ptr{std::make_unique<F>(std::forward<U>(f))}
            {
            }

            void operator()() noexcept { (*ptr)(); }

            std::unique_ptr<F> ptr;
        };

        template<typename F>
        using stored_t = std::conditional_t<fits_inline<F>(), F, heap_stored<F>>;

        template<typename T>
        static T& access(storage& s) noexcept
        {
            return *std::launder(reinterpret_cast<T*>(s.bytes.data())); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        }

        template<typename F>
        static void invoke(storage& s) noexcept
        {
            access<stored_t<F>>(s)();
        }

        // Moves `self` into `dst` and destroys `self`, or only destroys `self` when `dst` is null.
        template<typename F>
        static void manage(storage& self, storage* dst) noexcept
        {
            auto& stored = access<stored_t<F>>(self);
            if (dst)
                ::new (static_cast<void*>(dst->bytes.data())) stored_t<F>(std::move(stored));
            std::destroy_at(&stored);
        }

        using invoke_fn = void (*)(storage&) noexcept;
        using manage_fn = void (*)(storage&, storage*) noexcept;

    public:
        template<typename F>
            requires (!std::same_as<std::remove_cvref_t<F>, move_only_callback>)
                      && std::is_nothrow_invocable_v<std::decay_t<F>&> && std::constructible_from<std::decay_t<F>, F&&>
        explicit move_only_callback(F&& f)
            : m_invoke{&invoke<std::decay_t<F>>}
            , m_manage{&manage<std::decay_t<F>>}
        {
            static_assert(fits_inline<stored_t<std::decay_t<F>>>(), "the heap fallback must itself fit inline");
            ::new (static_cast<void*>(m_storage.bytes.data())) stored_t<std::decay_t<F>>(std::forward<F>(f));
        }

        move_only_callback(move_only_callback&& o) noexcept
            : m_invoke{std::exchange(o.m_invoke, nullptr)}
            , m_manage{std::exchange(o.m_manage, nullptr)}
        {
            if (m_manage)
                m_manage(o.m_storage, &m_storage);
        }

        move_only_callback(const move_only_callback&)            = delete;
        move_only_callback& operator=(const move_only_callback&) = delete;
        move_only_callback& operator=(move_only_callback&&)      = delete;

        ~move_only_callback() noexcept
        {
            if (m_manage)
                m_manage(m_storage, nullptr);
        }

        void operator()() noexcept { m_invoke(m_storage); }

    private:
        storage   m_storage{};
        invoke_fn m_invoke{};
        manage_fn m_manage{};
    };
} // namespace ito::details::utils
