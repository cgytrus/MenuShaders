cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "x86_64")

project(MenuShaders VERSION 1.2.0)

if (APPLE)
    file(GLOB SOURCES
        src/*.cpp
        src/*.mm
    )
else ()
    file(GLOB SOURCES
        src/*.cpp
    )
endif ()

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

if(WIN32)
    target_link_libraries(${PROJECT_NAME} opengl32)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL -framework Cocoa")
elseif(ANDROID)
    target_link_libraries(${PROJECT_NAME} GLESv2)
endif()

CPMAddPackage("gh:hanickadot/compile-time-regular-expressions#9725886")
target_link_libraries(${PROJECT_NAME} ctre)
