add_executable(ito_benchmarks
    simple_benchmarks.cpp
    benchmark_baseline.cpp
)

target_link_libraries(ito_benchmarks
    PRIVATE
        ito
        benchmark::benchmark_main
)

# target_compile_options(ito_benchmarks PRIVATE -Wno-c2y-extensions)
set_target_properties(ito_benchmarks PROPERTIES 
    C_CLANG_TIDY "" 
    CXX_CLANG_TIDY ""
)

