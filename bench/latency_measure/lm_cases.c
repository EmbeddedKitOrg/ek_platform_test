/**
 * @file lm_cases.c
 * @brief Zephyr latency_measure 风格内核微基准用例集合
 */

#include "lm_bench.h"

#include "ek/mem/heap.h"
#include "ek/os/barrier.h"
#include "ek/os/condvar.h"
#include "ek/os/kernel.h"
#include "ek/os/kfifo.h"
#include "ek/os/msgq.h"
#include "ek/os/mutex.h"
#include "ek/os/sem.h"
#include "ek/os/thread.h"

#define LM_PRIO_HIGH    (5U)
#define LM_PRIO_YIELD   (8U)
#define LM_PRIO_LOW     (12U)
#define LM_PRIO_BG      (15U)
#define LM_WORKER_STACK (1024U)

/* ---------- 1. thread: yield ---------- */

static ek_os_sem_t *s_y_start_sem;
static volatile uint32_t s_y_t0;
static volatile uint32_t s_y_idx;
static uint32_t s_y_total;
static lm_stats_t s_y_stats;

static void *_lm_yield_a(void *arg)
{
    (void)arg;
    (void)ek_os_sem_take(s_y_start_sem, EK_OS_FOREVER);

    for (;;)
    {
        uint32_t t1 = lm_now_cycles();
        uint32_t idx = s_y_idx++;
        if (idx >= LM_WARMUP_ITERS && idx < s_y_total)
        {
            lm_stats_record(&s_y_stats, t1 - s_y_t0);
        }
        if (idx + 1U >= s_y_total)
        {
            ek_os_thread_yield();
            break;
        }
        s_y_t0 = lm_now_cycles();
        ek_os_thread_yield();
    }
    return NULL;
}

static void *_lm_yield_b(void *arg)
{
    (void)arg;
    ek_os_sem_give(s_y_start_sem);

    for (;;)
    {
        uint32_t idx = s_y_idx;
        if (idx >= s_y_total) break;

        s_y_t0 = lm_now_cycles();
        ek_os_thread_yield();
        uint32_t t1 = lm_now_cycles();

        uint32_t done_idx = s_y_idx++;
        if (done_idx >= LM_WARMUP_ITERS && done_idx < s_y_total)
        {
            lm_stats_record(&s_y_stats, t1 - s_y_t0);
        }
        if (done_idx + 1U >= s_y_total) break;
    }
    return NULL;
}

static void _bench_thread_yield(uint32_t iters)
{
    s_y_start_sem = ek_os_sem_create(0U);
    lm_stats_init(&s_y_stats);
    s_y_idx = 0U;
    s_y_total = LM_WARMUP_ITERS + iters;

    int tid_a = ek_os_thread_create("lm_ya", _lm_yield_a, NULL, LM_PRIO_YIELD, LM_WORKER_STACK);
    int tid_b = ek_os_thread_create("lm_yb", _lm_yield_b, NULL, LM_PRIO_YIELD, LM_WORKER_STACK);

    ek_os_thread_join(tid_a, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_b, EK_OS_FOREVER, NULL);
    ek_os_sem_destroy(s_y_start_sem);
    s_y_start_sem = NULL;

    lm_stats_report("thread.yield (2 threads)", &s_y_stats);
}

/* ---------- 2. thread: suspend / resume / preempt / create+join ---------- */

static volatile bool s_bg_stop;

static void *_lm_bg_thread(void *arg)
{
    (void)arg;
    while (!s_bg_stop)
    {
        __NOP();
    }
    return NULL;
}

static void _bench_thread_suspend_resume(uint32_t iters)
{
    lm_stats_t st_susp;
    lm_stats_t st_res;
    lm_stats_init(&st_susp);
    lm_stats_init(&st_res);

    s_bg_stop = false;
    int tid = ek_os_thread_create("lm_bg", _lm_bg_thread, NULL, LM_PRIO_BG, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_thread_suspend(tid);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_os_thread_resume(tid);
        uint32_t t3 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_susp, t1 - t0);
            lm_stats_record(&st_res, t3 - t2);
        }
    }

    s_bg_stop = true;
    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);

    lm_stats_report("thread.suspend (other)", &st_susp);
    lm_stats_report("thread.resume (other)", &st_res);
}

