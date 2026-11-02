#include "main.h"
#include "test.h"

#include "bsp_uart.h"
#include "bsp_led.h"
#include "bsp_timer.h"

#include "ek/core/ek_export.h"
#include "ek/core/ek_log.h"
#include "ek/sched/ek_picothread.h"

static void _restore_clock_to_240m(void);

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_INFO);

void system_init(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE0_SUB4);
    systick_init();
}

EK_EXPORT_BOOT(system_init, EK_EXPORT_ORDER_BOOT_SYSTEM);

int main(void)
{
    // evoke_test();
    // picothread_test_init();
    // ringbuf_test();
    // stack_test();
    // snapshot_test();
    // str_test();
    // str_view_test();
    // vec_test();
    vfs_test();

    while (1)
    {
        uint32_t now_tick = get_tick();
        uint32_t next_tick = ek_pt_schedule(now_tick);
        uint32_t diff_tick = next_tick - now_tick;

        if (!next_tick)
        {
            // 没有任何任务了
            // 可以深度睡眠
            SysTick->CTRL = 0;
            EK_LOG_WARN("goto deep sleep");
            // pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);
            // _restore_clock_to_240m();
            pmu_to_sleepmode(WFI_CMD);
            SysTick->CTRL = 1;
            EK_LOG_INFO("wake up from deepsleep");
        }

        if (!diff_tick)
        {
            // 还有任务没触发
            // 需要继续运行 ek_pt_schedule()
            continue;
        }
        else if (diff_tick)
        {
            // 设定时器，睡到下次唤醒
            bsp_schedule_timer_set_once(diff_tick);
            tick_inc(diff_tick);

            SysTick->CTRL = 0;
            pmu_to_sleepmode(WFI_CMD);
            SysTick->CTRL = 1;
        }
    }
}

int ek_port_io_fputc(int ch)
{
    bsp_uart_send_data(ch);
    return ch;
}

uint32_t ek_port_log_get_tick(void)
{
    return get_tick();
}

static void _restore_clock_to_240m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do
    {
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    } while ((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if (0U == (RCU_CTL & RCU_CTL_HXTALSTB))
    {
        while (1)
        {
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 25, PLL_N = 480, PLL_P = 2, PLL_Q = 10 */
    RCU_PLL = (25U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) | (RCU_PLLSRC_HXTAL) | (10U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while (0U == (RCU_CTL & RCU_CTL_PLLSTB))
    {
    }

    /* Enable the high-drive to extend the clock frequency to 240 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while (0U == (PMU_CS & PMU_CS_HDRF))
    {
    }

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while (0U == (PMU_CS & PMU_CS_HDSRF))
    {
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while (0U == (RCU_CFG0 & RCU_SCSS_PLLP))
    {
    }
}
