# Runs from the dependency source dir. Skips the patch when it is already applied so reconfigures stay quiet.
execute_process(
    COMMAND git apply --reverse --check "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT already_applied EQUAL 0)
    execute_process(COMMAND git apply "${PATCH_FILE}" COMMAND_ERROR_IS_FATAL ANY)
endif()
