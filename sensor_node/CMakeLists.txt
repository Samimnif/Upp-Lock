cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_node)

target_sources(app PRIVATE
    src/main.c
    ../drivers/sensor/ltr303/ltr303.c
    ../drivers/sensor/ss49e/ss49e.c
)

add_subdirectory(../drivers/sensor/mpu6050 mpu6050_driver)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)