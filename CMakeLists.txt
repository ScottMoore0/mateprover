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
    src/kingescape.h
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

# AVX is disabled on MinGW because enabling it produces a binary that faults in
# move generation. This is a code generation defect, not a portability
# preference, and the engine asks for nothing unusual: `Board` is a plain
# aggregate whose widest member is a uint64_t, so its alignment is 8, and there
# is no `alignas` anywhere in the source.
#
# What happens is that GCC copies `Board` through ymm registers and spills with
#
#     vmovdqa %ymm0,0x40(%rsp)
#
# into a stack slot it treats as 32-byte aligned, while the function prologue is
# eight pushes and a `sub` with no `and $-32,%rsp` to make that true. Windows
# guarantees 16-byte stack alignment, so the slot is 32-byte aligned only by
# luck: `perft` faulted with rsp = 0x5ff430, which is 16-aligned and not 32.
# That coin flip is why the crash depth moves with the optimisation level and
# why it can look intermittent. `-mstackrealign`, `-mpreferred-stack-boundary=5`
# and `-fno-tree-vectorize` all fail to avoid it; only disabling AVX does.
#
# Turning it off costs nothing measurable. The engine is branchy pointer-chasing
# work with no hand-vectorised kernel, and `-march=native -mno-avx` against a
# plain `-O3` build differs by 0.3% on perft (7.053 s against 7.073 s) and by
# less than the machine's own run-to-run noise on a node-limited search.
#
# On a baseline x86-64 build the flag is a no-op, so this is unconditional
# rather than conditional on detecting AVX -- there is no configuration in which
# it removes something the engine was using. Set MATEPROVER_ALLOW_AVX=ON to
# override it on a toolchain known to be fixed.
option(MATEPROVER_ALLOW_AVX
       "Permit AVX codegen on MinGW (see the alignment note in CMakeLists.txt)"
       OFF)
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT MATEPROVER_ALLOW_AVX)
    # Appended after CMAKE_CXX_FLAGS, so it wins over a user's -march=native.
    target_compile_options(mateprover PRIVATE -mno-avx)
    target_compile_options(bench_movegen PRIVATE -mno-avx)
    message(STATUS
        "mateprover: AVX disabled on MinGW to avoid a stack-alignment fault "
        "in generated struct copies (MATEPROVER_ALLOW_AVX=ON to override)")
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
