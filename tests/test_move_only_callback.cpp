#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ito/details/utils/move_only_callback.hpp>

#include <array>
#include <memory>
#include <utility>

namespace
{
    struct small_callback
    {
        std::shared_ptr<int> token;

        void operator()() const noexcept { ++*token; }
    };

    struct big_callback
    {
        std::shared_ptr<int> token;
        std::array<int, 16>  padding{};

        void operator()() const noexcept { *token += 1 + padding.front(); }
    };
} // namespace

TEMPLATE_TEST_CASE("move_only_callback owns its callable across moves", "", small_callback, big_callback)
{
    auto token = std::make_shared<int>(0);

    {
        ito::details::utils::move_only_callback source{TestType{.token = token}};
        REQUIRE(token.use_count() == 2);

        ito::details::utils::move_only_callback target{std::move(source)};
        // the callable moved rather than being copied: exactly one owner besides `token`
        REQUIRE(token.use_count() == 2);

        target();
        REQUIRE(*token == 1);
    }

    // destroying both the moved-from source and the target destroys the callable exactly once
    REQUIRE(token.use_count() == 1);
}
