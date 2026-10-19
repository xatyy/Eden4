# cmake -DOELF=<file.oelf> -P check-oelf.cmake
# create-fself can duplicate or omit split writable/RELRO segments. Check every
# allocated section's memory and initialized file image, plus the entry point.
find_program(PS4_OELF_PYTHON NAMES python3 REQUIRED)
execute_process(COMMAND ${PS4_OELF_PYTHON} ${CMAKE_CURRENT_LIST_DIR}/check-oelf.py ${OELF}
    RESULT_VARIABLE coverage_rc OUTPUT_VARIABLE coverage_out ERROR_VARIABLE coverage_err)
if (NOT coverage_rc EQUAL 0)
    message(FATAL_ERROR "check-oelf: invalid runtime section coverage:\n${coverage_out}${coverage_err}")
endif ()
message(STATUS "${coverage_out}")
