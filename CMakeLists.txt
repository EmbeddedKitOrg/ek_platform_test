cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_SOURCE_DIR}/lib/ek_platform/cmake/ek_platform.cmake)
ek_platform_prepare(ARCH cm4 PICOLIBC ON)
set(PICOLIBC_FETCH ON)

project(main LANGUAGES C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ek_platform 子模块
ek_platform_configure(
    CONF "${CMAKE_CURRENT_SOURCE_DIR}/include/ek_conf.h"
    BOARD "${CMAKE_CURRENT_SOURCE_DIR}/include/ek_board.h")
add_subdirectory(lib/ek_platform)
add_subdirectory(lib/gd32f4xx_driver)

set(INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bsp
)

# 收集源文件
file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# 创建可执行文件目标
add_executable(${CMAKE_PROJECT_NAME} ${SRCS})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${INC_DIRS}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    ek_platform
    gd32f4xx_driver
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -fdiagnostics-color=always
    -Wall
    -Wextra
    -g
)
