find_package(Catch2 REQUIRED)

add_executable(ito_benchmarks
    simple_benchmarks.cpp
    benchmark_baseline.cpp
)

target_link_libraries(ito_benchmarks
    PRIVATE
        ito
        Catch2::Catch2WithMain
)

set_target_properties(ito_benchmarks PROPERTIES 
    C_CLANG_TIDY "" 
    CXX_CLANG_TIDY ""
)

