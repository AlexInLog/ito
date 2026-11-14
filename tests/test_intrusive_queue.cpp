#include <catch2/catch_test_macros.hpp>
#include <ito/details/utils/intrusive_queue.hpp>

#include <optional>

struct node : public ito::details::utils::intrusive_queue_node
{
public:
    explicit node(size_t v)
        : value{v}
    {
    }
    size_t value{};
};

TEST_CASE("intrusive_queue base checks")
{
    ito::details::utils::intrusive_queue<node> queue{};
    REQUIRE(queue.empty());

    SECTION("adding value makes queue not empty")
    {
        {
            auto v1 = node{1};
            queue.push_back(v1);

            REQUIRE(!queue.empty());

            SECTION("pop_front returns this value")
            {
                REQUIRE(&queue.pop_front() == &v1);
                REQUIRE(queue.empty());
            }
        }
        SECTION("destrution of node empties queue")
        {
            REQUIRE(queue.empty());
        }
    }

    REQUIRE(queue.empty());

    SECTION("adding 3 values")
    {
        auto test = [&](size_t index_to_delete) {
            std::array<std::optional<node>, 3> vals{node{1}, node{2}, node{3}};
            for (auto& v : vals) queue.push_back(v.value());
            SECTION("destruction of " + std::to_string(index_to_delete) + "th node removes it")
            {
                REQUIRE(!queue.empty());
                vals.at(index_to_delete).reset();
                REQUIRE(!queue.empty());
                for (size_t i = 0; i < vals.size(); ++i)
                {
                    if (i != index_to_delete)
                        REQUIRE(&queue.pop_front() == &vals.at(i).value());
                }
                REQUIRE(queue.empty());
            }
        };
        for (size_t i = 0; i < 3; ++i)
        {
            test(i);
        }
    }
}

TEST_CASE("intrusive queue can be moved")
{
    ito::details::utils::intrusive_queue<node> q{};
    node                                       v1{1};
    node                                       v2{2};
    q.push_back(v1);
    q.push_back(v2);

    auto new_q = std::move(q);
    REQUIRE(q.empty()); // NOLINT(bugprone-use-after-move)
    REQUIRE(!new_q.empty());
    REQUIRE(&new_q.pop_front() == &v1);
    REQUIRE(&new_q.pop_front() == &v2);
}
