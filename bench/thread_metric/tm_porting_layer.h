#ifndef TM_PORTING_LAYER_H
#define TM_PORTING_LAYER_H

#include "ek/core/io.h"
#if EKCFG_IO_LWPRTF == 1
#    define printf ek_printf
#else
#    include <stdio.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 触发一次软件中断以执行 Thread-Metric 中断测试用例
 */
void tm_port_cause_interrupt(void);

#define TM_CAUSE_INTERRUPT tm_port_cause_interrupt();

#ifdef __cplusplus
}
#endif

#endif /* TM_PORTING_LAYER_H */
