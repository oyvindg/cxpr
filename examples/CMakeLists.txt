set(CXPR_EXAMPLE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

function(cxpr_add_manifest_example id source)
    set(target "cxpr_example_${id}")
    add_executable("${target}" "${source}")
    target_link_libraries("${target}" PRIVATE cxpr)
    target_compile_definitions("${target}" PRIVATE
        CXPR_EXAMPLE_SOURCE_DIR="${CXPR_EXAMPLE_SOURCE_DIR}")
    add_test(NAME "examples_${id}" COMMAND "$<TARGET_FILE:${target}>")
    set_property(GLOBAL APPEND PROPERTY CXPR_MANIFEST_EXAMPLE_TARGETS "${id}")
endfunction()

cxpr_add_manifest_example(fundamentals_expression
    fundamentals/expression/host.c)
cxpr_add_manifest_example(physics_oscillator
    domains/physics/oscillator/host.c)

# Validate the machine-readable inventory while targets and source paths are
# available. This intentionally fails configuration when documentation drifts.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" CXPR_EXAMPLE_MANIFEST)
string(JSON CXPR_EXAMPLE_COUNT LENGTH "${CXPR_EXAMPLE_MANIFEST}" examples)
if(CXPR_EXAMPLE_COUNT EQUAL 0)
    message(FATAL_ERROR "CXPR example manifest must contain at least one example")
endif()
math(EXPR CXPR_EXAMPLE_LAST "${CXPR_EXAMPLE_COUNT} - 1")
foreach(index RANGE 0 ${CXPR_EXAMPLE_LAST})
    string(JSON id GET "${CXPR_EXAMPLE_MANIFEST}" examples ${index} id)
    string(JSON readme GET "${CXPR_EXAMPLE_MANIFEST}" examples ${index} readme)
    string(JSON model GET "${CXPR_EXAMPLE_MANIFEST}" examples ${index} model)
    string(JSON host GET "${CXPR_EXAMPLE_MANIFEST}" examples ${index} host)
    string(JSON test GET "${CXPR_EXAMPLE_MANIFEST}" examples ${index} test)
    foreach(path IN ITEMS "${readme}" "${model}" "${host}")
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${path}")
            message(FATAL_ERROR
                "CXPR example '${id}' references missing file: ${path}")
        endif()
    endforeach()
    if(NOT TARGET "cxpr_example_${id}")
        message(FATAL_ERROR
            "CXPR example '${id}' has no cxpr_example_${id} target")
    endif()
    if(NOT test STREQUAL "examples_${id}")
        message(FATAL_ERROR
            "CXPR example '${id}' must use test name examples_${id}")
    endif()
endforeach()
