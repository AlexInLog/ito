find_package(Catch2 REQUIRED)

add_executable(ito_tests
    test_coro.cpp
    test_loop.cpp
    test_future.cpp
    test_task.cpp
    test_sleep.cpp
    test_move_only_callback.cpp

    common.hpp
)

target_link_libraries(ito_tests
    PRIVATE
        ito
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
)

discover_tests(ito_tests)

# Replaces the global operator new/delete, so it can't share an executable with the other tests.
add_executable(ito_alloc_failure_tests
    test_allocation_failure.cpp
)

target_link_libraries(ito_alloc_failure_tests
    PRIVATE
        ito
        Catch2::Catch2WithMain
)

discover_tests(ito_alloc_failure_tests)
