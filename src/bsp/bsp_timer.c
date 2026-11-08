#include "bsp_timer.h"
#include "ek/core/ek_export.h"
#include "ek/core/ek_arch.h"
#include "ek/core/ek_log.h"

EK_LOG_FILE_TAG(__FILE_NAME__);

void bsp_schedule_timer_init(void)
{
    rcu_periph_clock_enable(BSP_SCHEDULE_TIMER_RCU);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    timer_parameter_struct cfg = {
        .counterdirection = TIMER_COUNTER_UP,
        .alignedmode = TIMER_COUNTER_EDGE,
        .prescaler = EK_FREQ_M(120) / 10000 - 1,
        .period = UINT16_MAX - 1,
    };

    timer_init(BSP_SCHEDULE_TIMER, &cfg);
    timer_interrupt_flag_clear(BSP_SCHEDULE_TIMER, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(BSP_SCHEDULE_TIMER, TIMER_INT_UP);
    nvic_irq_enable(BSP_SCHEDULE_TIMER_IRQ, 0, 1);
    bsp_schedule_timer_set_once(100);
}

EK_EXPORT_BOARD(bsp_schedule_timer_init, EXPORT_ORDER_BOARD_TIMER);

void bsp_schedule_timer_set_once(uint16_t tick)
{
    timer_disable(BSP_SCHEDULE_TIMER);
    timer_single_pulse_mode_config(BSP_SCHEDULE_TIMER, TIMER_SP_MODE_SINGLE);
    timer_autoreload_value_config(BSP_SCHEDULE_TIMER, tick * 10 - 1);
    timer_counter_value_config(BSP_SCHEDULE_TIMER, 0);
    timer_interrupt_flag_clear(BSP_SCHEDULE_TIMER, TIMER_INT_FLAG_UP);
    timer_enable(BSP_SCHEDULE_TIMER);
}

void bsp_schedule_timer_irq_handler(void)
{
    if (timer_interrupt_flag_get(BSP_SCHEDULE_TIMER, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(BSP_SCHEDULE_TIMER, TIMER_INT_FLAG_UP);
        EK_LOG_INFO("timer irq evoke");
    }
}

EK_DEFINE_ARCH_IRQ(BSP_SCHEDULE_TIMER_IRQ, bsp_schedule_timer_irq_handler);
