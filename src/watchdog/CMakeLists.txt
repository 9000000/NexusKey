# NexusKey Watchdog — process supervisor for crash auto-respawn.
# Built only on Windows.

if(NOT WIN32)
    return()
endif()

add_executable(NexusKeyWatchdog WIN32
    main.cpp
)

target_compile_definitions(NexusKeyWatchdog PRIVATE
    UNICODE _UNICODE
)

target_link_libraries(NexusKeyWatchdog PRIVATE
    psapi.lib
)

if(MSVC)
    target_link_options(NexusKeyWatchdog PRIVATE /SUBSYSTEM:WINDOWS,6.01 /RELEASE)
endif()

set_target_properties(NexusKeyWatchdog PROPERTIES
    OUTPUT_NAME "NexusKeyWatchdog"
)
