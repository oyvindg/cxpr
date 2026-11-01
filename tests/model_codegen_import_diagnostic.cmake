execute_process(
    COMMAND "${CODEGEN}" --model "${FIXTURE_DIR}/root.cxpr"
        --output "${OUTPUT_FILE}" --function diagnostic_tick
    RESULT_VARIABLE result
    ERROR_VARIABLE diagnostic)

if(result EQUAL 0)
    message(FATAL_ERROR "cxpr_model_codegen unexpectedly accepted an invalid import")
endif()

foreach(expected
        "URI: ${FIXTURE_DIR}/broken.cxpr"
        "Model: broken_model"
        "Source:"
        "2 | out value ="
        "Import chain: ${FIXTURE_DIR}/root.cxpr -> ${FIXTURE_DIR}/child.cxpr -> ${FIXTURE_DIR}/broken.cxpr")
    string(FIND "${diagnostic}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}' in diagnostic:\n${diagnostic}")
    endif()
endforeach()
