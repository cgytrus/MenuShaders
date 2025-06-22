cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(MenuShaders VERSION 1.4.3)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

if (GEODE_TARGET_PLATFORM STREQUAL "Win64")
    target_link_libraries(${PROJECT_NAME} opengl32)
elseif (GEODE_TARGET_PLATFORM STREQUAL "Android32")
    target_link_libraries(${PROJECT_NAME} GLESv2)
elseif (GEODE_TARGET_PLATFORM STREQUAL "Android64")
    target_link_libraries(${PROJECT_NAME} GLESv2)
elseif (GEODE_TARGET_PLATFORM STREQUAL "MacOS")
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL -framework Cocoa")
elseif (GEODE_TARGET_PLATFORM STREQUAL "iOS")
    #target_link_libraries(${PROJECT_NAME} "-framework OpenGLESa")
endif()

CPMAddPackage("gh:hanickadot/compile-time-regular-expressions#eb9577a")
target_link_libraries(${PROJECT_NAME} ctre)
