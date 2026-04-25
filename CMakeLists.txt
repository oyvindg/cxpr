cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/cxpr/version.h" CXPR_VERSION_DEFINES
    REGEX "^#define CXPR_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$"
)
foreach(CXPR_VERSION_DEFINE IN LISTS CXPR_VERSION_DEFINES)
    if(CXPR_VERSION_DEFINE MATCHES "^#define CXPR_VERSION_([A-Z]+) ([0-9]+)$")
        set(CXPR_VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
if(NOT DEFINED CXPR_VERSION_MAJOR OR NOT DEFINED CXPR_VERSION_MINOR OR NOT DEFINED CXPR_VERSION_PATCH)
    message(FATAL_ERROR "Failed to parse CXPR_VERSION_* from include/cxpr/version.h")
endif()

project(cxpr
    VERSION "${CXPR_VERSION_MAJOR}.${CXPR_VERSION_MINOR}.${CXPR_VERSION_PATCH}"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C core uses C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default single-config local builds to Release so benchmark runs are meaningful
# unless the caller explicitly requested another build type.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CXPR_STRICT "Enable stricter compiler warnings and treat warnings as errors" OFF)
option(CXPR_ENABLE_ASAN "Enable AddressSanitizer instrumentation" OFF)
option(CXPR_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer instrumentation" OFF)

if(CXPR_ENABLE_ASAN AND MSVC)
    message(FATAL_ERROR "CXPR_ENABLE_ASAN is only supported on GCC/Clang-style toolchains")
endif()

if(CXPR_ENABLE_UBSAN AND MSVC)
    message(FATAL_ERROR "CXPR_ENABLE_UBSAN is only supported on GCC/Clang-style toolchains")
endif()

if((CXPR_ENABLE_ASAN OR CXPR_ENABLE_UBSAN) AND
   NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(FATAL_ERROR "Sanitizers require a GCC/Clang-style compiler")
endif()

# ═══════════════════════════════════════════════════════════════════════════
# C core library
# ═══════════════════════════════════════════════════════════════════════════
add_library(cxpr STATIC
    src/lexer/lexer.c
    src/parser/parser.c
    src/parser/helpers.c
    src/parser/grammar_expr.c
    src/parser/primary.c
    src/ast/construct.c
    src/ast/free.c
    src/ast/inspect.c
    src/ast/references.c
    src/ast/analyze.c
    src/call/args.c
    src/eval/helpers.c
    src/eval/calls.c
    src/eval/tree.c
    src/eval/api.c
    src/ir/program.c
    src/ir/cache_key.c
    src/ir/validate.c
    src/ir/compile.c
    src/ir/exec/lookup.c
    src/ir/exec/calls.c
    src/ir/exec/typed.c
    src/ir/exec/fast.c
    src/ir/exec/api.c
    src/hashmap/hashmap.c
    src/struct.c
    src/context/cache.c
    src/context/lifecycle.c
    src/context/values.c
    src/context/structs.c
    src/context/slot.c
    src/registry/helpers.c
    src/registry/add.c
    src/registry/defaults.c
    src/registry/define.c
    src/registry/call.c
    src/timeseries.c
    src/basket.c
    src/expression/evaluator.c
    src/expression/expression.c
)
add_library(cxpr::cxpr ALIAS cxpr)

set_target_properties(cxpr PROPERTIES
    VERSION ${PROJECT_VERSION}
    EXPORT_NAME cxpr
)

target_include_directories(cxpr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(cxpr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Determinism: no fast-math, no FP contraction
target_compile_options(cxpr PRIVATE -ffp-contract=off -fno-fast-math)

if(CXPR_STRICT)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(cxpr PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(cxpr PRIVATE /W4 /WX)
    endif()
endif()

if(CXPR_ENABLE_ASAN OR CXPR_ENABLE_UBSAN)
    set(CXPR_SANITIZER_FLAGS "")

    if(CXPR_ENABLE_ASAN)
        list(APPEND CXPR_SANITIZER_FLAGS -fsanitize=address)
    endif()

    if(CXPR_ENABLE_UBSAN)
        list(APPEND CXPR_SANITIZER_FLAGS -fsanitize=undefined)
    endif()

    list(APPEND CXPR_SANITIZER_FLAGS -fno-omit-frame-pointer)

    add_compile_options(${CXPR_SANITIZER_FLAGS})
    add_link_options(${CXPR_SANITIZER_FLAGS})
endif()

# Link math library on UNIX systems
target_link_libraries(cxpr PRIVATE m)

# ═══════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_TESTS "Build cxpr tests" ${PROJECT_IS_TOP_LEVEL})
if(CXPR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Benchmarks
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_BENCHMARKS "Build cxpr benchmarks" ${PROJECT_IS_TOP_LEVEL})
if(CXPR_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Install and package export
# ═══════════════════════════════════════════════════════════════════════════
install(
    TARGETS cxpr
    EXPORT cxprTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cxprConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cxprConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cxprConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxpr
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cxprConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cxprConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxpr
)

install(
    EXPORT cxprTargets
    FILE cxprTargets.cmake
    NAMESPACE cxpr::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxpr
)
