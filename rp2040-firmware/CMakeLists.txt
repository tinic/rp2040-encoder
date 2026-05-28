# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")
#set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.14...3.27)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.gitmodules")
    execute_process(COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# pico-vscode.cmake (if present) points PICO_SDK_PATH at a global SDK install
# that may not match this project's SDK version. Override to use the bundled submodule.
set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk")

set(PICO_SDK_FETCH_FROM_GIT on)
set(CMAKE_CXX_STANDARD 23)

include(pico-sdk/pico_sdk_init.cmake)

project(rp2040-hal-encoder)

pico_sdk_init()

add_executable(${CMAKE_PROJECT_NAME}
    main.cpp
    position.cpp
    usb_device.cpp
    quadrature_encoder.cpp
    ws2812_led.cpp
)

target_compile_options(${CMAKE_PROJECT_NAME} PUBLIC
    -Os
    -Wall
    -Wextra
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
    CFG_TUSB_MCU=OPT_MCU_RP2040
)

target_link_libraries(${CMAKE_PROJECT_NAME} 
    pico_stdlib
    hardware_pwm
    hardware_timer
    hardware_irq
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_watchdog
    tinyusb_device
    tinyusb_board
)

# Disable stdio USB since we're using TinyUSB directly
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 0)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 0)

pico_add_uf2_output(${CMAKE_PROJECT_NAME})
pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)
pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

find_package(Git REQUIRED)

execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_SHORT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_REV_COUNT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} show -s --format=%ad HEAD --date=iso-strict WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} show -s --format=%as HEAD --date=iso-strict WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_DATE_SHORT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --match "v*" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT GIT_VERSION)
    set(GIT_VERSION "${GIT_SHORT_SHA}")
endif()
message(STATUS "Building version: ${GIT_VERSION}")

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_BINARY_DIR}/version.h" @ONLY)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/version.h)

set(CMAKE_SIZE arm-none-eabi-size)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} ${CMAKE_PROJECT_NAME}.elf
    USES_TERMINAL)

set(FLASH_DEVICE_PATH "/media/$ENV{USER}/RPI-RP2" CACHE PATH "Path to RP2040 mass storage device")

add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_PROJECT_NAME}.uf2 ${FLASH_DEVICE_PATH}/${CMAKE_PROJECT_NAME}.uf2
    USES_TERMINAL
)

add_custom_target(flash-quick
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_PROJECT_NAME}.uf2 ${FLASH_DEVICE_PATH}/
    USES_TERMINAL
)

# `cmake --install <build-dir>` flashes the firmware to a connected RP2040
# via picotool. The pico-sdk's bundled picotool (in build/_deps/picotool)
# is hardcoded with PICOTOOL_NO_LIBUSB=1 in the SDK's Findpicotool.cmake
# -- it can only manipulate UF2 files, not talk to devices over USB. We
# build a second copy with USB support enabled, reusing the same
# upstream tag as the SDK pins. Requires libusb-1.0 development headers
# at build time (apt: libusb-1.0-0-dev).
#
# Flags: -f force-reboots the device from running firmware into BOOTSEL
# mode if needed; -x re-launches the new firmware after load so the
# device comes back up enumerated on the USB bus.
include(ExternalProject)

set(_picotool_usb_src ${CMAKE_BINARY_DIR}/picotool-usb-src)
set(_picotool_usb_bld ${CMAKE_BINARY_DIR}/picotool-usb-build)

ExternalProject_Add(picotool_usb_build
    GIT_REPOSITORY https://github.com/raspberrypi/picotool.git
    GIT_TAG ${picotoolVersion}
    GIT_SHALLOW TRUE
    SOURCE_DIR ${_picotool_usb_src}
    BINARY_DIR ${_picotool_usb_bld}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DPICO_SDK_PATH=${PICO_SDK_PATH}
        -DPICOTOOL_NO_LIBUSB=0
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${_picotool_usb_bld}/picotool
)

set(PICOTOOL_INSTALL_EXECUTABLE ${_picotool_usb_bld}/picotool)

# Ensure the USB-enabled picotool is built as part of `all` so that
# `cmake --install` after `cmake --build` finds the binary.
add_dependencies(${CMAKE_PROJECT_NAME} picotool_usb_build)

install(CODE "
    if(NOT EXISTS \"${PICOTOOL_INSTALL_EXECUTABLE}\")
        message(FATAL_ERROR \"USB-enabled picotool was not built. Did you run 'cmake --build' first? Path checked: ${PICOTOOL_INSTALL_EXECUTABLE}\")
    endif()
    message(STATUS \"Flashing ${CMAKE_PROJECT_NAME}.uf2 via ${PICOTOOL_INSTALL_EXECUTABLE}...\")
    execute_process(
        COMMAND \"${PICOTOOL_INSTALL_EXECUTABLE}\" load -f -x \"${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.uf2\"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR \"picotool failed (rc=\${_rc}). Ensure the RP2040 is connected via USB and the user has permission to access it (udev rules for 2e8a:* on Linux).\")
    endif()
")
