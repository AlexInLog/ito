#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <ito/details/utils/outcome.hpp>
#include <ito/exceptions.hpp>

#include <exception>
#include <stdexcept>
#include <utility>

namespace
{
    struct counted
    {
        int* moves;

        explicit counted(int* moves)
            : moves{moves}
        {
        }
        counted(const counted&) = delete;
        counted(counted&& o) noexcept
            : moves{o.moves}
        {
            ++*moves;
        }
        counted& operator=(const counted&) = delete;
        counted& operator=(counted&&)      = delete;
        ~counted()                         = default;
    };
} // namespace

// Taken (set once, already handed out) is distinct from never set: a taken outcome still counts as settled, so it
// can't be set again, and a second get_result() reports that the value was already taken.
template<typename Outcome>
void require_taken(Outcome& o)
{
    REQUIRE_FALSE(o.is_ready());
    REQUIRE(o.is_settled());
    REQUIRE_THROWS_WITH(o.get_result(), "value already taken");
    REQUIRE_THROWS_AS(o.set_exception(std::make_exception_ptr(std::runtime_error{"late"})), ito::exceptions::value_is_set);
}

TEST_CASE("outcome hands its result out once")
{
    SECTION("never set: not ready, not settled, reports an empty value")
    {
        ito::details::utils::outcome<int> o{};
        REQUIRE_FALSE(o.is_ready());
        REQUIRE_FALSE(o.is_settled());
        REQUIRE_THROWS_WITH(o.get_result(), "empty value");
    }
    SECTION("a value is moved out exactly once and marks the outcome as taken")
    {
        int                                   moves = 0;
        ito::details::utils::outcome<counted> o{};
        o.set_result(counted{&moves});
        moves = 0;

        [[maybe_unused]] const auto taken = o.get_result();
        REQUIRE(moves == 1);
        require_taken(o);
    }
    SECTION("a stored exception is rethrown once and marks the outcome as taken")
    {
        ito::details::utils::outcome<int> o{};
        o.set_exception(std::make_exception_ptr(std::runtime_error{"failed"}));

        REQUIRE_THROWS_WITH(o.get_result(), "failed");
        require_taken(o);
    }
    SECTION("void")
    {
        ito::details::utils::outcome<void> o{};
        o.set_result();

        REQUIRE_NOTHROW(o.get_result());
        require_taken(o);
        REQUIRE_THROWS_AS(o.set_result(), ito::exceptions::value_is_set);
    }
}
