# CMakeLists.txt — Zephyr RTOS Build Configuration
# CPR External AED Shock Controller (ESP32-C3)
# Protocol: ESP-NOW only (No BLE)

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpr_zephyr_aed_controller)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
    src/main.c
    src/aed_button.c
    src/aed_led.c
    src/aed_espnow.c
)
