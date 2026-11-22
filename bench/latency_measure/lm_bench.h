/**
 * @file lm_bench.h
 * @brief Zephyr latency_measure 风格内核微基准共享定义
 */

#ifndef LM_BENCH_H
#define LM_BENCH_H

#include <stdbool.h>
#include <stdint.h>

#include "ek/core/arch.h"
#include "ek_soc.h"

#ifndef LM_BENCH_ITERS
#    define LM_BENCH_ITERS (1000U)
#endif

#define LM_WARMUP_ITERS (16U)

typedef struct
{
    uint64_t sum;
    uint32_t min;
    uint32_t max;
    uint32_t count;
} lm_stats_t;

extern uint32_t g_lm_ts_overhead;
extern uint32_t g_lm_core_mhz;

static inline uint32_t lm_now_cycles(void)
{
    __asm__ volatile("" ::: "memory");
    uint32_t c = DWT->CYCCNT;
    __asm__ volatile("" ::: "memory");
    return c;
}

void lm_stats_init(lm_stats_t *st);
void lm_stats_record(lm_stats_t *st, uint32_t raw_cycles);
void lm_stats_report(const char *name, const lm_stats_t *st);

void lm_sw_irq_set_handler(void (*fn)(void));
void lm_sw_irq_trigger(void);
void lm_run_all_cases(uint32_t iters);

#endif /* LM_BENCH_H */
