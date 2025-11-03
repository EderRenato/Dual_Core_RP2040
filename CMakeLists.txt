cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(Dual_Core C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT_NAME} main.c 
        lib/aht20.c 
        lib/bmp280.c 
        lib/ssd1306.c
        )

pico_set_program_name(${PROJECT_NAME} "Dual_Core")
pico_set_program_version(${PROJECT_NAME} "0.1")

# Generate PIO header
pico_generate_pio_header(${PROJECT_NAME} 
                        ${CMAKE_CURRENT_LIST_DIR}/lib/ws2818b.pio
                        )

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# Add the standard library to the build
target_link_libraries(${PROJECT_NAME}
        pico_stdlib
        pico_multicore
)

# Add the standard include files to the build
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(${PROJECT_NAME}
        hardware_i2c
        hardware_gpio
        hardware_pio
        )

pico_add_extra_outputs(${PROJECT_NAME})
