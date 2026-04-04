cmake_minimum_required(VERSION 3.20)
project(cxpr VERSION 1.0.1 LANGUAGES C)

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
    src/lexer.c
    src/parser.c
    src/ast.c
    src/eval.c
    src/ir.c
    src/ir/compile.c
    src/ir/exec.c
    src/hashmap.c
    src/struct.c
    src/context.c
    src/registry.c
    src/evaluator.c
    src/expression.c
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
