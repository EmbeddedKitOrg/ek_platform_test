/**
 * @file bsp_os_timer.h
 * @brief RTOS 时间源与单次闹钟（单 32 位通用定时器 TIMER4）
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
 * - now：读取 32 位 TIMER4 自由计数并结合更新溢出中断扩展为 64 位单调微秒时间；
 * - set_timeout：通过 TIMER4 CH0 比较中断触发单次唤醒，到期后调用 ek_os_kernel_announce_isr()。
 *
 * @note 时间单位为微秒；delta < 0 表示关闭闹钟但保持时间源继续运行。
 */
extern const ek_os_timer_ops_t g_bsp_os_timer_ops;

#endif /* EKCFG_RTOS == 1 */

#endif /* BSP_OS_TIMER_H */
