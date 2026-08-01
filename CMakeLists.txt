cmake_minimum_required(VERSION 3.16)

project(echest
    VERSION 0.1.0
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
add_executable(echest
    src/echest.cpp
    src/types.h
    src/table.h
    src/search_state.h
    src/board.h
    src/movegen.h
    src/ordering.h
    src/prooftable.h
    src/search.h)
target_link_libraries(echest PRIVATE Threads::Threads)
target_compile_definitions(echest PRIVATE ECHEST_VERSION="${PROJECT_VERSION}")

# Cost attribution for the move-generation path. Not built by default: it is a
# development tool, not part of the shipped engine.
add_executable(bench_movegen EXCLUDE_FROM_ALL tools/bench_movegen.cpp)
target_include_directories(bench_movegen PRIVATE src)
target_link_libraries(bench_movegen PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(echest PRIVATE /W4 /permissive-)
else()
    target_compile_options(echest PRIVATE -Wall -Wextra -pedantic)
endif()

# Reproducible builds: identical source should yield an identical binary so the
# benchmark registry can pin an engine by hash. Only the MinGW linker needs the
# explicit flag; ELF and Mach-O output is already timestamp-free.
if(MINGW)
    target_link_options(echest PRIVATE -Wl,--no-insert-timestamp)
endif()

enable_testing()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME echest_selftest
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                     --engine $<TARGET_FILE:echest>)
    set_tests_properties(echest_selftest PROPERTIES TIMEOUT 900)
else()
    message(STATUS "Python3 not found; skipping echest self-test registration")
endif()

install(TARGETS echest RUNTIME DESTINATION bin)
install(PROGRAMS tools/verify_proof.py DESTINATION bin)
