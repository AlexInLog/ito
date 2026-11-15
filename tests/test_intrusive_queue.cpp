#include <catch2/catch_test_macros.hpp>
#include <ito/details/utils/intrusive_queue.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>

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
        SECTION("destruction of node empties queue")
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

TEST_CASE("intrusive queue nodes report whether they're linked")
{
    ito::details::utils::intrusive_queue<node> queue{};
    node                                       v1{1};
    REQUIRE_FALSE(v1.is_linked());

    queue.push_back(v1);
    REQUIRE(v1.is_linked());

    SECTION("popping unlinks it, and it can be pushed again")
    {
        REQUIRE(&queue.pop_front() == &v1);
        REQUIRE_FALSE(v1.is_linked());

        queue.push_back(v1);
        REQUIRE(&queue.pop_front() == &v1);
        REQUIRE(queue.empty());
    }
}

TEST_CASE("moving an intrusive queue node")
{
    ito::details::utils::intrusive_queue<node> queue{};

    SECTION("an unlinked node moves to an unlinked node")
    {
        node v1{1};
        node moved{std::move(v1)};
        REQUIRE_FALSE(v1.is_linked()); // NOLINT(bugprone-use-after-move)
        REQUIRE_FALSE(moved.is_linked());
        REQUIRE(queue.empty());
    }
    SECTION("a queued node keeps its place in the queue")
    {
        node v1{1};
        node v2{2};
        node v3{3};
        queue.push_back(v1);
        queue.push_back(v2);
        queue.push_back(v3);

        node moved{std::move(v2)};
        REQUIRE_FALSE(v2.is_linked()); // NOLINT(bugprone-use-after-move)
        REQUIRE(moved.is_linked());

        REQUIRE(&queue.pop_front() == &v1);
        REQUIRE(&queue.pop_front() == &moved);
        REQUIRE(&queue.pop_front() == &v3);
        REQUIRE(queue.empty());
    }
}

TEST_CASE("moving an empty intrusive queue leaves both queues usable")
{
    ito::details::utils::intrusive_queue<node> q{};
    auto                                       new_q = std::move(q);
    REQUIRE(q.empty()); // NOLINT(bugprone-use-after-move)
    REQUIRE(new_q.empty());

    node v1{1};
    new_q.push_back(v1);
    REQUIRE(&new_q.pop_front() == &v1);
    q.push_back(v1); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): a moved-from queue is a valid empty queue
    REQUIRE(&q.pop_front() == &v1);
}

TEST_CASE("an intrusive queue destroyed before its nodes unlinks them")
{
    node v1{1};
    node v2{2};
    {
        ito::details::utils::intrusive_queue<node> queue{};
        queue.push_back(v1);
        queue.push_back(v2);
    }
    // the nodes' own destructors run after this and must not touch the dead queue
    REQUIRE_FALSE(v1.is_linked());
    REQUIRE_FALSE(v2.is_linked());
}

TEST_CASE("a queued node destroyed while another one is being processed leaves the queue")
{
    // the loop's drain: pop a node, run it, and running it may destroy a node still queued behind it
    ito::details::utils::intrusive_queue<node> queue{};
    std::optional<node>                        v1{std::in_place, 1};
    std::optional<node>                        v2{std::in_place, 2};
    queue.push_back(*v1);
    queue.push_back(*v2);

    auto& popped = queue.pop_front();
    REQUIRE(&popped == &*v1);
    v2.reset();
    REQUIRE(queue.empty());
}
