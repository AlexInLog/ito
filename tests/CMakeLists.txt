find_package(Catch2 REQUIRED)

add_executable(ito_tests
    test_coro.cpp
    # test_loop.cpp
    # test_future.cpp
    test_task.cpp
    # test_sleep.cpp
    # test_move_only_callback.cpp
    # test_outcome.cpp
    # test_anchored.cpp

    common.hpp
)

target_link_libraries(ito_tests
    PRIVATE
        ito
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
)

discover_tests(ito_tests)

# Replaces the global operator new/delete, so it can't share an executable with the other tests. TSan, LSan and MSan
# link their own strong operator new/delete, which a replacement collides with (ASan's are weak and yield to it), so
# it's skipped there: the test is single-threaded and about allocation failure, which the other builds cover.
if(NOT (ITO_ENABLE_TSAN OR ITO_ENABLE_LSAN OR ITO_ENABLE_MSAN))
    add_executable(ito_alloc_failure_tests
        test_allocation_failure.cpp
    )

    target_link_libraries(ito_alloc_failure_tests
        PRIVATE
            ito
            Catch2::Catch2WithMain
    )

    discover_tests(ito_alloc_failure_tests)
endif()
