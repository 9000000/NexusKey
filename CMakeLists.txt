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
)

# Core Library (Engine + Shared State)
add_library(NextKeyCore STATIC
    src/core/SharedStateManager.cpp
    src/core/SharedStateManager.h
    src/core/SharedState.h
    src/core/TypingConfig.h
    src/core/engine/TelexEngine.cpp
    src/core/engine/TelexEngine.h
    src/core/engine/TelexEngineAdapter.cpp
    src/core/engine/TelexEngineAdapter.h
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

# Core App (EXE)
add_executable(NextKeyApp
    src/app/main.cpp
)
target_link_libraries(NextKeyApp PRIVATE NextKeyCore)

# Link Windows libraries
if(WIN32)
    target_link_libraries(NextKeyTSF PRIVATE advapi32 user32 ole32 oleaut32 uuid)
    target_link_libraries(NextKeyApp PRIVATE advapi32 user32 ole32)
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
)
target_link_libraries(NextKeyTests PRIVATE NextKeyCore GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(NextKeyTests)
