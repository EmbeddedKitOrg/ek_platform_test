#include "gd32f4xx_rcu.h"
#include "ek/core/ek_export.h"

void system_init(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE0_SUB4);
    if (SysTick_Config(SystemCoreClock / 1000U))
    {
        while (1)
        {
        }
    }
    nvic_irq_enable(SysTick_IRQn, 0, 15);
}

EK_EXPORT_BOOT(system_init, 0);
