# Runs CMD --smoke-test --settings-file SETTINGS and fails unless it exits 0
# and its output matches EXPECT. PASS_REGULAR_EXPRESSION alone would ignore
# the exit code.
foreach(required CMD SETTINGS EXPECT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "run_and_expect.cmake: -D${required}=... is required")
  endif()
endforeach()
execute_process(COMMAND "${CMD}" --smoke-test --settings-file "${SETTINGS}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "exit code ${rc}\n${out}${err}")
endif()
if(NOT "${out}${err}" MATCHES "${EXPECT}")
  message(FATAL_ERROR "output does not match '${EXPECT}':\n${out}${err}")
endif()
