# ── cmake-conan ────────────────────────────────────────────────────────────────
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan_provider.cmake")
    message(STATUS "Downloading cmake-conan conan_provider.cmake...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/conan-io/cmake-conan/refs/heads/develop2/conan_provider.cmake"
        "${CMAKE_BINARY_DIR}/conan_provider.cmake"
        TLS_VERIFY ON
    )
endif()
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_BINARY_DIR}/conan_provider.cmake")

cmake_minimum_required(VERSION 3.25)

project(ito
    VERSION 0.1.0
    DESCRIPTION "C++20 async runtime with work-stealing scheduler"
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
option(ITO_BUILD_TESTS "Build tests" ON)

if(ITO_BUILD_TESTS)
    set(CONAN_INSTALL_ARGS "${CONAN_INSTALL_ARGS};-o ito/*:with_tests=True")
endif()

# ── Dependencies ───────────────────────────────────────────────────────────────

# ── Compiler warnings ──────────────────────────────────────────────────────────
add_library(ito_warnings INTERFACE)
target_compile_options(ito_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wconversion
        -Wshadow
    >
)

# ── Sanitizer presets ──────────────────────────────────────────────────────────
add_library(ito_sanitizers INTERFACE)
if(ITO_ENABLE_ASAN)
    message(STATUS "ASan enabled")
    target_compile_options(ito_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ito_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

if(ITO_ENABLE_TSAN)
    message(STATUS "TSan enabled")
    target_compile_options(ito_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(ito_sanitizers INTERFACE -fsanitize=thread)
endif()

# ── clang-tidy ─────────────────────────────────────────────────────────────────
if(ITO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found")
    endif()
endif()

# ── Library ────────────────────────────────────────────────────────────────────
add_library(ito INTERFACE)
target_include_directories(ito INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(ito INTERFACE ito_warnings ito_sanitizers)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ITO_BUILD_TESTS)
    find_package(Catch2 REQUIRED)
    find_package(trompeloeil REQUIRED)

    enable_testing()

    set(ITO_TEST_RESULTS_DIR  ${CMAKE_BINARY_DIR}/test_results)
    file(MAKE_DIRECTORY ${ITO_TEST_RESULTS_DIR})
    
    set(ITO_COVERAGE_TARGETS -instr-profile=${ITO_TEST_RESULTS_DIR}/results.profdata -object $<TARGET_FILE:ito_tests>)

    macro(discover_tests TARGET)
        include(Catch)
        catch_discover_tests(${TARGET} PROPERTIES  ENVIRONMENT "LLVM_PROFILE_FILE=${ITO_TEST_RESULTS_DIR}/${TARGET}.profraw" )
    endmacro()

    add_custom_target(
        coverage
        COMMAND llvm-profdata merge -o ${ITO_TEST_RESULTS_DIR}/results.profdata ${ITO_TEST_RESULTS_DIR}/*.profraw
        COMMAND llvm-cov report --ignore-filename-regex=build ${ITO_COVERAGE_TARGETS}
        COMMAND llvm-cov show --ignore-filename-regex=build --show-branches=count ${ITO_COVERAGE_TARGETS} > ${ITO_TEST_RESULTS_DIR}/coverage.txt
        COMMENT "Generating coverage report"
    )

    add_subdirectory(tests)
endif()

# ── Examples ───────────────────────────────────────────────────────────────────
# option(ITO_BUILD_EXAMPLES "Build examples" ON)
# if(ITO_BUILD_EXAMPLES)
#     add_subdirectory(examples)
# endif()
