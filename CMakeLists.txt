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
option(CXPR_ENABLE_COVERAGE "Enable compiler coverage instrumentation" OFF)

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

if(CXPR_ENABLE_COVERAGE AND NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(FATAL_ERROR "Coverage requires a GCC/Clang-style compiler")
endif()

if(CXPR_ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# C core library
# ═══════════════════════════════════════════════════════════════════════════
file(GLOB_RECURSE CXPR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
list(APPEND CXPR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/c.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/cuda.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/graph.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/meta.c")
list(SORT CXPR_SOURCES)

add_library(cxpr STATIC ${CXPR_SOURCES})
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
# Tools
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_TOOLS "Build cxpr command-line tools" ON)
if(CXPR_BUILD_TOOLS)
    add_executable(cxpr_model_codegen
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/cxpr_model_codegen.c")
    target_link_libraries(cxpr_model_codegen PRIVATE cxpr)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_TESTS "Build cxpr tests" ${PROJECT_IS_TOP_LEVEL})
if(CXPR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    add_executable(cxpr_example_engine_lookback
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/engine_lookback.c")
    target_link_libraries(cxpr_example_engine_lookback PRIVATE cxpr)
    add_test(NAME examples_engine_lookback
             COMMAND "$<TARGET_FILE:cxpr_example_engine_lookback>")

    add_executable(cxpr_example_ast_snapshot_cytoscape
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/ast_snapshot_cytoscape.c")
    target_link_libraries(cxpr_example_ast_snapshot_cytoscape PRIVATE cxpr)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Benchmarks
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_BENCHMARKS "Build cxpr benchmarks" ${PROJECT_IS_TOP_LEVEL})
if(CXPR_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Fuzzers (libFuzzer; Clang only)
# ═══════════════════════════════════════════════════════════════════════════
option(CXPR_BUILD_FUZZERS "Build cxpr libFuzzer targets (requires Clang)" OFF)
if(CXPR_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()

if(CXPR_ENABLE_COVERAGE)
    add_custom_target(cxpr_check_function_coverage
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_function_coverage.sh
                ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking that every cxpr function is executed by coverage tests"
        VERBATIM
    )
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
