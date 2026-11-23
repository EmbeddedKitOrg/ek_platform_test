cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_SOURCE_DIR}/lib/ek_platform/cmake/platform.cmake)
ek_platform_prepare(ARCH cm4 PICOLIBC ON BUILD_TYPE Debug)
set(PICOLIBC_FETCH ON)

project(main LANGUAGES C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib/gd32f4xx_driver)

set(INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bsp
)

# 收集源文件
file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

set(EK_BENCH_TM "OFF" CACHE STRING "Thread-Metric benchmark case: OFF|basic|cooperative|preemptive|interrupt|interrupt_preemption|message|synchronization|memory")
set(EK_BENCH_TM_DURATION "30" CACHE STRING "Thread-Metric report interval in seconds")
set(EK_BENCH_LM "OFF" CACHE BOOL "Build Zephyr-style latency_measure micro-benchmark")
set(EK_BENCH_LM_ITERS "1000" CACHE STRING "Default iteration count for latency_measure")
set(EK_TEST_STACKOVERFLOW "OFF" CACHE BOOL "Build RTOS stack overflow & high-water-mark test")

if(NOT EK_BENCH_TM STREQUAL "OFF" AND EK_BENCH_LM)
    message(FATAL_ERROR "EK_BENCH_TM and EK_BENCH_LM cannot be enabled simultaneously")
endif()

if(EK_TEST_STACKOVERFLOW AND (NOT EK_BENCH_TM STREQUAL "OFF" OR EK_BENCH_LM))
    message(FATAL_ERROR "EK_TEST_STACKOVERFLOW cannot be enabled with benchmarks")
endif()

# 创建可执行文件目标
add_executable(${CMAKE_PROJECT_NAME} ${SRCS})

if(EK_TEST_STACKOVERFLOW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE EK_RUN_STACKOVERFLOW_TEST=1)
endif()

# 按需接入 latency_measure 微基准测试
if(EK_BENCH_LM)
    set(_lm_dir "${CMAKE_CURRENT_SOURCE_DIR}/bench/latency_measure")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${_lm_dir}/lm_main.c"
        "${_lm_dir}/lm_cases.c"
    )
    list(APPEND INC_DIRS "${_lm_dir}")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        EK_RUN_LATENCY_MEASURE=1
        LM_BENCH_ITERS=${EK_BENCH_LM_ITERS}
    )
endif()

# 按需接入 Thread-Metric 基准测试
if(NOT EK_BENCH_TM STREQUAL "OFF")
    set(_tm_dir "${CMAKE_CURRENT_SOURCE_DIR}/bench/thread_metric")
    set(_tm_vendor "${_tm_dir}/vendor")
    if(EK_BENCH_TM STREQUAL "basic")
        set(_tm_test "${_tm_vendor}/tm_basic_processing_test.c")
    elseif(EK_BENCH_TM STREQUAL "cooperative")
        set(_tm_test "${_tm_vendor}/tm_cooperative_scheduling_test.c")
    elseif(EK_BENCH_TM STREQUAL "preemptive")
        set(_tm_test "${_tm_vendor}/tm_preemptive_scheduling_test.c")
    elseif(EK_BENCH_TM STREQUAL "interrupt")
        set(_tm_test "${_tm_vendor}/tm_interrupt_processing_test.c")
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TM_BENCH_CASE_INTERRUPT=1)
    elseif(EK_BENCH_TM STREQUAL "interrupt_preemption")
        set(_tm_test "${_tm_vendor}/tm_interrupt_preemption_processing_test.c")
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TM_BENCH_CASE_INTERRUPT_PREEMPTION=1)
    elseif(EK_BENCH_TM STREQUAL "message")
        set(_tm_test "${_tm_vendor}/tm_message_processing_test.c")
    elseif(EK_BENCH_TM STREQUAL "synchronization")
        set(_tm_test "${_tm_vendor}/tm_synchronization_processing_test.c")
    elseif(EK_BENCH_TM STREQUAL "memory")
        set(_tm_test "${_tm_vendor}/tm_memory_allocation_test.c")
    else()
        message(FATAL_ERROR "Unknown EK_BENCH_TM='${EK_BENCH_TM}'. Expected: OFF|basic|cooperative|preemptive|interrupt|interrupt_preemption|message|synchronization|memory")
    endif()

    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${_tm_dir}/tm_porting_layer.c"
        "${_tm_test}"
    )
    list(APPEND INC_DIRS "${_tm_dir}" "${_tm_vendor}")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        EK_RUN_THREAD_METRIC=1
        TM_TEST_DURATION=${EK_BENCH_TM_DURATION}
    )
    set_source_files_properties("${_tm_test}" PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-strict-prototypes;-Wno-sign-compare"
    )
endif()

# ek_platform 子模块
ek_platform_configure(
    CONF "${CMAKE_CURRENT_SOURCE_DIR}/include/ek_conf.h"
    BOARD "${CMAKE_CURRENT_SOURCE_DIR}/include/ek_board.h"
    SOC "${CMAKE_CURRENT_SOURCE_DIR}/include/ek_soc.h"
    SOC_TARGET gd32f4xx_driver
)
add_subdirectory(lib/ek_platform)

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
)
