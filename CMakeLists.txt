cmake_minimum_required(VERSION 3.20)
project(MarketSimulator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ==========================
# Options
# ==========================

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_COVERAGE "Enable Coverage" OFF)
option(ENABLE_VALGRIND "Enable Valgrind Memcheck" OFF)
option(BUILD_TESTS "Build tests" ON)

# ==========================
# Safety checks
# ==========================

if(ENABLE_COVERAGE AND (ENABLE_ASAN OR ENABLE_UBSAN))
    message(FATAL_ERROR "Coverage cannot be combined with sanitizers")
endif()

if(ENABLE_VALGRIND AND (ENABLE_ASAN OR ENABLE_UBSAN))
    message(FATAL_ERROR "Valgrind cannot be combined with sanitizers")
endif()

# ==========================
# Compiler warnings
# ==========================

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
        -Wold-style-cast -Wduplicated-cond -Wduplicated-branches
        -Wlogical-op -Wuseless-cast -Wzero-as-null-pointer-constant
    )
endif()

# ==========================
# Base flags
# ==========================

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# ==========================
# Sanitizers
# ==========================

if(ENABLE_ASAN OR ENABLE_UBSAN)

    set(SANITIZER_LIST "")

    if(ENABLE_ASAN)
        list(APPEND SANITIZER_LIST address)
    endif()

    if(ENABLE_UBSAN)
        list(APPEND SANITIZER_LIST undefined)
    endif()

    string(JOIN "," SANITIZERS ${SANITIZER_LIST})

    message(STATUS "Enabled sanitizers: ${SANITIZERS}")

    add_compile_options(
        -fsanitize=${SANITIZERS}
        -fno-omit-frame-pointer
        -O1
        -g
    )

    add_link_options(-fsanitize=${SANITIZERS})

endif()


# ==========================
# Coverage
# ==========================

if(ENABLE_COVERAGE)

    message(STATUS "Enabled coverage")

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Coverage only supported on GCC/Clang")
    endif()

    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)

endif()

# ==========================
# Library
# ==========================

add_library(SimCore
    src/exchange/matching_engine.cpp
)

target_include_directories(SimCore PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ==========================
# Main executable
# ==========================

add_executable(MarketSimulator
    src/main.cpp
)

target_link_libraries(MarketSimulator PRIVATE SimCore)

# ==========================
# Tests
# ==========================

if(BUILD_TESTS)

    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(all_tests
        tests/matching_engine_tests.cpp
        tests/scheduler_tests.cpp
        tests/simulation_engine_tests.cpp
    )

    target_link_libraries(all_tests
        PRIVATE SimCore
        GTest::gtest
        GTest::gtest_main
    )

    include(GoogleTest)

    if(ENABLE_VALGRIND)

        add_test(
            NAME all_tests_memcheck
            COMMAND valgrind
                --leak-check=full
                --error-exitcode=1
                --track-origins=yes
                $<TARGET_FILE:all_tests>
        )

    else()

        gtest_discover_tests(all_tests)

    endif()

endif()

# ==========================
# Coverage Report Target
# ==========================

if(ENABLE_COVERAGE AND BUILD_TESTS)

    find_program(LCOV lcov REQUIRED)
    find_program(GENHTML genhtml REQUIRED)

    get_filename_component(GCC_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    find_program(GCOV_BIN gcov
        HINTS ${GCC_BIN_DIR}
        REQUIRED
    )

    message(STATUS "Using gcov: ${GCOV_BIN}")

    add_custom_target(coverage
        COMMAND ${LCOV} --gcov-tool ${GCOV_BIN} --directory . --zerocounters
        COMMAND ctest
        COMMAND ${LCOV} --gcov-tool ${GCOV_BIN} --directory . --capture --output-file coverage.info
        COMMAND ${LCOV} --remove coverage.info
            '/usr/*'
            '*/tests/*'
            '*/_deps/*'
            '*/main.cpp'
            --output-file coverage.info
        COMMAND ${GENHTML} coverage.info --output-directory coverage_report
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report"
    )

endif()

