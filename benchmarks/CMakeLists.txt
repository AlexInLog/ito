# ── Catch2 benchmarks ────────────────────────────────────────────────────────
# Existing local benchmarks driven by Catch2's microbenchmark framework.
# Only built when ITO_BUILD_BENCHMARKS is ON (which pulls Catch2 via Conan).
if(ITO_BUILD_BENCHMARKS)
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
endif()

# ── CodSpeed (Google Benchmark) ──────────────────────────────────────────────
# Continuous benchmarking target using CodSpeed's google_benchmark
# compatibility layer, fetched via FetchContent. It mirrors the Catch2
# benchmarks above but does not depend on Catch2/Conan, so it can be built
# standalone in CI. Enabled with -DITO_BUILD_CODSPEED_BENCHMARKS=ON (which the
# CI workflow sets) together with -DCODSPEED_MODE=<simulation|walltime|...>.
if(ITO_BUILD_CODSPEED_BENCHMARKS)
    include(FetchContent)

    set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON)

    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/CodSpeedHQ/codspeed-cpp
        SOURCE_SUBDIR google_benchmark
        GIT_TAG v2.4.0
    )

    FetchContent_MakeAvailable(google_benchmark)

    add_executable(ito_codspeed_benchmarks
        codspeed_benchmarks.cpp
    )

    target_link_libraries(ito_codspeed_benchmarks
        PRIVATE
            ito
            benchmark::benchmark
    )

    # The CodSpeed google_benchmark fork carries its own warnings; keep the
    # strict project options and lint tooling off this target.
    set_target_properties(ito_codspeed_benchmarks PROPERTIES
        C_CLANG_TIDY ""
        CXX_CLANG_TIDY ""
    )
endif()
