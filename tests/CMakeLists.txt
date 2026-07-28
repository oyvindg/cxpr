file(GLOB_RECURSE CXPR_C_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.test.c"
)
list(SORT CXPR_C_TESTS)
set(CXPR_C_TEST_NAMES)

set(CXPR_C_TEST_INTERNAL_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/chain_access.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_cache.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_values.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/define.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/errors.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/field_value.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hashmap.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/math.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexer.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/precedence.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/program.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/simulation.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_ctx.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_fn.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_producer.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_ownership.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/expression_ir.test.c"
)

foreach(test_file IN LISTS CXPR_C_TESTS)
    file(RELATIVE_PATH test_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${test_file}")
    string(REPLACE ".test.c" "" test_base "${test_rel}")
    string(REPLACE "/" "_" test_base "${test_base}")
    set(target_name "test_${test_base}")

    add_executable("${target_name}" "${test_file}")
    target_link_libraries("${target_name}" PRIVATE cxpr)
    target_include_directories("${target_name}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions("${target_name}" PRIVATE
        CXPR_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options("${target_name}" PRIVATE
            -UNDEBUG
            -include "${CMAKE_CURRENT_SOURCE_DIR}/cxpr_test_internal.h")
    elseif(MSVC)
        target_compile_options("${target_name}" PRIVATE /UNDEBUG)
    endif()

    if(test_file IN_LIST CXPR_C_TEST_INTERNAL_INCLUDE)
        target_include_directories("${target_name}" PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    endif()

    add_test(NAME "${test_base}" COMMAND "$<TARGET_FILE:${target_name}>")
    list(APPEND CXPR_C_TEST_NAMES "${test_base}")
endforeach()

if(TARGET cxpr_model_codegen)
    add_test(NAME model_codegen_imports
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/imports/macd_uses_ema.cxpr"
            --output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.gen.c"
            --meta-output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.meta.json"
            --graph-output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.graph.json"
            --function cxpr_test_macd_uses_ema_tick
            --qualifiers "static inline"
            --specialize-defaults)
endif()

if(CXPR_ENABLE_ASAN)
    set_tests_properties(${CXPR_C_TEST_NAMES} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
