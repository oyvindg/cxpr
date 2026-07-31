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
    if(test_file STREQUAL
       "${CMAKE_CURRENT_SOURCE_DIR}/scale_parity_benchmark.test.c")
        continue()
    endif()
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
    set(CXPR_SCALE_FIXTURE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scale")
    set(CXPR_SCALE_GENERATED_C
        "${CMAKE_CURRENT_BINARY_DIR}/large_host_neutral.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_SCALE_GENERATED_C}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CXPR_SCALE_FIXTURE_DIR}/large_host_neutral.cxpr"
            --output "${CXPR_SCALE_GENERATED_C}"
            --function cxpr_scale_fixture_tick
            --outputs active,inactive,score_04,confidence,selected_value,weighted_energy,signed_energy
            --specialize-defaults
        DEPENDS
            cxpr_model_codegen
            "${CXPR_SCALE_FIXTURE_DIR}/large_host_neutral.cxpr"
            "${CXPR_SCALE_FIXTURE_DIR}/scale_math.cxpr"
            "${CXPR_SCALE_FIXTURE_DIR}/scale_transforms.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_scale_fixture
        DEPENDS "${CXPR_SCALE_GENERATED_C}")
    add_executable(test_scale_parity_benchmark
        scale_parity_benchmark.test.c)
    add_dependencies(
        test_scale_parity_benchmark
        generate_cxpr_scale_fixture)
    target_link_libraries(test_scale_parity_benchmark PRIVATE cxpr)
    target_include_directories(test_scale_parity_benchmark PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(test_scale_parity_benchmark PRIVATE
        CXPR_SCALE_FIXTURE_DIR="${CXPR_SCALE_FIXTURE_DIR}")
    target_compile_options(test_scale_parity_benchmark PRIVATE -UNDEBUG)
    add_test(NAME scale_parity_benchmark
        COMMAND $<TARGET_FILE:test_scale_parity_benchmark>)

    add_test(NAME model_codegen_imports
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/imports/macd_uses_ema.cxpr"
            --output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.gen.c"
            --meta-output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.meta.json"
            --graph-output "${CMAKE_CURRENT_BINARY_DIR}/macd_uses_ema.graph.json"
            --function cxpr_test_macd_uses_ema_tick
            --qualifiers "static inline"
            --specialize-defaults)

    set(CXPR_COMPILED_STRATEGY_EXAMPLE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../examples/compiled_strategy")
    set(CXPR_COMPILED_STRATEGY_GENERATED_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated_examples/compiled_strategy")
    set(CXPR_COMPILED_STRATEGY_GENERATED_C
        "${CXPR_COMPILED_STRATEGY_GENERATED_DIR}/compiled_strategy.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_COMPILED_STRATEGY_GENERATED_C}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CXPR_COMPILED_STRATEGY_GENERATED_DIR}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CXPR_COMPILED_STRATEGY_EXAMPLE_DIR}/strategy.cxpr"
            --output "${CXPR_COMPILED_STRATEGY_GENERATED_C}"
            --function compiled_strategy_example_tick
        DEPENDS
            cxpr_model_codegen
            "${CXPR_COMPILED_STRATEGY_EXAMPLE_DIR}/strategy.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_compiled_strategy_example
        DEPENDS "${CXPR_COMPILED_STRATEGY_GENERATED_C}")
    add_executable(cxpr_compiled_strategy_example
        "${CXPR_COMPILED_STRATEGY_EXAMPLE_DIR}/host.c")
    add_dependencies(
        cxpr_compiled_strategy_example
        generate_cxpr_compiled_strategy_example)
    target_link_libraries(cxpr_compiled_strategy_example PRIVATE cxpr)
    target_include_directories(cxpr_compiled_strategy_example PRIVATE
        "${CXPR_COMPILED_STRATEGY_GENERATED_DIR}")
    target_compile_definitions(cxpr_compiled_strategy_example PRIVATE
        CXPR_COMPILED_STRATEGY_SOURCE="${CXPR_COMPILED_STRATEGY_EXAMPLE_DIR}/strategy.cxpr")
    target_compile_options(cxpr_compiled_strategy_example PRIVATE -UNDEBUG)
    add_test(
        NAME cxpr_compiled_strategy_example
        COMMAND $<TARGET_FILE:cxpr_compiled_strategy_example>)
endif()

if(CXPR_ENABLE_ASAN)
    set_tests_properties(${CXPR_C_TEST_NAMES} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
