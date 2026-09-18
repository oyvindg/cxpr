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
    if(NOT TARGET cxpr_model_codegen AND test_file MATCHES
       "(bulk_buffer_parity|bulk_codegen_e2e|composition_bulk_codegen_e2e|dynamic_host_e2e)\\.test\\.c$")
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

if(CXPR_BUILD_SHARED)
    add_executable(test_pinvoke_smoke pinvoke_smoke.c)
    target_link_libraries(test_pinvoke_smoke PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(test_pinvoke_smoke PRIVATE
        CXPR_SHARED_LIBRARY_PATH="$<TARGET_FILE:cxpr>")
    add_dependencies(test_pinvoke_smoke cxpr)
    add_test(NAME pinvoke_smoke COMMAND $<TARGET_FILE:test_pinvoke_smoke>)

    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        execute_process(COMMAND "${DOTNET_EXECUTABLE}" --version
            OUTPUT_VARIABLE DOTNET_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "^[0-9]+" DOTNET_SDK_MAJOR "${DOTNET_SDK_VERSION}")
        set(CXPR_DOTNET_SMOKE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pinvoke_dotnet_smoke")
        file(MAKE_DIRECTORY "${CXPR_DOTNET_SMOKE_DIR}")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/pinvoke_dotnet_smoke/pinvoke_dotnet_smoke.csproj.in"
            "${CXPR_DOTNET_SMOKE_DIR}/pinvoke_dotnet_smoke.csproj" @ONLY)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/pinvoke_dotnet_smoke/Program.cs"
            "${CXPR_DOTNET_SMOKE_DIR}/Program.cs" COPYONLY)
        add_test(NAME pinvoke_dotnet_smoke
            COMMAND "${DOTNET_EXECUTABLE}" run --project
                "${CXPR_DOTNET_SMOKE_DIR}/pinvoke_dotnet_smoke.csproj"
                --configuration Release)
        set_tests_properties(pinvoke_dotnet_smoke PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:cxpr>"
            LABELS "dotnet")
    endif()
endif()