static volatile uint32_t s_pre_t0;
static volatile uint32_t s_pre_iters;
static lm_stats_t s_pre_stats;

static void *_lm_preempt_target(void *arg)
{
    (void)arg;
    int self = ek_os_kernel_current_tid();
    uint32_t total = s_pre_iters;

    for (uint32_t i = 0U; i < total; i++)
    {
        ek_os_thread_suspend(self);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_pre_stats, t1 - s_pre_t0);
        }
    }
    return NULL;
}

static void _bench_thread_resume_preempt(uint32_t iters)
{
    lm_stats_init(&s_pre_stats);
    s_pre_iters = LM_WARMUP_ITERS + iters;

    int tid = ek_os_thread_create("lm_pre", _lm_preempt_target, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_pre_iters; i++)
    {
        s_pre_t0 = lm_now_cycles();
        ek_os_thread_resume(tid);
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    lm_stats_report("thread.resume -> preempt", &s_pre_stats);
}

static uint8_t s_static_thrd_mem[LM_WORKER_STACK + EK_OS_THREAD_TCB_SIZE] __EK_ALIGNED(8);

static void *_lm_noop_thread(void *arg)
{
    (void)arg;
    return NULL;
}

static void _bench_thread_create_join(uint32_t iters)
{
    uint32_t n = EK_MIN(iters, 200U);
    lm_stats_t st_static;
    lm_stats_t st_dyn;
    lm_stats_init(&st_static);
    lm_stats_init(&st_dyn);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + n; i++)
    {
        uint32_t t0 = lm_now_cycles();
        int tid = ek_os_thread_create_static(
            "lm_st", _lm_noop_thread, NULL, LM_PRIO_HIGH, s_static_thrd_mem, sizeof(s_static_thrd_mem));
        ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS) lm_stats_record(&st_static, t1 - t0);
    }

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + n; i++)
    {
        uint32_t t0 = lm_now_cycles();
        int tid = ek_os_thread_create("lm_dy", _lm_noop_thread, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);
        ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS) lm_stats_record(&st_dyn, t1 - t0);
    }

    lm_stats_report("thread.create+join (static)", &st_static);
    lm_stats_report("thread.create+join (dynamic)", &st_dyn);
}

/* ---------- 3. isr ---------- */

static volatile uint32_t s_isr_t_enter;

static void _lm_isr_probe_cb(void)
{
    s_isr_t_enter = lm_now_cycles();
}

static void _bench_isr_entry_exit(uint32_t iters)
{
    lm_stats_t st_in;
    lm_stats_t st_out;
    lm_stats_init(&st_in);
    lm_stats_init(&st_out);

    lm_sw_irq_set_handler(_lm_isr_probe_cb);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        lm_sw_irq_trigger();
        uint32_t t1 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_in, s_isr_t_enter - t0);
            lm_stats_record(&st_out, t1 - s_isr_t_enter);
        }
    }

    lm_sw_irq_set_handler(NULL);
    lm_stats_report("isr.entry", &st_in);
    lm_stats_report("isr.to_interrupted_thread", &st_out);
}

static ek_os_sem_t *s_isr_sem;
static volatile uint32_t s_isr_sem_t0;
static volatile uint32_t s_isr_sem_iters;
static lm_stats_t s_isr_sem_stats;

static void _lm_isr_sem_cb(void)
{
    s_isr_sem_t0 = lm_now_cycles();
    (void)ek_os_sem_give(s_isr_sem);
}

static void *_lm_isr_sem_waiter(void *arg)
{
    (void)arg;
    uint32_t total = s_isr_sem_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_sem_take(s_isr_sem, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_isr_sem_stats, t1 - s_isr_sem_t0);
        }
    }
    return NULL;
}

