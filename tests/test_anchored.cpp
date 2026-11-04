#include <catch2/catch_test_macros.hpp>
#include <ito/details/utils/anchored.hpp>

#include <optional>
#include <utility>
#include <vector>

using ito::details::utils::anchor;
using ito::details::utils::anchored;

TEST_CASE("anchored value is usable only while its anchor exists")
{
    SECTION("get() returns the value while the anchor is alive")
    {
        auto [value, anchor_end] = anchored<int>::create(42);
        REQUIRE(value.get() != nullptr);
        REQUIRE(*value.get() == 42);
    }
    SECTION("get() returns null once the anchor is destroyed")
    {
        auto [value, anchor_end] = anchored<int>::create(42);
        {
            [[maybe_unused]] const auto dropped = std::move(anchor_end);
        }
        REQUIRE(value.get() == nullptr);
    }
    SECTION("destroying the value first leaves the anchor harmless")
    {
        auto [value, anchor_end] = anchored<int>::create(42);
        {
            [[maybe_unused]] const auto dropped = std::move(value);
        }
        // anchor_end is destroyed at the end of this section; ASan would catch a write through a stale pointer
        SUCCEED();
    }
}

TEST_CASE("a default-constructed anchored is empty")
{
    anchored<int> empty{};
    REQUIRE(empty.get() == nullptr);

    SECTION("move-assigning a linked value into it links it")
    {
        auto [value, anchor_end] = anchored<int>::create(5);
        empty                    = std::move(value);
        REQUIRE(empty.get() != nullptr);
        REQUIRE(*empty.get() == 5);
    }
}

TEST_CASE("anchored keeps its link across moves")
{
    SECTION("moving the value re-points the link")
    {
        auto [value, anchor_end] = anchored<int>::create(7);
        auto moved               = std::move(value);
        REQUIRE(moved.get() != nullptr);
        REQUIRE(*moved.get() == 7);

        {
            [[maybe_unused]] const auto dropped = std::move(anchor_end);
        }
        REQUIRE(moved.get() == nullptr);
    }
    SECTION("a moved-from value is unlinked")
    {
        auto [value, anchor_end]             = anchored<int>::create(7);
        [[maybe_unused]] const auto moved_to = std::move(value);
        REQUIRE(value.get() == nullptr); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    }
    SECTION("moving the anchor keeps the value usable until the new anchor dies, not the moved-from one")
    {
        auto [value, anchor_end] = anchored<int>::create(7);
        std::optional<anchor> moved{std::move(anchor_end)};
        REQUIRE(value.get() != nullptr);

        moved.reset();
        // the moved-from anchor_end is still alive here, but no longer linked
        REQUIRE(value.get() == nullptr);
    }
    SECTION("move-assigning a value takes over the source's anchor and drops its own")
    {
        auto [value, anchor_end]         = anchored<int>::create(7);
        auto [other_value, other_anchor] = anchored<int>::create(9);

        other_value = std::move(value);
        REQUIRE(other_value.get() != nullptr);
        REQUIRE(*other_value.get() == 7);

        // other_anchor lost its value, so destroying it changes nothing for the reassigned value
        {
            [[maybe_unused]] const auto dropped = std::move(other_anchor);
        }
        REQUIRE(other_value.get() != nullptr);

        {
            [[maybe_unused]] const auto dropped = std::move(anchor_end);
        }
        REQUIRE(other_value.get() == nullptr);
    }
    SECTION("move-assigning an anchor drops the link it had and takes over the source's")
    {
        auto [value, anchor_end]         = anchored<int>::create(7);
        auto [other_value, other_anchor] = anchored<int>::create(9);

        other_anchor = std::move(anchor_end);
        REQUIRE(other_value.get() == nullptr); // its anchor now belongs to `value`
        REQUIRE(value.get() != nullptr);

        {
            [[maybe_unused]] const auto dropped = std::move(other_anchor);
        }
        REQUIRE(value.get() == nullptr);
    }
}

TEST_CASE("anchored values survive container reallocation")
{
    // like the loop's queues: values are moved around (vector growth, heap sifts) while their anchors stay put
    std::vector<anchored<int>> values{};
    std::vector<anchor>        anchors{};

    for (int i = 0; i < 64; ++i)
    {
        auto [value, anchor_end] = anchored<int>::create(i);
        values.push_back(std::move(value));
        anchors.push_back(std::move(anchor_end));
    }

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        REQUIRE(values.at(i).get() != nullptr);
        REQUIRE(std::cmp_equal(*values.at(i).get(), i));
    }

    // erase shifts the later anchors down by move-assignment, which must re-point their values
    anchors.erase(anchors.begin() + 10);
    REQUIRE(values.at(10).get() == nullptr);
    REQUIRE(values.at(11).get() != nullptr);
    REQUIRE(*values.at(63).get() == 63);
}
