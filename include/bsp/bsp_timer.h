#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include "main.h"

#define BSP_SCHEDULE_TIMER     TIMER6
#define BSP_SCHEDULE_TIMER_RCU RCU_TIMER6
#define BSP_SCHEDULE_TIMER_IRQ TIMER6_IRQn

void bsp_schedule_timer_init(void);
void bsp_schedule_timer_set_once(uint16_t tick);
#if !(EKCFG_RTOS == 1 && EKCFG_OS_ISR_WORKQ == 1)
void bsp_schedule_timer_irq_handler(void);
#endif

#endif // BSP_TIMER_H
