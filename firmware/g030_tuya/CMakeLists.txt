cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME g030_tuya)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# The Tuya download is a local generated snapshot and is deliberately not
# committed. Recreate a patched build copy on every configure.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TUYA_SNAPSHOT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tuya_generated/2026-07-16"
    CACHE PATH "Path to the locally downloaded Tuya MCU SDK snapshot")
if(NOT EXISTS "${TUYA_SNAPSHOT_DIR}/protocol.c")
    message(FATAL_ERROR "Tuya MCU SDK snapshot missing: ${TUYA_SNAPSHOT_DIR}")
endif()
set(TUYA_PORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/tuya_sdk")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_tuya_sdk.py"
            "${TUYA_SNAPSHOT_DIR}" "${TUYA_PORT_DIR}"
    RESULT_VARIABLE TUYA_PORT_RESULT
    ERROR_VARIABLE TUYA_PORT_ERROR)
if(NOT TUYA_PORT_RESULT EQUAL 0)
    message(FATAL_ERROR "Tuya SDK port failed: ${TUYA_PORT_ERROR}")
endif()

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/bridge.c
    "${TUYA_PORT_DIR}/mcu_api.c"
    "${TUYA_PORT_DIR}/protocol.c"
    "${TUYA_PORT_DIR}/system.c"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    "${TUYA_PORT_DIR}"
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    COMMENT "Generating HEX and BIN images")
