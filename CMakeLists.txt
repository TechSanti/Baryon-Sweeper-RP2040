cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH não está definido. Configure o caminho do Pico SDK antes de compilar.")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Baryon_Sweeper C CXX ASM)

# O Pico SDK 1.5.x usa extensões GNU, incluindo typeof, em arquivos internos.
# Por isso o projeto deve usar GNU11, e não ISO C11 estrito.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(USE_RP2040_ZERO "Compilar para RP2040-Zero com WS2812 no GPIO16" OFF)
option(ENABLE_DEBUG_LOG "Habilitar logs detalhados pela USB" OFF)
option(LEGACY_WIRE_COMPAT "Manter o comportamento do firmware original para máxima compatibilidade" ON)
option(STRICT_RX_CHECKSUM "Descartar pedidos do PSP com checksum diferente" OFF)

pico_sdk_init()

set(BARYON_SOURCES
    main.c
    keys.c
    tiny-AES-c/aes.c
)

add_executable(${PROJECT_NAME} ${BARYON_SOURCES})

# Avisos aplicados somente aos arquivos do projeto. Não use -Wpedantic nos
# fontes internos do Pico SDK 1.5.x, pois eles utilizam extensões GNU válidas.
set_source_files_properties(${BARYON_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tiny-AES-c
)

# O projeto utiliza somente AES-128 ECB e CBC.
target_compile_definitions(${PROJECT_NAME} PRIVATE CTR=0)

if(ENABLE_DEBUG_LOG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_LOG=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_LOG=0)
endif()

if(LEGACY_WIRE_COMPAT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LEGACY_CHALLENGE_MAP=1
        LEGACY_SERIAL_PACKET=1
    )
    message(STATUS ">>> Compatibilidade original ativada <<<")
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LEGACY_CHALLENGE_MAP=0
        LEGACY_SERIAL_PACKET=0
    )
    message(STATUS ">>> Mapeamento de referência ativado <<<")
endif()

if(STRICT_RX_CHECKSUM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STRICT_RX_CHECKSUM=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE STRICT_RX_CHECKSUM=0)
endif()

if(USE_RP2040_ZERO)
    target_sources(${PROJECT_NAME} PRIVATE ws2812/ws2812.c)
    set_source_files_properties(ws2812/ws2812.c PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra"
    )

    pico_generate_pio_header(
        ${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/ws2812/ws2812.pio
    )

    target_include_directories(${PROJECT_NAME} PRIVATE ws2812)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        BOARD_RP2040_ZERO=1
        USE_NEOPIXEL=1
        NEOPIXEL_PIN=16
    )
    target_link_libraries(${PROJECT_NAME} hardware_pio hardware_clocks)
    message(STATUS ">>> RP2040-Zero: NeoPixel WS2812 no GPIO16 <<<")
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE LED_PIN=25)
    message(STATUS ">>> Raspberry Pi Pico padrão: LED no GPIO25 <<<")
endif()

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_uart
    hardware_watchdog
)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_add_extra_outputs(${PROJECT_NAME})