add_executable(cxpr_generated_array_codegen_tool generated_array_codegen_tool.c)
target_link_libraries(cxpr_generated_array_codegen_tool PRIVATE cxpr)
set(CXPR_GENERATED_ARRAY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated_array_functions.h")
add_custom_command(
    OUTPUT "${CXPR_GENERATED_ARRAY_HEADER}"
    COMMAND $<TARGET_FILE:cxpr_generated_array_codegen_tool>
            "${CXPR_GENERATED_ARRAY_HEADER}"
    DEPENDS cxpr_generated_array_codegen_tool
    VERBATIM)
add_custom_target(generate_cxpr_array_functions
    DEPENDS "${CXPR_GENERATED_ARRAY_HEADER}")
add_dependencies(test_generated_array_parity generate_cxpr_array_functions)
target_include_directories(test_generated_array_parity PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(cxpr_generated_history_codegen_tool generated_history_codegen_tool.c)
target_link_libraries(cxpr_generated_history_codegen_tool PRIVATE cxpr)
set(CXPR_GENERATED_HISTORY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated_history_function.h")
add_custom_command(
    OUTPUT "${CXPR_GENERATED_HISTORY_HEADER}"
    COMMAND $<TARGET_FILE:cxpr_generated_history_codegen_tool>
            "${CXPR_GENERATED_HISTORY_HEADER}"
    DEPENDS cxpr_generated_history_codegen_tool
    VERBATIM)
add_custom_target(generate_cxpr_history_function
    DEPENDS "${CXPR_GENERATED_HISTORY_HEADER}")
add_dependencies(test_generated_history_parity generate_cxpr_history_function)
target_include_directories(test_generated_history_parity PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(cxpr_generated_resample_codegen_tool generated_resample_codegen_tool.c)
target_link_libraries(cxpr_generated_resample_codegen_tool PRIVATE cxpr)
set(CXPR_GENERATED_RESAMPLE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_resample_function.h")
add_custom_command(OUTPUT "${CXPR_GENERATED_RESAMPLE_HEADER}"
    COMMAND $<TARGET_FILE:cxpr_generated_resample_codegen_tool> "${CXPR_GENERATED_RESAMPLE_HEADER}"
    DEPENDS cxpr_generated_resample_codegen_tool VERBATIM)
add_custom_target(generate_cxpr_resample_function DEPENDS "${CXPR_GENERATED_RESAMPLE_HEADER}")
add_dependencies(test_generated_resample_parity generate_cxpr_resample_function)
target_include_directories(test_generated_resample_parity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(test_history PRIVATE Threads::Threads)

add_executable(cxpr_cuda_bulk_codegen_tool cuda_bulk_codegen_tool.c)
target_link_libraries(cxpr_cuda_bulk_codegen_tool PRIVATE cxpr)
add_test(NAME cuda_bulk_codegen
    COMMAND $<TARGET_FILE:cxpr_cuda_bulk_codegen_tool>
        "${CMAKE_CURRENT_SOURCE_DIR}/../examples/bulk_grid/klein_gordon_cell.cxpr"
        "${CMAKE_CURRENT_BINARY_DIR}/cxpr_klein_gordon_codegen_test.cuh")

option(CXPR_BUILD_CUDA_TESTS "Build CUDA runtime end-to-end tests" OFF)
if(CXPR_BUILD_CUDA_TESTS)
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        message(FATAL_ERROR "CXPR_BUILD_CUDA_TESTS=ON requires the CUDA toolkit and nvcc")
    endif()
    enable_language(CUDA)

    set(CXPR_CUDA_BULK_GENERATED
        "${CMAKE_CURRENT_BINARY_DIR}/cxpr_klein_gordon_generated.cuh")
    add_custom_command(
        OUTPUT "${CXPR_CUDA_BULK_GENERATED}"
        COMMAND $<TARGET_FILE:cxpr_cuda_bulk_codegen_tool>
            "${CMAKE_CURRENT_SOURCE_DIR}/../examples/bulk_grid/klein_gordon_cell.cxpr"
            "${CXPR_CUDA_BULK_GENERATED}"
        DEPENDS cxpr_cuda_bulk_codegen_tool
            "${CMAKE_CURRENT_SOURCE_DIR}/../examples/bulk_grid/klein_gordon_cell.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_cuda_bulk_fixture
        DEPENDS "${CXPR_CUDA_BULK_GENERATED}")
    add_executable(test_cuda_bulk_runtime cuda_bulk_runtime.cu)
    add_dependencies(test_cuda_bulk_runtime generate_cxpr_cuda_bulk_fixture)
    target_include_directories(test_cuda_bulk_runtime PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_cuda_bulk_runtime PRIVATE cxpr)
    set_target_properties(test_cuda_bulk_runtime PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME cuda_bulk_runtime COMMAND $<TARGET_FILE:test_cuda_bulk_runtime>)
    set_tests_properties(cuda_bulk_runtime PROPERTIES LABELS "cuda")

    set(CXPR_CUDA_BUFFER_GENERATED
        "${CMAKE_CURRENT_BINARY_DIR}/cxpr_rolling_buffer_generated.cuh")
    add_custom_command(
        OUTPUT "${CXPR_CUDA_BUFFER_GENERATED}"
        COMMAND $<TARGET_FILE:cxpr_cuda_bulk_codegen_tool>
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bulk/rolling_buffer.cxpr"
            "${CXPR_CUDA_BUFFER_GENERATED}"
            cxpr_rolling_buffer_tick
        DEPENDS cxpr_cuda_bulk_codegen_tool
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bulk/rolling_buffer.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_cuda_buffer_fixture
        DEPENDS "${CXPR_CUDA_BUFFER_GENERATED}")
    add_executable(test_cuda_bulk_buffer_runtime cuda_bulk_buffer_runtime.cu)
    add_dependencies(test_cuda_bulk_buffer_runtime generate_cxpr_cuda_buffer_fixture)
    target_include_directories(test_cuda_bulk_buffer_runtime PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_cuda_bulk_buffer_runtime PRIVATE cxpr)
    target_compile_definitions(test_cuda_bulk_buffer_runtime PRIVATE
        CXPR_ROLLING_BUFFER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bulk/rolling_buffer.cxpr")
    set_target_properties(test_cuda_bulk_buffer_runtime PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME cuda_bulk_buffer_runtime
        COMMAND $<TARGET_FILE:test_cuda_bulk_buffer_runtime>)
    set_tests_properties(cuda_bulk_buffer_runtime PROPERTIES LABELS "cuda")

    add_executable(cxpr_cuda_bulk_benchmark cuda_bulk_benchmark.cu)
    add_dependencies(cxpr_cuda_bulk_benchmark generate_cxpr_cuda_bulk_fixture)
    target_include_directories(cxpr_cuda_bulk_benchmark PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(cxpr_cuda_bulk_benchmark PRIVATE cxpr)
    target_compile_options(cxpr_cuda_bulk_benchmark PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:-O3>)
    set_target_properties(cxpr_cuda_bulk_benchmark PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME cuda_bulk_benchmark_smoke
        COMMAND $<TARGET_FILE:cxpr_cuda_bulk_benchmark> 262144 50 256 3)
    set_tests_properties(cuda_bulk_benchmark_smoke PROPERTIES LABELS "cuda;benchmark")

    set(CXPR_CUDA_OPTIMIZE_GENERATED
        "${CMAKE_CURRENT_BINARY_DIR}/cxpr_optimize_candidate_generated.cuh")
    add_custom_command(
        OUTPUT "${CXPR_CUDA_OPTIMIZE_GENERATED}"
        COMMAND $<TARGET_FILE:cxpr_cuda_bulk_codegen_tool>
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optimize/cuda_candidate.cxpr"
            "${CXPR_CUDA_OPTIMIZE_GENERATED}"
            cxpr_optimize_candidate_tick
        DEPENDS cxpr_cuda_bulk_codegen_tool
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optimize/cuda_candidate.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_cuda_optimize_fixture
        DEPENDS "${CXPR_CUDA_OPTIMIZE_GENERATED}")
    add_executable(test_cuda_optimize_runtime cuda_optimize_runtime.cu)
    add_dependencies(test_cuda_optimize_runtime generate_cxpr_cuda_optimize_fixture)
    target_include_directories(test_cuda_optimize_runtime PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_cuda_optimize_runtime PRIVATE cxpr)
    target_compile_definitions(test_cuda_optimize_runtime PRIVATE
        CXPR_CUDA_OPTIMIZE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/optimize/cuda_candidate.cxpr")
    set_target_properties(test_cuda_optimize_runtime PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME cuda_optimize_runtime COMMAND $<TARGET_FILE:test_cuda_optimize_runtime>)
    set_tests_properties(cuda_optimize_runtime PROPERTIES LABELS "cuda")

    set(CXPR_CUDA_RESAMPLE_GENERATED
        "${CMAKE_CURRENT_BINARY_DIR}/cxpr_resample_generated.cu")
    add_custom_command(
        OUTPUT "${CXPR_CUDA_RESAMPLE_GENERATED}"
        COMMAND ${CMAKE_COMMAND} -E env
            CXPR_CUDA_SOURCE_OUT=${CXPR_CUDA_RESAMPLE_GENERATED}
            $<TARGET_FILE:test_cuda_index_contract>
        DEPENDS test_cuda_index_contract
        VERBATIM)
    add_custom_target(generate_cxpr_cuda_resample_fixture
        DEPENDS "${CXPR_CUDA_RESAMPLE_GENERATED}")
    add_executable(test_cuda_resample_runtime cuda_resample_runtime.cu)
    add_dependencies(test_cuda_resample_runtime generate_cxpr_cuda_resample_fixture)
    target_include_directories(test_cuda_resample_runtime PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_cuda_resample_runtime PRIVATE cxpr)
    target_compile_definitions(test_cuda_resample_runtime PRIVATE
        CXPR_CUDA_RESAMPLE_GENERATED="${CXPR_CUDA_RESAMPLE_GENERATED}")
    set_target_properties(test_cuda_resample_runtime PROPERTIES
        CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME cuda_resample_runtime COMMAND $<TARGET_FILE:test_cuda_resample_runtime>)
    set_tests_properties(cuda_resample_runtime PROPERTIES LABELS "cuda")
endif()

if(TARGET cxpr_model_codegen)
    set(CXPR_DYNAMIC_HOST_FIXTURE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dynamic_host")
    set(CXPR_DYNAMIC_HOST_GENERATED_C
        "${CMAKE_CURRENT_BINARY_DIR}/dynamic_host.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_DYNAMIC_HOST_GENERATED_C}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CXPR_DYNAMIC_HOST_FIXTURE_DIR}/root.cxpr"
            --output "${CXPR_DYNAMIC_HOST_GENERATED_C}"
            --function dynamic_host_tick
        DEPENDS
            cxpr_model_codegen
            "${CXPR_DYNAMIC_HOST_FIXTURE_DIR}/root.cxpr"
            "${CXPR_DYNAMIC_HOST_FIXTURE_DIR}/counter.cxpr"
            "${CXPR_DYNAMIC_HOST_FIXTURE_DIR}/accumulator.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_dynamic_host_fixture
        DEPENDS "${CXPR_DYNAMIC_HOST_GENERATED_C}")
    add_dependencies(test_dynamic_host_e2e generate_cxpr_dynamic_host_fixture)
    target_include_directories(test_dynamic_host_e2e PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")

    set(CXPR_COMPOSITION_FANOUT_FIXTURE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition_fanout")
    set(CXPR_COMPOSITION_FANOUT_GENERATED_C
        "${CMAKE_CURRENT_BINARY_DIR}/composition_fanout.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_COMPOSITION_FANOUT_GENERATED_C}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}/fanout.cxpr"
            --output "${CXPR_COMPOSITION_FANOUT_GENERATED_C}"
            --function cxpr_composition_fanout_tick
        DEPENDS
            cxpr_model_codegen
            "${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}/fanout.cxpr"
            "${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}/producer.cxpr"
            "${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}/consumer_a.cxpr"
            "${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}/consumer_b.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_composition_fanout_fixture
        DEPENDS "${CXPR_COMPOSITION_FANOUT_GENERATED_C}")
    add_dependencies(test_composition_bulk_codegen_e2e
        generate_cxpr_composition_fanout_fixture)
    target_include_directories(test_composition_bulk_codegen_e2e PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(test_composition_bulk_codegen_e2e PRIVATE
        CXPR_COMPOSITION_FANOUT_FIXTURE_DIR="${CXPR_COMPOSITION_FANOUT_FIXTURE_DIR}")

    set(CXPR_BULK_GRID_EXAMPLE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../examples/bulk_grid")
    set(CXPR_BULK_GRID_GENERATED_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated_examples/bulk_grid")
    set(CXPR_BULK_GRID_GENERATED_C
        "${CXPR_BULK_GRID_GENERATED_DIR}/klein_gordon_cell.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_BULK_GRID_GENERATED_C}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CXPR_BULK_GRID_GENERATED_DIR}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CXPR_BULK_GRID_EXAMPLE_DIR}/klein_gordon_cell.cxpr"
            --output "${CXPR_BULK_GRID_GENERATED_C}"
            --function klein_gordon_cell_tick
        DEPENDS
            cxpr_model_codegen
            "${CXPR_BULK_GRID_EXAMPLE_DIR}/klein_gordon_cell.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_bulk_grid_e2e
        DEPENDS "${CXPR_BULK_GRID_GENERATED_C}")
    add_dependencies(test_bulk_codegen_e2e generate_cxpr_bulk_grid_e2e)
    target_include_directories(test_bulk_codegen_e2e PRIVATE
        "${CXPR_BULK_GRID_GENERATED_DIR}")

    set(CXPR_BULK_BUFFER_GENERATED_C
        "${CMAKE_CURRENT_BINARY_DIR}/rolling_buffer.gen.c")
    add_custom_command(
        OUTPUT "${CXPR_BULK_BUFFER_GENERATED_C}"
        COMMAND $<TARGET_FILE:cxpr_model_codegen>
            --model "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bulk/rolling_buffer.cxpr"
            --output "${CXPR_BULK_BUFFER_GENERATED_C}"
            --function rolling_buffer_tick
        DEPENDS cxpr_model_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bulk/rolling_buffer.cxpr"
        VERBATIM)
    add_custom_target(generate_cxpr_bulk_buffer_fixture
        DEPENDS "${CXPR_BULK_BUFFER_GENERATED_C}")
    add_dependencies(test_bulk_buffer_parity generate_cxpr_bulk_buffer_fixture)
    target_include_directories(test_bulk_buffer_parity PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")

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
