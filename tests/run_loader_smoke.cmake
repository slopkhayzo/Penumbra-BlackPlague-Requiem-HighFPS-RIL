if(NOT DEFINED HOST OR NOT DEFINED STAGE)
    message(FATAL_ERROR "HOST and STAGE are required")
endif()

file(REMOVE "${STAGE}/PenumbraBlackPlague.FPSFix.log")
execute_process(
    COMMAND "${HOST}"
    WORKING_DIRECTORY "${STAGE}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ASI loader smoke host exited with ${result}")
endif()
