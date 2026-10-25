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
set(CMAKE_PROJECT_NAME g030_mainboard)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
option(TUYA_PROGRAMMING_BRIDGE "Build dedicated fixed-baud Tuya programming bridge" OFF)
option(TUYA_PROTOCOL_APP "Build mainboard Tuya app with bounded motor control" OFF)
option(PRODUCT_FIRMWARE "Build product motion, calibration, local keys and Tuya control" OFF)
option(TRQ_CAPTURE_ONLY "Local-only constant-current TRQ capture; no EEPROM commits" OFF)
set(TRQ_CAPTURE_UP_REVERSE "0" CACHE STRING "Physically confirmed UP DIR level for capture (0/1)")
set(PRODUCT_UP_REVERSE "0" CACHE STRING "User-confirmed UP DIR level for this Product assembly (0/1)")
if(PRODUCT_FIRMWARE AND NOT PRODUCT_UP_REVERSE MATCHES "^[01]$")
    message(FATAL_ERROR "Product requires the physically confirmed UP direction 0/1")
endif()
if(TRQ_CAPTURE_ONLY AND (NOT PRODUCT_FIRMWARE OR NOT TRQ_CAPTURE_UP_REVERSE MATCHES "^[01]$"))
    message(FATAL_ERROR "TRQ capture requires Product and a confirmed UP direction 0/1")
endif()
if(PRODUCT_FIRMWARE AND (NOT TUYA_PROTOCOL_APP OR TUYA_PROGRAMMING_BRIDGE))
    message(FATAL_ERROR "Product requires TUYA_PROTOCOL_APP and excludes the programming bridge")
endif()
if(TUYA_PROGRAMMING_BRIDGE AND TUYA_PROTOCOL_APP)
    message(FATAL_ERROR "Select either transport bridge or protocol app")
endif()
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
if(NOT TUYA_PROTOCOL_APP)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/serial_bringup.c
    App/board_test.c
    App/eeprom.c
    App/eeprom_policy_test.c
    App/motor.c
    App/motor_tune.c
    App/music.c
    App/switch_control.c
)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(TUYA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tuya_generated/2026-07-16"
        CACHE PATH "Path to your legally obtained compatible Tuya MCU SDK")
    if(NOT EXISTS "${TUYA_SDK_DIR}/protocol.c")
        message(FATAL_ERROR "Tuya SDK missing. Set -DTUYA_SDK_DIR=<your-sdk-directory>; see docs/tuya-setup.md. Debug/TuyaBridge builds do not need this SDK.")
    endif()
    set(TUYA_PORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/tuya_sdk")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../g030_tuya/prepare_tuya_sdk.py"
        "${TUYA_SDK_DIR}"
        "${TUYA_PORT_DIR}" COMMAND_ERROR_IS_FATAL ANY)
    if(PRODUCT_FIRMWARE)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/eeprom.c App/music.c App/motor_tune.c
            App/product/motion.c App/product/product_motor.c
            App/product/product.c App/product/product_button.c App/product/product_app.c App/product/bridge.c)
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE App/product)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PRODUCT_FIRMWARE=1
            LIFT_CONFIRMED_DIRECTION=1 LIFT_CONFIRMED_UP_REVERSE=${PRODUCT_UP_REVERSE})
        if(TRQ_CAPTURE_ONLY)
            target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
                LIFT_TRQ_CAPTURE=1 LIFT_CAPTURE_UP_REVERSE=${TRQ_CAPTURE_UP_REVERSE})
        endif()
        target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wextra -Werror)
    else()
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/motor.c App/motor_tune.c
            App/tuya_protocol/bridge.c App/tuya_protocol/tuya_app.c)
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${TUYA_PORT_DIR}/mcu_api.c" "${TUYA_PORT_DIR}/protocol.c" "${TUYA_PORT_DIR}/system.c")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE App/tuya_protocol "${TUYA_PORT_DIR}")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TUYA_PROTOCOL_APP=1)
endif()

# Keep callable application entry points before the Tuya transport is enabled.
if(TUYA_PROGRAMMING_BRIDGE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/tuya_bridge.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TUYA_PROGRAMMING_BRIDGE=1)
elseif(NOT TUYA_PROTOCOL_APP)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-Wl,-u,Music_SaveScore,-u,Music_GetScore,-u,Music_PlayStored,-u,Music_Stop")
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
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
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    COMMENT "Generating HEX and BIN images")