static void _bench_isr_to_preempt(uint32_t iters)
{
    s_isr_sem = ek_os_sem_create(0U);
    lm_stats_init(&s_isr_sem_stats);
    s_isr_sem_iters = LM_WARMUP_ITERS + iters;

    lm_sw_irq_set_handler(_lm_isr_sem_cb);
    int tid = ek_os_thread_create("lm_isem", _lm_isr_sem_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_isr_sem_iters; i++)
    {
        lm_sw_irq_trigger();
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    lm_sw_irq_set_handler(NULL);
    ek_os_sem_destroy(s_isr_sem);
    s_isr_sem = NULL;

    lm_stats_report("isr.to_preempt_thread (sem)", &s_isr_sem_stats);
}

/* ---------- 4. sem ---------- */

static ek_os_sem_t *s_sem;
static volatile uint32_t s_sem_t0;
static volatile uint32_t s_sem_iters;
static lm_stats_t s_sem_sw_stats;

static void *_lm_sem_waiter(void *arg)
{
    (void)arg;
    uint32_t total = s_sem_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_sem_take(s_sem, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_sem_sw_stats, t1 - s_sem_t0);
        }
    }
    return NULL;
}

static void _bench_sem(uint32_t iters)
{
    lm_stats_t st_give;
    lm_stats_t st_take;
    lm_stats_init(&st_give);
    lm_stats_init(&st_take);
    lm_stats_init(&s_sem_sw_stats);

    s_sem = ek_os_sem_create(0U);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_sem_give(s_sem);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_os_sem_take(s_sem, 0);
        uint32_t t3 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_give, t1 - t0);
            lm_stats_record(&st_take, t3 - t2);
        }
    }

    s_sem_iters = LM_WARMUP_ITERS + iters;
    int tid = ek_os_thread_create("lm_sem", _lm_sem_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_sem_iters; i++)
    {
        s_sem_t0 = lm_now_cycles();
        ek_os_sem_give(s_sem);
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    ek_os_sem_destroy(s_sem);
    s_sem = NULL;

    lm_stats_report("sem.give (no waiter)", &st_give);
    lm_stats_report("sem.take (no block)", &st_take);
    lm_stats_report("sem.give -> wakeup switch", &s_sem_sw_stats);
}

/* ---------- 5. mutex (含 PI 移交) ---------- */

static ek_os_mutex_t *s_mtx;
static ek_os_sem_t *s_pi_gate;
static ek_os_sem_t *s_pi_done;
static volatile uint32_t s_pi_t0;
static volatile uint32_t s_pi_iters;
static lm_stats_t s_pi_stats;

static void *_lm_pi_high(void *arg)
{
    (void)arg;
    uint32_t total = s_pi_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_sem_take(s_pi_gate, EK_OS_FOREVER);
        (void)ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_pi_stats, t1 - s_pi_t0);
        }
        (void)ek_os_mutex_unlock(s_mtx);
    }
    return NULL;
}

static void *_lm_pi_low(void *arg)
{
    (void)arg;
    uint32_t total = s_pi_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
        (void)ek_os_sem_give(s_pi_gate);
        s_pi_t0 = lm_now_cycles();
        (void)ek_os_mutex_unlock(s_mtx);
    }
    (void)ek_os_sem_give(s_pi_done);
    return NULL;
}

