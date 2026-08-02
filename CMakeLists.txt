cmake_minimum_required(VERSION 3.16)

# Read the version out of the source rather than declaring it here. Two
# declarations drift: this file said 0.1.0 while a direct g++ build reported
# 0.1.0-dev, for the same code.
file(READ src/mateprover.cpp _mateprover_main_source)
string(REGEX MATCH "#define MATEPROVER_VERSION \"([0-9]+\.[0-9]+\.[0-9]+)\""
       _mateprover_version_match "${_mateprover_main_source}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "could not read MATEPROVER_VERSION from src/mateprover.cpp")
endif()

project(mateprover
    VERSION ${CMAKE_MATCH_1}
    DESCRIPTION "Exact directmate prover with machine-checkable proof certificates"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# Deliberately a unity build: the modules are headers included in order by one
# translation unit, because the search relies on cross-module inlining in its
# hottest paths. Listing the headers keeps IDEs and install rules aware of them.
add_executable(mateprover
    src/mateprover.cpp
    src/types.h
    src/table.h
    src/search_state.h
    src/board.h
    src/movegen.h
    src/ordering.h
    src/prooftable.h
    src/prove.h
    src/rootsplit.h
    src/dfpn.h
    src/routes.h
    src/report.h
    src/solve.h)
target_link_libraries(mateprover PRIVATE Threads::Threads)

# Cost attribution for the move-generation path. Not built by default: it is a
# development tool, not part of the shipped engine.
add_executable(bench_movegen EXCLUDE_FROM_ALL tools/bench_movegen.cpp)
target_include_directories(bench_movegen PRIVATE src)
target_link_libraries(bench_movegen PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(mateprover PRIVATE /W4 /permissive-)
else()
    target_compile_options(mateprover PRIVATE -Wall -Wextra -pedantic)
endif()

# Reproducible builds: identical source should yield an identical binary so the
# benchmark registry can pin an engine by hash. Only the MinGW linker needs the
# explicit flag; ELF and Mach-O output is already timestamp-free.
if(MINGW)
    target_link_options(mateprover PRIVATE -Wl,--no-insert-timestamp)
endif()

enable_testing()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME mateprover_selftest
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                     --engine $<TARGET_FILE:mateprover>)
    set_tests_properties(mateprover_selftest PROPERTIES TIMEOUT 900)
else()
    message(STATUS "Python3 not found; skipping mateprover self-test registration")
endif()

install(TARGETS mateprover RUNTIME DESTINATION bin)
install(PROGRAMS tools/verify_proof.py DESTINATION bin)
