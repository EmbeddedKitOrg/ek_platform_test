#include "main.h"
#include "test.h"

#include "bsp_uart.h"
#include "bsp_led.h"
#include "bsp_timer.h"
#include "bsp_os_timer.h"

#include "ek/core/export.h"
#include "ek/core/log.h"
#include "ek/os/kernel.h"
#include "ek/sched/picothread.h"
#include "ek/vfs/vfs_core.h"

static void _restore_clock_to_240m(void);

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_NONE);

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
    // vfs_test();
    // shell_test();
    EK_LOG_INFO("hello (%s)", ek_build_mode_str());

#if EKCFG_RTOS == 1
    EK_LOG_INFO("main: entering RTOS");

    {
        extern uint32_t _sram_vtor_start[];
        extern void ek_arch_start_first_thread(void);
        extern void ek_arch_switch_thread(void);
        uint32_t *vt = (uint32_t *)(uintptr_t)SCB->VTOR;
        EK_LOG_INFO("VTOR=0x%08lx sram=0x%08lx heap=%lu",
                    (unsigned long)SCB->VTOR,
                    (unsigned long)(uintptr_t)_sram_vtor_start,
                    (unsigned long)ek_heap_total_size());
        EK_LOG_INFO("svc   vec=0x%08lx want=0x%08lx",
                    (unsigned long)vt[11],
                    (unsigned long)(uintptr_t)ek_arch_start_first_thread);
        EK_LOG_INFO(
            "pendsv vec=0x%08lx want=0x%08lx", (unsigned long)vt[14], (unsigned long)(uintptr_t)ek_arch_switch_thread);
    }

#    if defined(EK_RUN_THREAD_METRIC)
    extern void tm_main(void);
    EK_LOG_INFO("main: starting Thread-Metric benchmark");
    tm_main();
#    elif defined(EK_RUN_LATENCY_MEASURE)
    extern void lm_main(void);
    EK_LOG_INFO("main: starting latency_measure benchmark");
    lm_main();
#    else
    os_test(); /* 创建自检线程，内核启动后才开始运行 */
    EK_LOG_INFO("main: os_test done, starting kernel");
    ek_os_kernel_start(&g_bsp_os_timer_ops);
#    endif
    EK_LOG_ERROR("main: kernel start returned unexpectedly");
#endif

    while (1)
    {
    }
}

int ek_port_io_fputc(int ch)
{
    bsp_uart_send_data(ch);
    return ch;
}

uint32_t ek_port_log_get_tick(void)
{
#if EKCFG_RTOS == 1
    return (uint32_t)(ek_os_kernel_time() / 1000UL);
#endif
    return 0;
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
