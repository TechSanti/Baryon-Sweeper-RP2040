cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Baryon_Sweeper C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(USE_RP2040_ZERO "Build for RP2040-Zero with WS2812 NeoPixel" OFF)

if(USE_RP2040_ZERO)
    add_compile_definitions(BOARD_RP2040_ZERO)
    message(STATUS ">>> Configurado para pinagem do RP2040-Zero (NeoPixel GPIO16) <<<")
else()
    message(STATUS ">>> Configurado para Raspberry Pi Pico Padrão (LED GPIO25) <<<")
endif()

pico_sdk_init()

add_executable(${PROJECT_NAME}
    main.c
    keys.c
    tiny-AES-c/aes.c
)

if(USE_RP2040_ZERO)
    target_sources(${PROJECT_NAME} PRIVATE ws2812/ws2812.c)
    pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/ws2812/ws2812.pio)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_NEOPIXEL)
    target_include_directories(${PROJECT_NAME} PRIVATE ws2812)
    target_link_libraries(${PROJECT_NAME} hardware_pio)
endif()

pico_add_extra_outputs(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tiny-AES-c
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_watchdog
)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)