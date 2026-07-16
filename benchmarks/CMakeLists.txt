find_package(Catch2 REQUIRED)

add_executable(ito_benchmarks
    benchmark_base_loop.cpp
)

target_link_libraries(ito_benchmarks
    PRIVATE
        ito
        Catch2::Catch2WithMain
)
