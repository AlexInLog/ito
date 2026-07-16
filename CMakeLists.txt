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
option(ITO_BUILD_BENCHMARKS "Build benchmarks" ON)

option(ITO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ITO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ITO_ENABLE_LSAN "Enable LeakSanitizer" OFF)
option(ITO_ENABLE_MSAN "Enable MemorySanitizer" OFF)
option(ITO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ITO_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

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
    )

    target_link_options(ito_project_options INTERFACE
        -fsanitize=undefined
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
if(ITO_BUILD_BENCHMARKS)
    set(CONAN_INSTALL_ARGS "${CONAN_INSTALL_ARGS};-o ito/*:with_benchmarks=True")
endif()


# ── Library ────────────────────────────────────────────────────────────────────
add_library(ito INTERFACE)
target_include_directories(ito INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(ito INTERFACE ito_project_options)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ITO_BUILD_TESTS OR ITO_BUILD_BENCHMARKS)
    find_package(Catch2 REQUIRED)
    if (ITO_BUILD_TESTS)
        find_package(trompeloeil REQUIRED)

        enable_testing()

        set(ITO_TEST_RESULTS_DIR  ${CMAKE_BINARY_DIR}/test_results)
        file(MAKE_DIRECTORY ${ITO_TEST_RESULTS_DIR})

        set(ITO_COVERAGE_TARGETS -instr-profile=${ITO_TEST_RESULTS_DIR}/results.profdata -object $<TARGET_FILE:ito_tests>)

        macro(discover_tests TARGET)
            target_link_libraries(${TARGET} PRIVATE ito_project_options)
            include(Catch)
            catch_discover_tests(${TARGET} PROPERTIES  ENVIRONMENT "LLVM_PROFILE_FILE=${ITO_TEST_RESULTS_DIR}/${TARGET}-%p.profraw" )
        endmacro()

        add_custom_target(
            coverage
            COMMAND llvm-profdata merge -o ${ITO_TEST_RESULTS_DIR}/results.profdata ${ITO_TEST_RESULTS_DIR}/*.profraw
            COMMAND llvm-cov report --ignore-filename-regex=${CMAKE_BINARY_DIR} ${ITO_COVERAGE_TARGETS}
            COMMAND llvm-cov show --ignore-filename-regex=${CMAKE_BINARY_DIR} --show-branches=count -show-instantiations=false ${ITO_COVERAGE_TARGETS} > ${ITO_TEST_RESULTS_DIR}/coverage.txt
            COMMENT "Generating coverage report"
        )

        add_subdirectory(tests)
    endif()
    if (ITO_BUILD_BENCHMARKS) 
        add_subdirectory(benchmarks)
    endif()
endif()

# ── Examples ───────────────────────────────────────────────────────────────────
# option(ITO_BUILD_EXAMPLES "Build examples" ON)
# if(ITO_BUILD_EXAMPLES)
#     add_subdirectory(examples)
# endif()
