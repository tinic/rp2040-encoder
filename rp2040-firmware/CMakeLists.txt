# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")
#set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13...3.27)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.gitmodules")
    execute_process(COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

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