static void _bench_mutex(uint32_t iters)
{
    lm_stats_t st_lock;
    lm_stats_t st_unlock;
    lm_stats_t st_relock;
    lm_stats_init(&st_lock);
    lm_stats_init(&st_unlock);
    lm_stats_init(&st_relock);
    lm_stats_init(&s_pi_stats);

    s_mtx = ek_os_mutex_create();

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_mutex_lock(s_mtx, 0);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_os_mutex_lock(s_mtx, 0);
        ek_os_mutex_unlock(s_mtx);
        uint32_t t3 = lm_now_cycles();

        uint32_t t4 = lm_now_cycles();
        ek_os_mutex_unlock(s_mtx);
        uint32_t t5 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_lock, t1 - t0);
            lm_stats_record(&st_relock, t3 - t2);
            lm_stats_record(&st_unlock, t5 - t4);
        }
    }

    s_pi_gate = ek_os_sem_create(0U);
    s_pi_done = ek_os_sem_create(0U);
    s_pi_iters = LM_WARMUP_ITERS + iters;

    int tid_h = ek_os_thread_create("lm_pih", _lm_pi_high, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);
    int tid_l = ek_os_thread_create("lm_pil", _lm_pi_low, NULL, LM_PRIO_LOW, LM_WORKER_STACK);

    (void)ek_os_sem_take(s_pi_done, EK_OS_FOREVER);
    ek_os_thread_join(tid_h, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_l, EK_OS_FOREVER, NULL);

    ek_os_sem_destroy(s_pi_gate);
    ek_os_sem_destroy(s_pi_done);
    ek_os_mutex_destroy(s_mtx);
    s_pi_gate = NULL;
    s_pi_done = NULL;
    s_mtx = NULL;

    lm_stats_report("mutex.lock (uncontended)", &st_lock);
    lm_stats_report("mutex.unlock (uncontended)", &st_unlock);
    lm_stats_report("mutex.relock+unlock (recursive)", &st_relock);
    lm_stats_report("mutex.unlock -> handoff (PI)", &s_pi_stats);
}

/* ---------- 6. condvar ---------- */

static ek_os_condvar_t *s_cv;
static ek_os_mutex_t *s_cv_mtx;
static ek_os_sem_t *s_cv_done;
static volatile bool s_cv_waiting;
static volatile uint32_t s_cv_t0;
static volatile uint32_t s_cv_iters;
static lm_stats_t s_cv_sig_stats;
static lm_stats_t s_cv_sw_stats;

static void *_lm_cv_waiter(void *arg)
{
    (void)arg;
    uint32_t total = s_cv_iters;
    (void)ek_os_mutex_lock(s_cv_mtx, EK_OS_FOREVER);
    for (uint32_t i = 0U; i < total; i++)
    {
        s_cv_waiting = true;
        (void)ek_os_condvar_wait(s_cv, s_cv_mtx, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_cv_sw_stats, t1 - s_cv_t0);
        }
    }
    (void)ek_os_mutex_unlock(s_cv_mtx);
    return NULL;
}

static void *_lm_cv_signaler(void *arg)
{
    (void)arg;
    uint32_t total = s_cv_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_mutex_lock(s_cv_mtx, EK_OS_FOREVER);
        if (s_cv_waiting)
        {
            s_cv_waiting = false;
            uint32_t ts0 = lm_now_cycles();
            (void)ek_os_condvar_signal(s_cv);
            uint32_t ts1 = lm_now_cycles();
            if (i >= LM_WARMUP_ITERS)
            {
                lm_stats_record(&s_cv_sig_stats, ts1 - ts0);
            }
        }
        s_cv_t0 = lm_now_cycles();
        (void)ek_os_mutex_unlock(s_cv_mtx);
    }
    (void)ek_os_sem_give(s_cv_done);
    return NULL;
}

static void _bench_condvar(uint32_t iters)
{
    s_cv = ek_os_condvar_create();
    s_cv_mtx = ek_os_mutex_create();
    s_cv_done = ek_os_sem_create(0U);
    s_cv_waiting = false;
    s_cv_iters = LM_WARMUP_ITERS + iters;
    lm_stats_init(&s_cv_sig_stats);
    lm_stats_init(&s_cv_sw_stats);

    int tid_w = ek_os_thread_create("lm_cvw", _lm_cv_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);
    int tid_s = ek_os_thread_create("lm_cvs", _lm_cv_signaler, NULL, LM_PRIO_LOW, LM_WORKER_STACK);

    (void)ek_os_sem_take(s_cv_done, EK_OS_FOREVER);
    ek_os_thread_join(tid_w, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_s, EK_OS_FOREVER, NULL);

    ek_os_sem_destroy(s_cv_done);
    ek_os_mutex_destroy(s_cv_mtx);
    ek_os_condvar_destroy(s_cv);
    s_cv_done = NULL;
    s_cv_mtx = NULL;
    s_cv = NULL;

    lm_stats_report("condvar.signal (with waiter)", &s_cv_sig_stats);
    lm_stats_report("condvar.unlock -> wakeup+reacq", &s_cv_sw_stats);
}

