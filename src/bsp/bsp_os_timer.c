/**
 * @file bsp_os_timer.c
 * @brief RTOS 时间源与单次闹钟
 *
 * 自由时间源按下面顺序选择，运行期自动探测：
 *   1. DWT->CYCCNT（真机，CPU 周期计数，软件扩展为 64 位后换算成微秒）；
 *   2. 探测失败（如 Renode 不模拟 CYCCNT）时回退到 TIMER4 自由运行，
 *      1 MHz 计数、32 位量程，计数即微秒。
 *
 * 单次闹钟固定使用 TIMER5：单脉冲模式、1 MHz（1 tick = 1 us），
 * 到期中断中调用 ek_os_kernel_announce_isr()。
 */

#include "bsp_os_timer.h"

#if EKCFG_RTOS == 1

#    include "ek/core/arch.h"
#    include "ek/core/export.h"

/* 单次闹钟 */
#    define OS_ALARM_TIMER TIMER5
#    define OS_ALARM_RCU   RCU_TIMER5
#    define OS_ALARM_IRQ   TIMER5_DAC_IRQn

/* 计数频率：1 MHz，即 1 tick = 1 us */
#    define OS_TIMER_TICK_HZ (1000000U)

/* 内核默认时钟频率回退值（SystemCoreClock 为 0 时使用） */
#    define OS_DEFAULT_CORE_HZ (240000000U)

/* 自动探测结果：true 用 DWT + TIMER5，false 回退 SysTick 1ms 节拍 */
static bool s_use_dwt;
static volatile uint32_t s_systick_ms;

static uint32_t s_last_cnt;
static uint64_t s_cnt_base;

static uint32_t _core_hz(void)
{
    return SystemCoreClock ? SystemCoreClock : OS_DEFAULT_CORE_HZ;
}

/* 每微秒的 CPU 周期数；至少为 1，避免除零 */
static uint32_t _cycles_per_us(void)
{
    uint32_t div = _core_hz() / 1000000U;
    return div ? div : 1U;
}

/* TIMER 时钟 = APB1 x 2 = SystemCoreClock / 2，分频到 1 MHz */
static uint16_t _timer_prescaler(void)
{
    uint32_t div = (_core_hz() / 2U) / OS_TIMER_TICK_HZ;
    if (div == 0U) div = 1U;
    return (uint16_t)(div - 1U);
}

/* 探测 DWT->CYCCNT 是否会递增；Renode 等环境读回恒 0 时返回 false */
static bool _dwt_probe(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (volatile uint32_t i = 0; i < 1000U; i++)
    {
    }

    return DWT->CYCCNT != 0U;
}

static void _systick_handler(void)
{
    s_systick_ms++;
    ek_os_kernel_announce_isr();
}

EK_DEFINE_ARCH_IRQ(SysTick_IRQn, _systick_handler);

static void _systick_init(void)
{
    s_systick_ms = 0U;
    (void)SysTick_Config(_core_hz() / 1000U);
}

static uint64_t _os_now_us(void)
{
    if (!s_use_dwt)
    {
        uint32_t flags = ek_os_kernel_spin_lock();
        uint64_t us = (uint64_t)s_systick_ms * 1000ULL;
        ek_os_kernel_spin_unlock(flags);
        return us;
    }

    uint32_t flags = ek_os_kernel_spin_lock();

    uint32_t cnt = DWT->CYCCNT;
    if (cnt < s_last_cnt) s_cnt_base += (1ULL << 32); /* 32 位 CYCCNT 回绕 */
    s_last_cnt = cnt;
    uint64_t cycles = s_cnt_base + cnt;

    ek_os_kernel_spin_unlock(flags);

    return cycles / _cycles_per_us();
}

static void _os_set_timeout_us(int64_t delta_us)
{
    /* SysTick 模式：周期节拍已经在跑，无需单次闹钟 */
    if (!s_use_dwt) return;

    timer_disable(OS_ALARM_TIMER);

    /* 内核用负数表示「当前没有定时需求」，停表即可 */
    if (delta_us < 0) return;

    /* 超过 16 位量程时先按最大值装一枪，到期提前唤醒后内核会自动续期 */
    uint32_t ticks = (delta_us > (int64_t)UINT16_MAX) ? UINT16_MAX : (uint32_t)delta_us;
    if (ticks == 0U) ticks = 1U;

    timer_single_pulse_mode_config(OS_ALARM_TIMER, TIMER_SP_MODE_SINGLE);
    timer_autoreload_value_config(OS_ALARM_TIMER, ticks - 1U);
    timer_counter_value_config(OS_ALARM_TIMER, 0U);
    timer_interrupt_flag_clear(OS_ALARM_TIMER, TIMER_INT_FLAG_UP);
    timer_enable(OS_ALARM_TIMER);
}

static void _os_timer_irq_handler(void)
{
    if (timer_interrupt_flag_get(OS_ALARM_TIMER, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(OS_ALARM_TIMER, TIMER_INT_FLAG_UP);
        ek_os_kernel_announce_isr();
    }
}

EK_DEFINE_ARCH_IRQ(OS_ALARM_IRQ, _os_timer_irq_handler);

static void _os_timer_init(void)
{
    /* 自由时间源：优先 DWT，不可用则回退 TIMER4 */
    s_use_dwt = _dwt_probe();
    if (s_use_dwt)
    {
        DWT->CYCCNT = 0U;
        s_last_cnt = DWT->CYCCNT;
        s_cnt_base = 0U;
    }
    else
    {
        /* Renode 等不驱动 CYCCNT 的环境：退化为 1 ms 周期节拍 */
        _systick_init();
    }

    /* 单次闹钟：TIMER5 */
    rcu_periph_clock_enable(OS_ALARM_RCU);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    timer_parameter_struct cfg = {
        .counterdirection = TIMER_COUNTER_UP,
        .alignedmode = TIMER_COUNTER_EDGE,
        .prescaler = _timer_prescaler(),
        .period = UINT16_MAX - 1U,
    };

    timer_init(OS_ALARM_TIMER, &cfg);
    timer_interrupt_flag_clear(OS_ALARM_TIMER, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(OS_ALARM_TIMER, TIMER_INT_UP);
    nvic_irq_enable(OS_ALARM_IRQ, 0, 0);
    timer_disable(OS_ALARM_TIMER);
}

EK_EXPORT_BOARD(_os_timer_init, EXPORT_ORDER_BOARD_TIMER);

const ek_os_timer_ops_t g_bsp_os_timer_ops = {
    .now = _os_now_us,
    .set_timeout = _os_set_timeout_us,
};

#endif /* EKCFG_RTOS == 1 */
