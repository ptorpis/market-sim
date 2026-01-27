cmake_minimum_required(VERSION 3.20)
project(MarketSimulator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_VALGRIND "Enable Valgrind for tests (cannot combine with ASAN/UBSAN)" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(BUILD_TESTS "Build tests" ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wold-style-cast
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
        -Wzero-as-null-pointer-constant
        -Wstrict-null-sentinel
    )
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -march=native")

if(ENABLE_ASAN AND ENABLE_VALGRIND)
    message(FATAL_ERROR "Cannot enable both ASAN and Valgrind")
endif()
if(ENABLE_UBSAN AND ENABLE_VALGRIND)
    message(FATAL_ERROR "Cannot enable both UBSAN and Valgrind")
endif()

if(ENABLE_ASAN OR ENABLE_UBSAN)
    message(STATUS "Building with sanitizers")
    set(SANITIZERS "")
    if(ENABLE_ASAN)
        list(APPEND SANITIZERS "address")
    endif()
    if(ENABLE_UBSAN)
        list(APPEND SANITIZERS "undefined")
    endif()
    add_compile_options(-g -O1 -fsanitize=${SANITIZERS} -fno-omit-frame-pointer)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZERS}")
else()
    message(STATUS "Building ${CMAKE_BUILD_TYPE} build")
endif()

# find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
# if(CLANG_TIDY_EXE)
    # message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE} (treating warnings as errors)")
    # set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*")
# endif()

add_library(SimCore
    src/time/simulation_timer.cpp
    src/exchange/matching_engine.cpp
)

target_include_directories(SimCore PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(MarketSimulator
    src/main.cpp
)

target_link_libraries(MarketSimulator PRIVATE SimCore)


if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)

    if(GTest_FOUND)
        add_executable(all_tests
            tests/matching_engine_tests.cpp
        )

        target_link_libraries(all_tests
            PRIVATE SimCore
            GTest::gtest
            GTest::gtest_main
        )

        if(ENABLE_ASAN OR ENABLE_UBSAN)
            target_compile_options(all_tests PRIVATE -fsanitize=${SANITIZERS} -fno-omit-frame-pointer -g -O0)
            target_link_options(all_tests PRIVATE -fsanitize=${SANITIZERS})
        endif()

        if(ENABLE_VALGRIND)
            add_test(NAME all_tests_valgrind
                COMMAND valgrind --leak-check=full $<TARGET_FILE:all_tests>)
        else()
            include(GoogleTest)
            gtest_discover_tests(all_tests)
        endif()
    endif()
endif()

