# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(proj C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(proj
    src/main.c
    src/core0_security.c
    src/core1_interface.c
    src/sensors.c
    src/alarm.c
    src/display.c
)

pico_set_program_name(proj "proj")
pico_set_program_version(proj "0.1")

# UART habilitado; USB habilitado
pico_enable_stdio_uart(proj 1)
pico_enable_stdio_usb(proj 1)

# Diretório raiz do projeto no include path
target_include_directories(proj PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Bibliotecas
target_link_libraries(proj
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_gpio
    hardware_adc
    hardware_pwm
    hardware_i2c
    hardware_timer
    hardware_dma        # ← adicionado para DMA no display
    pico_float          # ← sqrtf / powf sem soft-float
)

pico_add_extra_outputs(proj)
