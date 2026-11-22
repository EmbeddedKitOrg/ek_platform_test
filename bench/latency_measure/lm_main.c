/**
 * @file lm_main.c
 * @brief Zephyr latency_measure 风格微基准入口、周期校准与统计输出
 */

#include "lm_bench.h"

#include "bsp_os_timer.h"
#include "main.h"

#include "ek/core/io.h"
#include "ek/os/kernel.h"
#include "ek/os/thread.h"

/* 使用未占用的 CAN1_TX_IRQn（IRQ 63）作为软件触发中断源，避免与板级 TIMER6 冲突 */
#define LM_SW_IRQn CAN1_TX_IRQn

uint32_t g_lm_ts_overhead;
uint32_t g_lm_core_mhz = 240U;

static void (*volatile s_lm_irq_cb)(void);

static void _lm_irq_handler(void)
{
    NVIC_ClearPendingIRQ(LM_SW_IRQn);
    void (*fn)(void) = s_lm_irq_cb;
    if (fn) fn();
}

EK_DEFINE_ARCH_IRQ(LM_SW_IRQn, _lm_irq_handler);

void lm_sw_irq_set_handler(void (*fn)(void))
{
    s_lm_irq_cb = fn;
}

void lm_sw_irq_trigger(void)
{
    NVIC_SetPendingIRQ(LM_SW_IRQn);
    ek_arch_dsb();
    ek_arch_isb();
}

void lm_stats_init(lm_stats_t *st)
{
    st->sum = 0ULL;
    st->min = UINT32_MAX;
    st->max = 0U;
    st->count = 0U;
}

void lm_stats_record(lm_stats_t *st, uint32_t raw_cycles)
{
    uint32_t delta = (raw_cycles > g_lm_ts_overhead) ? (raw_cycles - g_lm_ts_overhead) : 0U;
    st->sum += delta;
    if (delta < st->min) st->min = delta;
    if (delta > st->max) st->max = delta;
    st->count++;
}

void lm_stats_report(const char *name, const lm_stats_t *st)
{
    if (!st || st->count == 0U)
    {
        ek_printf("  %-34s | %8s | %8s | %8s | %8s | %8s\r\n", name, "N/A", "N/A", "N/A", "N/A", "N/A");
        return;
    }

    uint32_t avg = (uint32_t)(st->sum / st->count);
    uint32_t mhz = g_lm_core_mhz ? g_lm_core_mhz : 1U;
    uint32_t avg_ns = (uint32_t)(((uint64_t)avg * 1000ULL + (mhz / 2U)) / mhz);
    uint32_t max_ns = (uint32_t)(((uint64_t)st->max * 1000ULL + (mhz / 2U)) / mhz);

    ek_printf("  %-34s | %8lu | %8lu | %8lu | %8lu | %8lu\r\n",
              name,
              (unsigned long)avg,
              (unsigned long)st->min,
              (unsigned long)st->max,
              (unsigned long)avg_ns,
              (unsigned long)max_ns);
}

static uint32_t _calibrate_ts_overhead(void)
{
    uint32_t min_ov = UINT32_MAX;
    for (uint32_t i = 0U; i < 1000U; i++)
    {
        uint32_t t0 = lm_now_cycles();
        uint32_t t1 = lm_now_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min_ov) min_ov = diff;
    }
    return min_ov;
}

static void *_lm_runner_thread(void *arg)
{
    (void)arg;

    uint32_t hz = SystemCoreClock ? SystemCoreClock : 240000000U;
    g_lm_core_mhz = hz / 1000000U;
    if (g_lm_core_mhz == 0U) g_lm_core_mhz = 1U;

    g_lm_ts_overhead = _calibrate_ts_overhead();

    ek_printf("\r\n==== ek_platform latency_measure (%s) ====\r\n", ek_build_mode_str());
    ek_printf("Clock: %lu MHz | Iterations: %lu (+%u warmup) | ts_overhead: %lu cyc\r\n",
              (unsigned long)g_lm_core_mhz,
              (unsigned long)LM_BENCH_ITERS,
              (unsigned int)LM_WARMUP_ITERS,
              (unsigned long)g_lm_ts_overhead);
    ek_printf("  %-34s | %8s | %8s | %8s | %8s | %8s\r\n",
              "Benchmark", "avg(cyc)", "min(cyc)", "max(cyc)", "avg(ns)", "max(ns)");
    ek_printf("  -----------------------------------+----------+----------+----------+----------+----------\r\n");

    lm_run_all_cases(LM_BENCH_ITERS);

    ek_printf("==== latency_measure finished ====\r\n");

    for (;;)
    {
        ek_os_thread_sleep(EK_OS_SEC(60));
    }

    return NULL;
}

void lm_main(void)
{
    nvic_irq_enable(LM_SW_IRQn, 0, 2);
    (void)ek_os_thread_create("lm_runner", _lm_runner_thread, NULL, 10U, 2048U);
    ek_os_kernel_start(&g_bsp_os_timer_ops);
}
