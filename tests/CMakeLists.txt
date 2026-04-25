set(CXPR_C_TESTS
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_analyze.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_construct.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_free.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_func.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_inspect.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_references.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/basket.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/call_args.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/chain_access.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_cache.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_lifecycle.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_slot.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_structs.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_values.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/define.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/errors.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval_api.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval_calls.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval_helpers.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval_tree.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/physics.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/robotics.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/trading.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/expression.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/expression_evaluator.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/expression_ir.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/field_value.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hashmap.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_cache_key.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_compile.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_exec_api.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_exec_calls.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_exec_fast.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_exec_lookup.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_exec_typed.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_ownership.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir_validate.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexer.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lookback.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/math.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser_grammar_expr.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser_helpers.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser_primary.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics_simulation.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/precedence.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/program.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/readme.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/registry_add.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/registry_call.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/registry_defaults.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/registry_helpers.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/simulation.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/strategy_expression.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_ctx.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_fn.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/struct_producer.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/timeseries.test.c"
)
set(CXPR_C_TEST_NAMES)

set(CXPR_C_TEST_INTERNAL_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/chain_access.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/define.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/errors.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/eval.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/field_value.test.c"
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
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options("${target_name}" PRIVATE
            -include "${CMAKE_CURRENT_SOURCE_DIR}/cxpr_test_internal.h")
    endif()

    if(test_file IN_LIST CXPR_C_TEST_INTERNAL_INCLUDE)
        target_include_directories("${target_name}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    add_test(NAME "${test_base}" COMMAND "$<TARGET_FILE:${target_name}>")
    list(APPEND CXPR_C_TEST_NAMES "${test_base}")
endforeach()

if(CXPR_ENABLE_ASAN)
    set_tests_properties(${CXPR_C_TEST_NAMES} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
