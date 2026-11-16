# ── cmake-conan ────────────────────────────────────────────────────────────────
# Only when ito is the top-level project: a project embedding ito via add_subdirectory/FetchContent resolves its own
# dependencies, and the provider would have no effect there anyway (it only hooks the first project() call).
# Pinned to a develop2 commit (cmake-conan has no Conan 2 releases) and hash-checked, since the file runs as part of the
# build. file(DOWNLOAD) skips the download when the file already exists with this hash.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(DOWNLOAD
        "https://raw.githubusercontent.com/conan-io/cmake-conan/b1593849dd842c37ff198b9d3e7a6d4e03803121/conan_provider.cmake"
        "${CMAKE_BINARY_DIR}/conan_provider.cmake"
        EXPECTED_HASH SHA256=63caaf42c06620d4f6139a0e17b3442738570fb9b123585ba8247afc322f8789
        TLS_VERIFY ON
    )
    set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_BINARY_DIR}/conan_provider.cmake")
endif()

cmake_minimum_required(VERSION 3.25)

project(ito
    VERSION 0.1.0
    DESCRIPTION "C++20 coroutine-based async runtime"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR  ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif(CCACHE)

# -- Options ---------------------------------
option(ITO_BUILD_TESTS "Build tests" OFF)
option(ITO_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ITO_BUILD_EXAMPLES "Build examples" OFF)

option(ITO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ITO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ITO_ENABLE_LSAN "Enable LeakSanitizer" OFF)
option(ITO_ENABLE_MSAN "Enable MemorySanitizer" OFF)
option(ITO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ITO_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

# Warnings, sanitizers and coverage for this repository's own targets (tests, examples, benchmarks). Deliberately not
# linked to `ito`: an INTERFACE dependency would push -Werror, -fno-rtti and the sanitizers onto every consumer.
add_library(ito_project_options INTERFACE)
if(MSVC)

    target_compile_options(ito_project_options INTERFACE
        /W4
        /WX
    )

else()

    target_compile_options(ito_project_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror

        -Wcast-qual
        -Wformat=2
        -Wundef
        -Werror=float-equal

        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-error=maybe-uninitialized
        -Wno-error=uninitialized
        -Wno-unknown-warning-option
        -Wno-missing-template-arg-list-after-template-kw

        -fno-rtti
    )

endif()

if(ITO_ENABLE_ASAN)
    target_compile_options(ito_project_options INTERFACE
        -fsanitize=address
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -fsanitize-address-use-after-scope
        -g
        -O1
    )

    target_link_options(ito_project_options INTERFACE
        -fsanitize=address
    )

endif()

if(ITO_ENABLE_TSAN)
    target_compile_options(ito_project_options INTERFACE
        -fsanitize=thread
        -g
        -O1
    )

    target_link_options(ito_project_options INTERFACE
        -fsanitize=thread
    )

endif()

if(ITO_ENABLE_UBSAN)
    target_compile_options(ito_project_options INTERFACE
        -fsanitize=undefined
        -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer
        -g
    )

    target_link_options(ito_project_options INTERFACE
        -fsanitize=undefined
    )

endif()

if(ITO_ENABLE_LSAN)
    target_compile_options(ito_project_options INTERFACE
        -fsanitize=leak
        -g
        -O1
    )

    target_link_options(ito_project_options INTERFACE
        -fsanitize=leak
    )

endif()

if(ITO_ENABLE_COVERAGE)
    target_compile_options(ito_project_options INTERFACE
        -O0
        -g
        -fprofile-instr-generate
        -fcoverage-mapping
    )

    target_link_options(ito_project_options INTERFACE
        -fprofile-instr-generate
        -fcoverage-mapping
    )

endif()

if(ITO_BUILD_TESTS)
    set(CONAN_INSTALL_ARGS "${CONAN_INSTALL_ARGS};-o ito/*:with_tests=True")
endif()


# ── Library ────────────────────────────────────────────────────────────────────
add_library(ito INTERFACE)
target_include_directories(ito INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ITO_BUILD_TESTS)
    find_package(Catch2 REQUIRED)
    find_package(trompeloeil REQUIRED)

    enable_testing()

    set(ITO_TEST_RESULTS_DIR  ${CMAKE_BINARY_DIR}/test_results)
    file(MAKE_DIRECTORY ${ITO_TEST_RESULTS_DIR})

    macro(discover_tests TARGET)
        target_link_libraries(${TARGET} PRIVATE ito_project_options)
        include(Catch)
        catch_discover_tests(${TARGET} PROPERTIES  ENVIRONMENT "LLVM_PROFILE_FILE=${ITO_TEST_RESULTS_DIR}/${TARGET}-%p.profraw" )
        set_property(GLOBAL APPEND PROPERTY ITO_TEST_TARGETS ${TARGET})
    endmacro()

    add_subdirectory(tests)

    # Every executable registered through discover_tests() above contributes to the coverage report.
    get_property(ito_test_targets GLOBAL PROPERTY ITO_TEST_TARGETS)
    set(ITO_COVERAGE_TARGETS -instr-profile=${ITO_TEST_RESULTS_DIR}/results.profdata)
    foreach(target IN LISTS ito_test_targets)
        list(APPEND ITO_COVERAGE_TARGETS -object $<TARGET_FILE:${target}>)
    endforeach()

    add_custom_target(
            coverage
            COMMAND llvm-profdata merge -o ${ITO_TEST_RESULTS_DIR}/results.profdata ${ITO_TEST_RESULTS_DIR}/*.profraw
            COMMAND llvm-cov report --ignore-filename-regex=${CMAKE_BINARY_DIR} ${ITO_COVERAGE_TARGETS}
            COMMAND llvm-cov show --ignore-filename-regex=${CMAKE_BINARY_DIR} --show-branches=count -show-instantiations=false ${ITO_COVERAGE_TARGETS} > ${ITO_TEST_RESULTS_DIR}/coverage.txt
            # Consume the raw profiles, so a later report doesn't merge in profiles from binaries that were rebuilt since.
            COMMAND ${CMAKE_COMMAND} -E rm -f ${ITO_TEST_RESULTS_DIR}/*.profraw
            COMMENT "Generating coverage report"
        )
endif()

if (ITO_BUILD_BENCHMARKS)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
      google_benchmark
      GIT_REPOSITORY https://github.com/CodSpeedHQ/codspeed-cpp
      SOURCE_SUBDIR google_benchmark
      GIT_TAG v2.4.0
      SYSTEM
    )

    FetchContent_MakeAvailable(google_benchmark)

    target_compile_options(benchmark_main PRIVATE -Wno-error)
    target_compile_options(benchmark PUBLIC -Wno-c2y-extensions PRIVATE -Wno-error)

    add_subdirectory(benchmarks)
endif()

# ── Examples ───────────────────────────────────────────────────────────────────
if(ITO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
