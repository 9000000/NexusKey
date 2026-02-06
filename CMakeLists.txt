cmake_minimum_required(VERSION 3.20)
project(NextKey LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific configurations
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)
endif()

# Global Include Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tsf
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
)

# Core Library (Engine + Shared State + Config)
add_library(NextKeyCore STATIC
    src/core/SharedStateManager.cpp
    src/core/SharedStateManager.h
    src/core/SharedState.h
    src/core/ConfigEvent.cpp
    src/core/ConfigEvent.h
    src/core/TypingConfig.h
    src/core/config/ConfigManager.cpp
    src/core/config/ConfigManager.h
    src/core/engine/TelexEngine.cpp
    src/core/engine/TelexEngine.h
    src/core/engine/TelexEngineAdapter.cpp
    src/core/engine/TelexEngineAdapter.h
    src/core/engine/VniEngine.cpp
    src/core/engine/VniEngine.h
    src/core/engine/EngineFactory.cpp
    src/core/engine/EngineFactory.h
    src/core/engine/KeyTranslator.cpp
    src/core/engine/KeyTranslator.h
    src/core/engine/Telex.h
    src/core/engine/TelexMaps.h
    src/core/engine/TelexData.h
    src/core/engine/IInputEngine.h
)

# TSF DLL (TIP)
add_library(NextKeyTSF SHARED
    src/tsf/stdafx.h
    src/tsf/Globals.h
    src/tsf/Globals.cpp
    src/tsf/TextService.h
    src/tsf/TextService.cpp
    src/tsf/KeyEventSink.h
    src/tsf/KeyEventSink.cpp
    src/tsf/CompositionManager.h
    src/tsf/CompositionManager.cpp
    src/tsf/EngineController.h
    src/tsf/EngineController.cpp
    src/tsf/KeyHandler.h
    src/tsf/KeyHandler.cpp
    src/tsf/Register.cpp
    src/tsf/dllmain.cpp
    src/tsf/EditSession.h
    src/tsf/CompositionEditSession.h
    src/tsf/DisplayAttribute.h
    src/tsf/ComUtils.h
    src/tsf/Define.h
    src/tsf/NextKeyTSF.def
)
target_link_libraries(NextKeyTSF PRIVATE NextKeyCore)
set_target_properties(NextKeyTSF PROPERTIES 
    LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/tsf/NextKeyTSF.def\""
)

# Core App (EXE) - Win32 GUI application with Sciter UI
add_executable(NextKeyApp WIN32
    src/app/main.cpp
    src/app/TrayIcon.h
    src/app/TrayIcon.cpp
    src/app/SettingsDialog.h
    src/app/SettingsDialog.cpp
    src/app/SciterArchive.h
    src/app/SciterArchive.cpp
    src/app/helpers/ScaleHelper.h
    src/app/helpers/SciterHelper.h
    src/app/helpers/SciterHelper.cpp
    src/app/resource.h
    src/app/NexusKey.rc
    extern/sciter/include/sciter-main.cpp
)
target_include_directories(NextKeyApp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/include
)
target_compile_definitions(NextKeyApp PRIVATE SKIP_MAIN)
target_link_libraries(NextKeyApp PRIVATE NextKeyCore)

# Link Windows libraries
if(WIN32)
    target_link_libraries(NextKeyTSF PRIVATE advapi32 user32 ole32 oleaut32 uuid)
    target_link_libraries(NextKeyApp PRIVATE advapi32 user32 ole32 shell32 dwmapi)
endif()

# ============================================================================
# Sciter UI Resources
# Always copy UI files to output directory (works for both Debug and Release)
# For Release with embedded resources, set SCITER_USE_PACKFOLDER manually
# ============================================================================

# Copy UI files to output directory after each build
add_custom_command(TARGET NextKeyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app/ui"
        "$<TARGET_FILE_DIR:NextKeyApp>/ui"
    COMMENT "Copying UI files to output directory"
)

# Optional: For Release builds with embedded resources
# 1. Run packfolder.exe manually: packfolder.exe src/app/ui src/app/resources.cpp -v resources
# 2. Add -DSCITER_USE_PACKFOLDER=ON to cmake configure
# 3. Rebuild
option(SCITER_USE_PACKFOLDER "Use packfolder embedded resources instead of file copy" OFF)
if(SCITER_USE_PACKFOLDER)
    target_compile_definitions(NextKeyApp PRIVATE SCITER_USE_PACKFOLDER)
    message(STATUS "Sciter: Using packfolder embedded resources")
else()
    message(STATUS "Sciter: Using file copy mode (ui/ folder next to exe)")
endif()

# ============================================================================
# Testing with Google Test (vendored in extern/)
# ============================================================================
# Google Test must be manually placed in extern/googletest/
# Download from: https://github.com/google/googletest/releases/tag/v1.14.0
# Extract googletest-1.14.0 folder contents to extern/googletest/

set(GOOGLETEST_VERSION "1.14.0" CACHE STRING "Google Test version")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(extern/googletest EXCLUDE_FROM_ALL)

enable_testing()

add_executable(NextKeyTests
    tests/TelexEngineTest.cpp
    tests/ConfigManagerTest.cpp
    tests/VniEngineTest.cpp
    tests/EngineFactoryTest.cpp
    tests/ConfigEventTest.cpp
)
target_link_libraries(NextKeyTests PRIVATE NextKeyCore GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(NextKeyTests)