/* ---------- 7. barrier ---------- */

static ek_os_barrier_t *s_bar2;
static volatile uint32_t s_bar_t0;
static volatile uint32_t s_bar_iters;
static lm_stats_t s_bar2_stats;

static void *_lm_bar_waiter(void *arg)
{
    (void)arg;
    uint32_t total = s_bar_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_barrier_wait(s_bar2, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_bar2_stats, t1 - s_bar_t0);
        }
    }
    return NULL;
}

static void _bench_barrier(uint32_t iters)
{
    lm_stats_t st_b1;
    lm_stats_init(&st_b1);
    lm_stats_init(&s_bar2_stats);

    ek_os_barrier_t *b1 = ek_os_barrier_create(1U);
    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_barrier_wait(b1, 0);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS) lm_stats_record(&st_b1, t1 - t0);
    }
    ek_os_barrier_destroy(b1);

    s_bar2 = ek_os_barrier_create(2U);
    s_bar_iters = LM_WARMUP_ITERS + iters;
    int tid = ek_os_thread_create("lm_bar", _lm_bar_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_bar_iters; i++)
    {
        s_bar_t0 = lm_now_cycles();
        ek_os_barrier_wait(s_bar2, 0);
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    ek_os_barrier_destroy(s_bar2);
    s_bar2 = NULL;

    lm_stats_report("barrier.wait (expected=1)", &st_b1);
    lm_stats_report("barrier.wait (2 thr release)", &s_bar2_stats);
}

/* ---------- 8. kfifo ---------- */

typedef struct
{
    ek_os_kfifo_node_t node;
    uint32_t val;
} lm_kfifo_item_t;

static ek_os_kfifo_t *s_fifo;
static lm_kfifo_item_t s_fifo_item;
static volatile uint32_t s_fifo_t0;
static volatile uint32_t s_fifo_iters;
static lm_stats_t s_fifo_ho_stats;

static void *_lm_fifo_waiter(void *arg)
{
    (void)arg;
    uint32_t total = s_fifo_iters;
    void *out = NULL;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_kfifo_get(s_fifo, &out, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_fifo_ho_stats, t1 - s_fifo_t0);
        }
    }
    return NULL;
}

static void _bench_kfifo(uint32_t iters)
{
    lm_stats_t st_put;
    lm_stats_t st_peek;
    lm_stats_t st_get;
    lm_stats_init(&st_put);
    lm_stats_init(&st_peek);
    lm_stats_init(&st_get);
    lm_stats_init(&s_fifo_ho_stats);

    s_fifo = ek_os_kfifo_create();
    s_fifo_item.val = 0x12345678U;
    void *out = NULL;

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_kfifo_put(s_fifo, &s_fifo_item);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_os_kfifo_peek(s_fifo, &out);
        uint32_t t3 = lm_now_cycles();

        uint32_t t4 = lm_now_cycles();
        ek_os_kfifo_get(s_fifo, &out, 0);
        uint32_t t5 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_put, t1 - t0);
            lm_stats_record(&st_peek, t3 - t2);
            lm_stats_record(&st_get, t5 - t4);
        }
    }

    s_fifo_iters = LM_WARMUP_ITERS + iters;
    int tid = ek_os_thread_create("lm_fifo", _lm_fifo_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_fifo_iters; i++)
    {
        s_fifo_t0 = lm_now_cycles();
        ek_os_kfifo_put(s_fifo, &s_fifo_item);
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    ek_os_kfifo_destroy(s_fifo);
    s_fifo = NULL;

    lm_stats_report("kfifo.put (no waiter)", &st_put);
    lm_stats_report("kfifo.peek", &st_peek);
    lm_stats_report("kfifo.get (no block)", &st_get);
    lm_stats_report("kfifo.put -> direct handoff", &s_fifo_ho_stats);
}

