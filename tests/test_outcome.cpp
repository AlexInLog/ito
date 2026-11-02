#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("outcome hands its result out once")
{
    SECTION("a value is moved out exactly once and leaves the outcome empty")
    {
        int                                  moves = 0;
        ito::details::utils::outcome<counted> o{};
        o.set_result(counted{&moves});
        moves = 0;

        [[maybe_unused]] const auto taken = o.get_result();
        REQUIRE(moves == 1);
        REQUIRE_FALSE(o.is_ready());
        REQUIRE_THROWS_AS(o.get_result(), ito::exceptions::empty_value);
    }
    SECTION("a stored exception is rethrown once and leaves the outcome empty")
    {
        ito::details::utils::outcome<int> o{};
        o.set_exception(std::make_exception_ptr(std::runtime_error{"failed"}));

        REQUIRE_THROWS_AS(o.get_result(), std::runtime_error);
        REQUIRE_FALSE(o.is_ready());
        REQUIRE_THROWS_AS(o.get_result(), ito::exceptions::empty_value);
    }
    SECTION("void")
    {
        ito::details::utils::outcome<void> o{};
        o.set_result();

        REQUIRE_NOTHROW(o.get_result());
        REQUIRE_FALSE(o.is_ready());
        REQUIRE_THROWS_AS(o.get_result(), ito::exceptions::empty_value);
    }
}
