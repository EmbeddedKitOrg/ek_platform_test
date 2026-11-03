cmake_minimum_required(VERSION 3.20)
project(gd32f4xx_driver C)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/standard_peripheral GD32F4XX_PERIPH_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/usb_library GD32F4XX_USB_SRCS)

add_library(gd32f4xx_driver STATIC
    ${GD32F4XX_PERIPH_SRCS}
    # ${GD32F4XX_USB_SRCS}
)

target_include_directories(gd32f4xx_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/standard_peripheral
    # ${CMAKE_CURRENT_SOURCE_DIR}/include/usb_library
    ${CMAKE_CURRENT_SOURCE_DIR}/cmsis
)

target_compile_definitions(gd32f4xx_driver PUBLIC
    GD32F470
)

target_compile_options(gd32f4xx_driver PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
)