/* ---------- 9. msgq (16B) ---------- */

static ek_os_msgq_t *s_msgq;
static volatile uint32_t s_msgq_t0;
static volatile uint32_t s_msgq_iters;
static lm_stats_t s_msgq_sw_stats;

static void *_lm_msgq_waiter(void *arg)
{
    (void)arg;
    uint32_t buf[4];
    uint32_t total = s_msgq_iters;
    for (uint32_t i = 0U; i < total; i++)
    {
        (void)ek_os_msgq_recv(s_msgq, buf, EK_OS_FOREVER);
        uint32_t t1 = lm_now_cycles();
        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&s_msgq_sw_stats, t1 - s_msgq_t0);
        }
    }
    return NULL;
}

static void _bench_msgq(uint32_t iters)
{
    lm_stats_t st_send;
    lm_stats_t st_recv;
    lm_stats_init(&st_send);
    lm_stats_init(&st_recv);
    lm_stats_init(&s_msgq_sw_stats);

    uint32_t tx[4] = { 1U, 2U, 3U, 4U };
    uint32_t rx[4] = { 0U };
    s_msgq = ek_os_msgq_create(sizeof(tx), 4U);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        ek_os_msgq_send(s_msgq, tx, 0);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_os_msgq_recv(s_msgq, rx, 0);
        uint32_t t3 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS)
        {
            lm_stats_record(&st_send, t1 - t0);
            lm_stats_record(&st_recv, t3 - t2);
        }
    }

    s_msgq_iters = LM_WARMUP_ITERS + iters;
    int tid = ek_os_thread_create("lm_msgq", _lm_msgq_waiter, NULL, LM_PRIO_HIGH, LM_WORKER_STACK);

    for (uint32_t i = 0U; i < s_msgq_iters; i++)
    {
        s_msgq_t0 = lm_now_cycles();
        ek_os_msgq_send(s_msgq, tx, EK_OS_FOREVER);
    }

    ek_os_thread_join(tid, EK_OS_FOREVER, NULL);
    ek_os_msgq_destroy(s_msgq);
    s_msgq = NULL;

    lm_stats_report("msgq.send (16B, no waiter)", &st_send);
    lm_stats_report("msgq.recv (16B, no block)", &st_recv);
    lm_stats_report("msgq.send (16B) -> wakeup switch", &s_msgq_sw_stats);
}

/* ---------- 10. heap (TLSF 128B) ---------- */

static void _bench_heap(uint32_t iters)
{
    lm_stats_t st_alloc;
    lm_stats_t st_free;
    lm_stats_init(&st_alloc);
    lm_stats_init(&st_free);

    for (uint32_t i = 0U; i < LM_WARMUP_ITERS + iters; i++)
    {
        uint32_t t0 = lm_now_cycles();
        void *p = ek_malloc(128U);
        uint32_t t1 = lm_now_cycles();

        uint32_t t2 = lm_now_cycles();
        ek_free(p);
        uint32_t t3 = lm_now_cycles();

        if (i >= LM_WARMUP_ITERS && p != NULL)
        {
            lm_stats_record(&st_alloc, t1 - t0);
            lm_stats_record(&st_free, t3 - t2);
        }
    }

    lm_stats_report("heap.malloc (128B)", &st_alloc);
    lm_stats_report("heap.free (128B)", &st_free);
}

void lm_run_all_cases(uint32_t iters)
{
    _bench_thread_yield(iters);
    _bench_thread_suspend_resume(iters);
    _bench_thread_resume_preempt(iters);
    _bench_thread_create_join(iters);
    _bench_isr_entry_exit(iters);
    _bench_isr_to_preempt(iters);
    _bench_sem(iters);
    _bench_mutex(iters);
    _bench_condvar(iters);
    _bench_barrier(iters);
    _bench_kfifo(iters);
    _bench_msgq(iters);
    _bench_heap(iters);
}
