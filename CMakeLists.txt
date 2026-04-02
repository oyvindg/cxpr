cmake_minimum_required(VERSION 3.20)
project(cxpr VERSION 1.0.0 LANGUAGES C)

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

# ═══════════════════════════════════════════════════════════════════════════
# C core library
# ═══════════════════════════════════════════════════════════════════════════
add_library(cxpr STATIC
    src/lexer.c
    src/parser.c
    src/ast.c
    src/eval.c
    src/ir.c
    src/hashmap.c
    src/struct.c
    src/context.c
    src/registry.c
    src/formula.c
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
