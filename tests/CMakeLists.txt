find_package(Catch2 REQUIRED)

add_executable(ito_tests
    test_coro.cpp
    test_loop.cpp
)

target_link_libraries(ito_tests
    PRIVATE
        ito
        Catch2::Catch2WithMain
        trompeloeil::trompeloeil
)

discover_tests(ito_tests)
