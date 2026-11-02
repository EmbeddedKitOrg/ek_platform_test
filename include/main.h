#ifndef __MAIN_H
#define __MAIN_H

#include "gd32f4xx.h"
#include "gd32f4xx_libopt.h"
#include "systick.h"
#include <ek/core/ek_def.h>

/* 主控/BSP 导出顺序，属于具体板级工程，不放进 ek_platform。 */
#define EK_EXPORT_ORDER_BOOT_SYSTEM (0)
#define EK_EXPORT_ORDER_BOARD_TIMER (0)
#define EK_EXPORT_ORDER_BOARD_UART  (1)
#define EK_EXPORT_ORDER_BOARD_LED   (2)

#endif /* __MAIN_H */
