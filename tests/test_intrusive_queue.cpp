#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <ito/details/utils/intrusive_queue.hpp>

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct node : public ito::details::utils::intrusive_queue_node
    {
        explicit node(size_t v)
            : value{v}
        {
        }
        size_t value{};
    };
} // namespace

TEST_CASE("intrusive queue push and pop")
{
    ito::details::utils::intrusive_queue<node> queue{};
    REQUIRE(queue.empty());

    SECTION("pop_front returns the pushed node and empties the queue")
    {
        node v1{1};
        queue.push_back(v1);
        REQUIRE_FALSE(queue.empty());
        REQUIRE(&queue.pop_front() == &v1);
        REQUIRE(queue.empty());
    }
    SECTION("destroying a queued node empties the queue")
    {
        {
            node v1{1};
            queue.push_back(v1);
            REQUIRE_FALSE(queue.empty());
        }
        REQUIRE(queue.empty());
    }
}

TEST_CASE("destroying a queued node removes it and keeps the others in order")
{
    ito::details::utils::intrusive_queue<node> queue{};
    const size_t                               index_to_delete = GENERATE(0, 1, 2);
    CAPTURE(index_to_delete);

    std::array<std::optional<node>, 3> vals{};
    std::array<node*, 3>               ptrs{};
    for (size_t i = 0; i < vals.size(); ++i) queue.push_back(*(ptrs.at(i) = &vals.at(i).emplace(i + 1)));

    vals.at(index_to_delete).reset();
    REQUIRE_FALSE(queue.empty());
    for (size_t i = 0; i < vals.size(); ++i)
    {
        if (i != index_to_delete)
            REQUIRE(&queue.pop_front() == ptrs.at(i));
    }
    REQUIRE(queue.empty());
}

TEST_CASE("moving an intrusive queue")
{
    ito::details::utils::intrusive_queue<node> q{};

    SECTION("with nodes moves them over in order and leaves an empty queue")
    {
        node v1{1};
        node v2{2};
        q.push_back(v1);
        q.push_back(v2);

        auto new_q = std::move(q);
        REQUIRE(q.empty()); // NOLINT(bugprone-use-after-move)
        REQUIRE_FALSE(new_q.empty());
        REQUIRE(&new_q.pop_front() == &v1);
        REQUIRE(&new_q.pop_front() == &v2);
        REQUIRE(new_q.empty());
    }
    SECTION("an empty one leaves both queues usable")
    {
        auto new_q = std::move(q);
        REQUIRE(q.empty()); // NOLINT(bugprone-use-after-move)
        REQUIRE(new_q.empty());

        node v1{1};
        new_q.push_back(v1);
        REQUIRE(&new_q.pop_front() == &v1);
        q.push_back(v1); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): a moved-from queue is a valid empty queue
        REQUIRE(&q.pop_front() == &v1);
    }
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

TEST_CASE("consume on an empty intrusive queue doesn't call the callback")
{
    ito::details::utils::intrusive_queue<node> queue{};
    size_t                                     calls = 0;
    queue.consume([&](node&) {
        ++calls;
        return true;
    });
    REQUIRE(calls == 0);
    REQUIRE(queue.empty());
}

TEST_CASE("consume visits only the nodes queued when it started")
{
    ito::details::utils::intrusive_queue<node> queue{};
    std::array<node, 3>                        vals{node{1}, node{2}, node{3}};
    for (auto& v : vals) queue.push_back(v);

    std::array<node, 3> added{node{4}, node{5}, node{6}};
    std::vector<size_t> visited{};

    SECTION("in order, emptying the queue")
    {
        queue.consume([&](node& n) {
            visited.push_back(n.value);
            REQUIRE_FALSE(n.is_linked());
            return true;
        });
        REQUIRE(visited == std::vector<size_t>{1, 2, 3});
        REQUIRE(queue.empty());
    }
    SECTION("nodes pushed while consuming stay queued, in order")
    {
        queue.consume([&](node& n) {
            visited.push_back(n.value);
            queue.push_back(added.at(n.value - 1));
            return true;
        });
        REQUIRE(visited == std::vector<size_t>{1, 2, 3});
        for (auto& a : added) REQUIRE(&queue.pop_front() == &a);
        REQUIRE(queue.empty());
    }
    SECTION("a node pushed back while it's being consumed waits for the next consume")
    {
        queue.consume([&](node& n) {
            visited.push_back(n.value);
            if (n.value == 1)
                queue.push_back(n);
            return true;
        });
        REQUIRE(visited == std::vector<size_t>{1, 2, 3});
        REQUIRE(&queue.pop_front() == &vals.at(0));
        REQUIRE(queue.empty());
    }
    SECTION("returning false stops after that node and leaves the rest ahead of new ones")
    {
        queue.consume([&](node& n) {
            visited.push_back(n.value);
            queue.push_back(added.at(n.value - 1));
            return n.value != 2;
        });
        REQUIRE(visited == std::vector<size_t>{1, 2});
        REQUIRE_FALSE(vals.at(1).is_linked());
        REQUIRE(&queue.pop_front() == &vals.at(2));
        REQUIRE(&queue.pop_front() == &added.at(0));
        REQUIRE(&queue.pop_front() == &added.at(1));
        REQUIRE(queue.empty());
    }
    SECTION("a throwing callback leaves the rest queued without the marker")
    {
        REQUIRE_THROWS_AS(
            queue.consume([&](node& n) -> bool {
                visited.push_back(n.value);
                if (n.value == 2)
                    throw std::runtime_error{"boom"};
                return true;
            }),
            std::runtime_error
        );
        REQUIRE(visited == std::vector<size_t>{1, 2});
        REQUIRE(&queue.pop_front() == &vals.at(2));
        REQUIRE(queue.empty());
    }
}

TEST_CASE("consume skips a node destroyed by the callback")
{
    ito::details::utils::intrusive_queue<node> queue{};
    std::array<std::optional<node>, 3>         vals{};
    for (size_t i = 0; i < vals.size(); ++i) queue.push_back(vals.at(i).emplace(i + 1));

    std::vector<size_t> visited{};
    queue.consume([&](node& n) {
        visited.push_back(n.value);
        if (n.value == 1)
            vals.at(1).reset();
        return true;
    });
    REQUIRE(visited == std::vector<size_t>{1, 3});
    REQUIRE(queue.empty());
}
