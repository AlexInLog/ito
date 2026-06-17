find_package(Catch2 REQUIRED)

add_executable(ito_tests
    test_task.cpp
)

target_link_libraries(ito_tests
    PRIVATE
        ito
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(ito_tests)
