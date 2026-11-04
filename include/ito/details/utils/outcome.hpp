#pragma once

#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/void.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>

#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace ito::details::utils
{
    // The failure paths live in their own functions: constructing and throwing the exception is most of
    // the code of the accessors below, and having it there is what keeps those accessors from being
    // inlined into their callers, even though it only ever runs when something went wrong.
    [[noreturn]] inline void throw_empty_value()
    {
        throw ito::exceptions::empty_value{"empty value"};
    }

    [[noreturn]] inline void throw_value_taken()
    {
        throw ito::exceptions::empty_value{"value already taken"};
    }

    [[noreturn]] inline void throw_value_is_set()
    {
        throw ito::exceptions::value_is_set{"value is already set"};
    }

    // Reports why a result could not be handed out: rethrows the stored exception, reports that it was already
    // taken, or reports the never-set state (which is also how a valueless_by_exception state is reported,
    // consistently with outcome::is_settled()).
    template<typename Storage>
    [[noreturn]] void rethrow_or_throw_empty(const Storage& value)
    {
        if (const auto err = std::get_if<2>(&value)) [[likely]]
            std::rethrow_exception(*err);
        if (value.index() == 3)
            throw_value_taken();

        throw_empty_value();
    }

    template<typename T>
    class outcome
    {
        // Set once and already handed out by get_result(): distinct from monostate (never set), so the outcome still
        // counts as settled and can't be set again. Its own type rather than void_t, which is already index 1 for void.
        struct moved_out_t
        {
        };

    public:
        using stored_t = std::conditional_t<std::is_void_v<T>, void_t, T>;

        void set_exception(const std::exception_ptr& err)
        {
            ensure_not_set();
            m_value.template emplace<2>(err);
        }

        // For a coroutine's unhandled_exception(): the body didn't complete normally, so the exception wins even over a
        // value already returned, since a local's destructor can throw after return_value(). Checking here instead
        // would throw value_is_set out of unhandled_exception(), and so out of resume().
        void replace_with_exception(const std::exception_ptr& err) { m_value.template emplace<2>(err); }

        // A result (value or exception) is there to be taken.
        [[nodiscard]] bool is_ready() const { return m_value.index() == 1 || m_value.index() == 2; }

        // A result was set, whether or not it has been taken since. A valueless_by_exception state (a set_result()
        // whose T constructor threw) counts as never set, so the set can be retried.
        [[nodiscard]] bool is_settled() const { return m_value.index() != 0 && !m_value.valueless_by_exception(); }

        void set_result()
            requires (std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(void_t{});
        }

        void set_result(stored_t&& v)
            requires (!std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(std::move(v));
        }

        void set_result(const stored_t& v)
            requires (!std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(v);
        }

        // Hands the result out once and marks the outcome as taken on every path: the guard does so after the value
        // has been moved into the return object, or while the stored exception unwinds.
        T get_result()
        {
            const auto _ = finally{[this]() noexcept { m_value.template emplace<3>(); }};

            // Deliberately not std::visit(): visiting dispatches through a function table, which neither
            // compiler manages to fold away, while this is the hot path of every awaited future.
            if (const auto value = std::get_if<1>(&m_value)) [[likely]]
            {
                if constexpr (std::is_void_v<T>)
                    return;
                else
                    // clang-analyzer loses track of the variant through trackable's defaulted move and reports an
                    // uninitialized value; index 1 is only ever set by emplace<1>. Same false positive as Sonar's
                    // cpp:S836, excluded in sonar-project.properties.
                    return std::move(*value); // NOLINT(clang-analyzer-core.uninitialized.UndefReturn)
            }

            rethrow_or_throw_empty(m_value);
        }

    private:
        void ensure_not_set()
        {
            if (is_settled()) [[unlikely]]
                throw_value_is_set();
        }

    private:
        std::variant<std::monostate, stored_t, std::exception_ptr, moved_out_t> m_value{};
    };
} // namespace ito::details::utils
