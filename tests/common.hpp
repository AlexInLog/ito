#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

struct call_mock // NOLINT (cppcoreguidelines-special-member-functions)
{
    virtual ~call_mock() = default;
    call_mock()          = default;

    MAKE_MOCK(call, auto(size_t)->void);
};

struct copy_count_tracker_impl
{
    MAKE_MOCK(copy_constructor, auto()->void);
    MAKE_MOCK(move_constructor, auto()->void);
};

class copy_count_tracker
{
public:
    copy_count_tracker()  = default;
    ~copy_count_tracker() = default;

    copy_count_tracker(const copy_count_tracker& v)
        : m_impl(v.m_impl)
    {
        m_impl->copy_constructor();
    }

    copy_count_tracker(copy_count_tracker&& v) noexcept
        : m_impl(std::move(v.m_impl))
    {
        m_impl->move_constructor();
    }

    copy_count_tracker& operator=(const copy_count_tracker& v)     = delete;
    copy_count_tracker& operator=(copy_count_tracker&& v) noexcept = delete;

    copy_count_tracker_impl& impl() { return *m_impl; }

private:
    std::shared_ptr<copy_count_tracker_impl> m_impl = std::make_shared<copy_count_tracker_impl>();
};
