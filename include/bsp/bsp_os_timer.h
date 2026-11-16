/**
 * @file bsp_os_timer.h
 * @brief RTOS 时间源（DWT 自由计数）与单次闹钟（TIMER5）
 */

#ifndef BSP_OS_TIMER_H
#define BSP_OS_TIMER_H

#include "main.h"
#include "ek/conf_internal.h"

#if EKCFG_RTOS == 1

#    include "ek/os/kernel.h"

/**
 * @brief 内核时间源与单次闹钟操作表
 *
 * - now：把 DWT->CYCCNT（32 位 CPU 周期计数）扩展成 64 位后换算为微秒；
 * - set_timeout：TIMER5 单次触发，到期后调用 ek_os_kernel_announce_isr()。
 *
 * @note 时间单位为微秒；delta < 0 表示停表。
 */
extern const ek_os_timer_ops_t g_bsp_os_timer_ops;

#endif /* EKCFG_RTOS == 1 */

#endif /* BSP_OS_TIMER_H */
