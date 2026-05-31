cmake_minimum_required(VERSION 3.20.0)

# Add USB CDC console only when building the RP2040 Pico controller.
# Build with: west build -b rpi_pico <path>/controller_node --pristine
if("${BOARD}" STREQUAL "rpi_pico")
    set(EXTRA_CONF_FILE "rpi_pico_usb.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(upp_lock_controller_uart)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

target_include_directories(app PRIVATE
    src
)

target_sources(app PRIVATE
    src/main.c
    src/controller_uart.c
)
