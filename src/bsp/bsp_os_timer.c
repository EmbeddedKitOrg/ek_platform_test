#include "bsp_os_timer.h"

#if EKCFG_RTOS == 1

#    include "ek/core/arch.h"
#    include "ek/core/export.h"

#    define OS_TIMER           TIMER4
#    define OS_TIMER_RCU       RCU_TIMER4
#    define OS_TIMER_IRQ       TIMER4_IRQn
#    define OS_TIMER_CH        TIMER_CH_0
#    define OS_TIMER_TICK_HZ   (1000000U)
#    define OS_DEFAULT_CORE_HZ (240000000U)
#    define OS_TIMER_MAX_DELTA (0x7FFFFFFFU)

static uint64_t s_high_us;

static uint32_t _core_hz(void)
{
    return SystemCoreClock ? SystemCoreClock : OS_DEFAULT_CORE_HZ;
}

// TIMER 时钟 = APB1 x 2 = SystemCoreClock / 2，分频到 1 MHz
static uint16_t _timer_prescaler(void)
{
    uint32_t div = (_core_hz() / 2U) / OS_TIMER_TICK_HZ;
    if (div == 0U) div = 1U;
    return (uint16_t)(div - 1U);
}

// 关中断临界区内读取 64 位单调微秒时间，并处理尚未被 UP ISR 消费的溢出回绕
static uint64_t _os_now_us(void)
{
    uint32_t flags = ek_arch_irq_lock();
    uint32_t cnt = timer_counter_read(OS_TIMER);
    if (timer_flag_get(OS_TIMER, TIMER_FLAG_UP) != RESET && cnt < 0x80000000U)
    {
        timer_flag_clear(OS_TIMER, TIMER_FLAG_UP);
        s_high_us += (1ULL << 32);
    }
    uint64_t us = s_high_us + cnt;
    ek_arch_irq_unlock(flags);
    return us;
}

// 使用 CH0 比较中断设置单次 Tickless 闹钟，delta_us < 0 仅关闭比较中断
static void _os_set_timeout_us(int64_t delta_us)
{
    uint32_t flags = ek_arch_irq_lock();
    timer_interrupt_disable(OS_TIMER, TIMER_INT_CH0);
    timer_flag_clear(OS_TIMER, TIMER_FLAG_CH0);

    if (delta_us < 0)
    {
        ek_arch_irq_unlock(flags);
        return;
    }

    if (delta_us == 0)
    {
        timer_interrupt_enable(OS_TIMER, TIMER_INT_CH0);
        timer_event_software_generate(OS_TIMER, TIMER_EVENT_SRC_CH0G);
        ek_arch_irq_unlock(flags);
        return;
    }

    uint32_t step = (delta_us > (int64_t)OS_TIMER_MAX_DELTA) ? OS_TIMER_MAX_DELTA : (uint32_t)delta_us;
    uint32_t target = timer_counter_read(OS_TIMER) + step;

    timer_channel_output_pulse_value_config(OS_TIMER, OS_TIMER_CH, target);
    timer_flag_clear(OS_TIMER, TIMER_FLAG_CH0);
    timer_interrupt_enable(OS_TIMER, TIMER_INT_CH0);

    // 防止极短超时在配置期间已被计数器越过而漏掉比较匹配
    if ((int32_t)(target - timer_counter_read(OS_TIMER)) <= 0)
    {
        timer_event_software_generate(OS_TIMER, TIMER_EVENT_SRC_CH0G);
    }

    ek_arch_irq_unlock(flags);
}

static void _os_timer_irq_handler(void)
{
    bool alarm = false;
    uint32_t flags = ek_arch_irq_lock();

    if (timer_interrupt_flag_get(OS_TIMER, TIMER_INT_FLAG_UP) != RESET)
    {
        timer_interrupt_flag_clear(OS_TIMER, TIMER_INT_FLAG_UP);
        s_high_us += (1ULL << 32);
    }

    if (timer_interrupt_flag_get(OS_TIMER, TIMER_INT_FLAG_CH0) != RESET)
    {
        timer_interrupt_flag_clear(OS_TIMER, TIMER_INT_FLAG_CH0);
        timer_interrupt_disable(OS_TIMER, TIMER_INT_CH0);
        alarm = true;
    }

    ek_arch_irq_unlock(flags);

    if (alarm)
    {
        ek_os_kernel_announce_isr();
    }
}

EK_DEFINE_ARCH_IRQ(OS_TIMER_IRQ, _os_timer_irq_handler);

static void _os_timer_init(void)
{
    rcu_periph_clock_enable(OS_TIMER_RCU);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);
    timer_deinit(OS_TIMER);

    timer_parameter_struct cfg;
    timer_struct_para_init(&cfg);
    cfg.prescaler = _timer_prescaler();
    cfg.alignedmode = TIMER_COUNTER_EDGE;
    cfg.counterdirection = TIMER_COUNTER_UP;
    cfg.period = UINT32_MAX;
    cfg.clockdivision = TIMER_CKDIV_DIV1;
    timer_init(OS_TIMER, &cfg);

    timer_channel_output_mode_config(OS_TIMER, OS_TIMER_CH, TIMER_OC_MODE_TIMING);
    timer_channel_output_shadow_config(OS_TIMER, OS_TIMER_CH, TIMER_OC_SHADOW_DISABLE);
    timer_update_source_config(OS_TIMER, TIMER_UPDATE_SRC_REGULAR);

    s_high_us = 0ULL;
    timer_counter_value_config(OS_TIMER, 0U);
    timer_flag_clear(OS_TIMER, TIMER_FLAG_UP);
    timer_flag_clear(OS_TIMER, TIMER_FLAG_CH0);
    timer_interrupt_disable(OS_TIMER, TIMER_INT_CH0);
    timer_interrupt_enable(OS_TIMER, TIMER_INT_UP);
    nvic_irq_enable(OS_TIMER_IRQ, 0, 0);
    timer_enable(OS_TIMER);
}

EK_EXPORT_BOARD(_os_timer_init, EXPORT_ORDER_BOARD_TIMER);

const ek_os_timer_ops_t g_bsp_os_timer_ops = {
    .now = _os_now_us,
    .set_timeout = _os_set_timeout_us,
};

#endif /* EKCFG_RTOS == 1 */
