cmake_minimum_required(VERSION 3.20.0)

# Select our board 
set(BOARD rpi_pico2/rp2350a/m33)
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/sensor/ss49e
)

# Find/Select cmake package 'Zephyr' 
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# This is only used by IntelliSense inside VS Code 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# For using west flash with picoprobe
set(OPENOCD "openocd")
set(OPENOCD_DEFAULT_PATH "/usr/local/share/openocd/scripts")

# Define project name
project(upp_lock)

# Add source files
# FILE(GLOB SRC_FILES "src/*.c")
target_sources(app PRIVATE src/main.c)

add_subdirectory(drivers/sensor/mpu6050)
add_subdirectory(drivers/sensor/ltr303)
#add_subdirectory(drivers/sensor/rgb_led)
add_subdirectory(drivers/sensor/buzzer)