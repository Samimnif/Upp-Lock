cmake_minimum_required(VERSION 3.20.0)

# Select our board 
set(BOARD rpi_pico2/rp2350a/m33)


# Find/Select cmake package 'Zephyr' 
find_package(Zephyr)

# This is only used by IntelliSense inside VS Code 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# For using west flash with picoprobe
# set(OPENOCD "openocd")
# set(OPENOCD_DEFAULT_PATH "/usr/local/share/openocd/scripts")

# Define project name
project(mpu6050_driver)

# Add include directory 
target_include_directories(app PRIVATE
    src/inc
    src/comm
)

# Add source files
# FILE(GLOB SRC_FILES "src/*.c")
target_sources(app PRIVATE
    src/main.c
    src/comm/uart_comm.c
    src/comm/command_handler.c
    src/comm/command_thread.c
)