foreach(required CLI NATIVE_BASELINE FIXTURE SOURCE_NAME WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing required argument: ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(native_output "${WORK_DIR}/doc_tooling_native.json")
set(cli_output "${WORK_DIR}/doc_tooling_cli.json")

execute_process(
    COMMAND "${NATIVE_BASELINE}" "${FIXTURE}" "${SOURCE_NAME}"
    OUTPUT_FILE "${native_output}"
    ERROR_VARIABLE native_error
    RESULT_VARIABLE native_result)
if(NOT native_result EQUAL 0)
    message(FATAL_ERROR
        "native document analysis failed (${native_result}): ${native_error}")
endif()

execute_process(
    COMMAND "${CLI}" --source-name "${SOURCE_NAME}"
    INPUT_FILE "${FIXTURE}"
    OUTPUT_FILE "${cli_output}"
    ERROR_VARIABLE cli_error
    RESULT_VARIABLE cli_result)
if(NOT cli_result EQUAL 0)
    message(FATAL_ERROR
        "cxpr_document_tooling failed (${cli_result}): ${cli_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${native_output}" "${cli_output}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "CLI stdout differs byte-for-byte from the native library baseline; "
        "outputs are in ${WORK_DIR}")
endif()
