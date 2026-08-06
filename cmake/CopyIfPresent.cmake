# Copy SOURCE to DESTINATION when SOURCE exists, and say nothing when it does not.
#
# Used for vkey_engine.dll.sig: a published engine ships one, a locally synced
# engine does not, and the build must work either way — Debug falls back to the
# lock hash. `copy_if_different` cannot express "skip when absent".
if(EXISTS "${SOURCE}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DESTINATION}")
endif()
