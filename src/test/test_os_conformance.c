/**
 * @file test_os_conformance.c
 * @brief RTOS 权威合规与边界压力测试套件（参考 ARM CMSIS-RTOS2 Validation / Zephyr kernel tests / NuttX ostest）
 */

#include "test.h"

#if EKCFG_RTOS == 1 && !defined(EK_RUN_THREAD_METRIC) && !defined(EK_RUN_LATENCY_MEASURE)

#    include "ek/core/arch.h"
#    include "ek/os/barrier.h"
#    include "ek/os/condvar.h"
#    include "ek/os/kernel.h"
#    include "ek/os/kfifo.h"
#    include "ek/os/msgq.h"
#    include "ek/os/mutex.h"
#    include "ek/os/sem.h"
#    include "ek/os/thread.h"

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_DEBUG);

#    define CONF_PRIO_CTRL (2U)
#    define CONF_PRIO_H    (4U)
#    define CONF_PRIO_M    (6U)
#    define CONF_PRIO_L    (10U)
#    define CONF_PRIO_LL   (12U)
#    define CONF_STACK     (1024U)

/* 选用工程未占用的 CAN1_RX0_IRQn（IRQ 64）作软中断合规校验，TIMER3 作高频随机压测源 */
#    define CONF_SW_IRQn CAN1_RX0_IRQn

static uint32_t s_total;
static uint32_t s_fail;

#    define CONF_CHECK(cond, msg)                         \
        do                                                \
        {                                                 \
            s_total++;                                    \
            if (!(cond))                                  \
            {                                             \
                s_fail++;                                 \
                EK_LOG_ERROR("[CONF FAIL] %s", (msg));    \
            }                                             \
        } while (0)

/* ========================================================================
 * 组 1：ARM CMSIS-RTOS2 Validation 风格 ISR 上下文 API 合规性验证
 * ======================================================================== */

typedef struct
{
    ek_os_kfifo_node_t node;
    uint32_t seq;
} conf_kfifo_item_t;

static struct
{
    volatile bool executed;
    int ctrl_tid;
    int susp_tid;
    ek_os_sem_t *sem;
    ek_os_mutex_t *mtx;
    ek_os_condvar_t *cv;
    ek_os_barrier_t *bar;
    ek_os_msgq_t *mq;
    ek_os_kfifo_t *fifo_wait;
    ek_os_kfifo_t *fifo_idle;
    conf_kfifo_item_t wait_item;
    conf_kfifo_item_t idle_item;
    uint8_t dummy_thrd_buf[512] __EK_ALIGNED(8);

    /* ISR 内部捕获返回值 */
    int r_thrd_create;
    int r_thrd_create_static;
    int r_thrd_suspend;
    int r_thrd_resume;
    int r_thrd_set_prio;
    int r_thrd_get_prio;
    uint8_t got_prio;
    int r_thrd_join;
    int r_thrd_yield;
    int r_thrd_sleep;
    int r_sched_lock;
    int r_sched_unlock;
    int r_cur_tid;
    uint64_t r_time;
    bool sem_create_null;
    int r_sem_take;
    int r_sem_give;
    bool mtx_create_null;
    int r_mtx_lock;
    int r_mtx_unlock;
    bool cv_create_null;
    int r_cv_wait;
    int r_cv_signal;
    int r_cv_broadcast;
    bool bar_create_null;
    int r_bar_wait;
    int r_bar_reset;
    bool mq_create_null;
    int r_mq_recv;
    int r_mq_send1;
    int r_mq_send2;
    bool fifo_create_null;
    int r_fifo_put_wait;
    int r_fifo_put_idle;
    int r_fifo_peek;
    void *peek_ptr;
    bool fifo_empty_after_put;
    int r_fifo_get_ok;
    void *get_ptr;
    bool fifo_empty_after_get;
    int r_fifo_get_empty;
    int r_fifo_get_block;
} s_isr_ctx;

static void *_dummy_thread_fn(void *arg)
{
    (void)arg;
    return NULL;
}

static void _conf_sw_irq_handler(void)
{
    NVIC_ClearPendingIRQ(CONF_SW_IRQn);

    s_isr_ctx.r_thrd_create = ek_os_thread_create("isr_x", _dummy_thread_fn, NULL, CONF_PRIO_L, CONF_STACK);
    s_isr_ctx.r_thrd_create_static = ek_os_thread_create_static(
        "isr_s", _dummy_thread_fn, NULL, CONF_PRIO_L, s_isr_ctx.dummy_thrd_buf, sizeof(s_isr_ctx.dummy_thrd_buf));
    s_isr_ctx.r_thrd_suspend = ek_os_thread_suspend(s_isr_ctx.ctrl_tid);
    s_isr_ctx.r_thrd_set_prio = ek_os_thread_set_prio(s_isr_ctx.ctrl_tid, CONF_PRIO_M);
    s_isr_ctx.r_thrd_join = ek_os_thread_join(s_isr_ctx.susp_tid, 0, NULL);
    s_isr_ctx.r_thrd_yield = ek_os_thread_yield();
    s_isr_ctx.r_thrd_sleep = ek_os_thread_sleep(1000);
    s_isr_ctx.r_sched_lock = ek_os_kernel_sched_lock();
    s_isr_ctx.r_sched_unlock = ek_os_kernel_sched_unlock();

    ek_os_sem_t *s = ek_os_sem_create(0);
    s_isr_ctx.sem_create_null = (s == NULL);
    if (s) ek_os_sem_destroy(s);
    s_isr_ctx.r_sem_take = ek_os_sem_take(s_isr_ctx.sem, 0);

    ek_os_mutex_t *m = ek_os_mutex_create();
    s_isr_ctx.mtx_create_null = (m == NULL);
    if (m) ek_os_mutex_destroy(m);
    s_isr_ctx.r_mtx_lock = ek_os_mutex_lock(s_isr_ctx.mtx, 0);
    s_isr_ctx.r_mtx_unlock = ek_os_mutex_unlock(s_isr_ctx.mtx);

    ek_os_condvar_t *c = ek_os_condvar_create();
    s_isr_ctx.cv_create_null = (c == NULL);
    if (c) ek_os_condvar_destroy(c);
    s_isr_ctx.r_cv_wait = ek_os_condvar_wait(s_isr_ctx.cv, s_isr_ctx.mtx, 1000);

    ek_os_barrier_t *b = ek_os_barrier_create(1);
    s_isr_ctx.bar_create_null = (b == NULL);
    if (b) ek_os_barrier_destroy(b);
    s_isr_ctx.r_bar_wait = ek_os_barrier_wait(s_isr_ctx.bar, 0);

    ek_os_msgq_t *q = ek_os_msgq_create(sizeof(uint32_t), 2);
    s_isr_ctx.mq_create_null = (q == NULL);
    if (q) ek_os_msgq_destroy(q);
    uint32_t rx = 0U;
    s_isr_ctx.r_mq_recv = ek_os_msgq_recv(s_isr_ctx.mq, &rx, 0);

    ek_os_kfifo_t *f = ek_os_kfifo_create();
    s_isr_ctx.fifo_create_null = (f == NULL);
    if (f) ek_os_kfifo_destroy(f);

    /* 允许在 ISR 中调用的接口 */
    s_isr_ctx.r_time = ek_os_kernel_time();
    s_isr_ctx.r_cur_tid = ek_os_kernel_current_tid();
    s_isr_ctx.got_prio = 0xFFU;
    s_isr_ctx.r_thrd_get_prio = ek_os_thread_get_prio(s_isr_ctx.ctrl_tid, &s_isr_ctx.got_prio);

    s_isr_ctx.r_sem_give = ek_os_sem_give(s_isr_ctx.sem);
    uint32_t v1 = 0xCAFEU;
    uint32_t v2 = 0xBEEFU;
    s_isr_ctx.r_mq_send1 = ek_os_msgq_send(s_isr_ctx.mq, &v1, EK_OS_FOREVER);
    s_isr_ctx.r_mq_send2 = ek_os_msgq_send(s_isr_ctx.mq, &v2, EK_OS_FOREVER);

    s_isr_ctx.wait_item.seq = 77U;
    s_isr_ctx.r_fifo_put_wait = ek_os_kfifo_put(s_isr_ctx.fifo_wait, &s_isr_ctx.wait_item);

    s_isr_ctx.idle_item.seq = 88U;
    s_isr_ctx.r_fifo_put_idle = ek_os_kfifo_put(s_isr_ctx.fifo_idle, &s_isr_ctx.idle_item);
    s_isr_ctx.peek_ptr = NULL;
    s_isr_ctx.r_fifo_peek = ek_os_kfifo_peek(s_isr_ctx.fifo_idle, &s_isr_ctx.peek_ptr);
    s_isr_ctx.fifo_empty_after_put = ek_os_kfifo_is_empty(s_isr_ctx.fifo_idle);
    s_isr_ctx.get_ptr = NULL;
    s_isr_ctx.r_fifo_get_ok = ek_os_kfifo_get(s_isr_ctx.fifo_idle, &s_isr_ctx.get_ptr, 0);
    s_isr_ctx.fifo_empty_after_get = ek_os_kfifo_is_empty(s_isr_ctx.fifo_idle);
    void *tmp_ptr = NULL;
    s_isr_ctx.r_fifo_get_empty = ek_os_kfifo_get(s_isr_ctx.fifo_idle, &tmp_ptr, 0);
    s_isr_ctx.r_fifo_get_block = ek_os_kfifo_get(s_isr_ctx.fifo_idle, &tmp_ptr, 1000);

    s_isr_ctx.r_cv_signal = ek_os_condvar_signal(s_isr_ctx.cv);
    s_isr_ctx.r_cv_broadcast = ek_os_condvar_broadcast(s_isr_ctx.cv);
    s_isr_ctx.r_thrd_resume = ek_os_thread_resume(s_isr_ctx.susp_tid);
    s_isr_ctx.r_bar_reset = ek_os_barrier_reset(s_isr_ctx.bar, 2);

    s_isr_ctx.executed = true;
}

EK_DEFINE_ARCH_IRQ(CONF_SW_IRQn, _conf_sw_irq_handler);

static volatile int s_isr_sem_waiter_err = -1;
static volatile bool s_isr_susp_resumed;
static volatile int s_isr_fifo_waiter_err = -1;
static volatile uint32_t s_isr_fifo_waiter_seq;

static void *_isr_sem_waiter(void *arg)
{
    (void)arg;
    s_isr_sem_waiter_err = ek_os_sem_take(s_isr_ctx.sem, EK_OS_FOREVER);
    return NULL;
}

static void *_isr_susp_worker(void *arg)
{
    (void)arg;
    ek_os_thread_suspend(ek_os_kernel_current_tid());
    s_isr_susp_resumed = true;
    return NULL;
}

static void *_isr_fifo_waiter(void *arg)
{
    (void)arg;
    void *p = NULL;
    s_isr_fifo_waiter_err = ek_os_kfifo_get(s_isr_ctx.fifo_wait, &p, EK_OS_FOREVER);
    if (s_isr_fifo_waiter_err == 0 && p)
    {
        s_isr_fifo_waiter_seq = ((conf_kfifo_item_t *)p)->seq;
    }
    return NULL;
}

static void _test_isr_conformance(void)
{
    memset((void *)&s_isr_ctx, 0, sizeof(s_isr_ctx));
    s_isr_ctx.ctrl_tid = ek_os_kernel_current_tid();
    s_isr_ctx.sem = ek_os_sem_create(0);
    s_isr_ctx.mtx = ek_os_mutex_create();
    s_isr_ctx.cv = ek_os_condvar_create();
    s_isr_ctx.bar = ek_os_barrier_create(1);
    s_isr_ctx.mq = ek_os_msgq_create(sizeof(uint32_t), 1);
    s_isr_ctx.fifo_wait = ek_os_kfifo_create();
    s_isr_ctx.fifo_idle = ek_os_kfifo_create();

    CONF_CHECK(s_isr_ctx.sem && s_isr_ctx.mtx && s_isr_ctx.cv && s_isr_ctx.bar && s_isr_ctx.mq &&
                   s_isr_ctx.fifo_wait && s_isr_ctx.fifo_idle,
               "RV2 ISR: create test objects");
    if (!s_isr_ctx.sem || !s_isr_ctx.mtx || !s_isr_ctx.cv || !s_isr_ctx.bar || !s_isr_ctx.mq ||
        !s_isr_ctx.fifo_wait || !s_isr_ctx.fifo_idle)
    {
        return;
    }

    s_isr_sem_waiter_err = -1;
    s_isr_susp_resumed = false;
    s_isr_fifo_waiter_err = -1;
    s_isr_fifo_waiter_seq = 0U;

    int sem_tid = ek_os_thread_create("rv2_sem_w", _isr_sem_waiter, NULL, CONF_PRIO_M, CONF_STACK);
    int fifo_tid = ek_os_thread_create("rv2_fifo_w", _isr_fifo_waiter, NULL, CONF_PRIO_M, CONF_STACK);
    s_isr_ctx.susp_tid = ek_os_thread_create("rv2_susp", _isr_susp_worker, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(3000);

    nvic_irq_enable(CONF_SW_IRQn, 0, 2);
    NVIC_SetPendingIRQ(CONF_SW_IRQn);
    ek_arch_dsb();
    ek_arch_isb();

    CONF_CHECK(s_isr_ctx.executed, "RV2 ISR: sw irq executed");
    CONF_CHECK(s_isr_ctx.r_thrd_create == -EPERM, "RV2 ISR: thread_create returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_create_static == -EPERM, "RV2 ISR: thread_create_static returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_suspend == -EPERM, "RV2 ISR: thread_suspend returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_set_prio == -EPERM, "RV2 ISR: thread_set_prio returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_join == -EPERM, "RV2 ISR: thread_join returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_yield == -EPERM, "RV2 ISR: thread_yield returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_thrd_sleep == -EPERM, "RV2 ISR: thread_sleep returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_sched_lock == -EPERM, "RV2 ISR: sched_lock returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_sched_unlock == -EPERM, "RV2 ISR: sched_unlock returns -EPERM");
    CONF_CHECK(s_isr_ctx.sem_create_null, "RV2 ISR: sem_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_sem_take == -EPERM, "RV2 ISR: sem_take returns -EPERM");
    CONF_CHECK(s_isr_ctx.mtx_create_null, "RV2 ISR: mutex_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_mtx_lock == -EPERM, "RV2 ISR: mutex_lock returns -EPERM");
    CONF_CHECK(s_isr_ctx.r_mtx_unlock == -EPERM, "RV2 ISR: mutex_unlock returns -EPERM");
    CONF_CHECK(s_isr_ctx.cv_create_null, "RV2 ISR: condvar_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_cv_wait == -EPERM, "RV2 ISR: condvar_wait returns -EPERM");
    CONF_CHECK(s_isr_ctx.bar_create_null, "RV2 ISR: barrier_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_bar_wait == -EPERM, "RV2 ISR: barrier_wait returns -EPERM");
    CONF_CHECK(s_isr_ctx.mq_create_null, "RV2 ISR: msgq_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_mq_recv == -EPERM, "RV2 ISR: msgq_recv returns -EPERM");
    CONF_CHECK(s_isr_ctx.fifo_create_null, "RV2 ISR: kfifo_create returns NULL");
    CONF_CHECK(s_isr_ctx.r_fifo_get_block == -EPERM, "RV2 ISR: kfifo_get(blocking) returns -EPERM");

    CONF_CHECK(s_isr_ctx.r_time > 0ULL, "RV2 ISR: kernel_time > 0");
    CONF_CHECK(s_isr_ctx.r_cur_tid > 0, "RV2 ISR: current_tid > 0");
    CONF_CHECK(s_isr_ctx.r_thrd_get_prio == 0 && s_isr_ctx.got_prio == CONF_PRIO_CTRL,
               "RV2 ISR: thread_get_prio returns 0");
    CONF_CHECK(s_isr_ctx.r_sem_give == 0, "RV2 ISR: sem_give returns 0");
    CONF_CHECK(s_isr_ctx.r_mq_send1 == 0, "RV2 ISR: msgq_send(space) returns 0");
    CONF_CHECK(s_isr_ctx.r_mq_send2 == -EAGAIN, "RV2 ISR: msgq_send(full) degrades to -EAGAIN");
    CONF_CHECK(s_isr_ctx.r_fifo_put_wait == 0, "RV2 ISR: kfifo_put direct handoff returns 0");
    CONF_CHECK(s_isr_ctx.r_fifo_put_idle == 0 && s_isr_ctx.r_fifo_peek == 0 &&
                   s_isr_ctx.peek_ptr == &s_isr_ctx.idle_item && !s_isr_ctx.fifo_empty_after_put &&
                   s_isr_ctx.r_fifo_get_ok == 0 && s_isr_ctx.get_ptr == &s_isr_ctx.idle_item &&
                   s_isr_ctx.fifo_empty_after_get && s_isr_ctx.r_fifo_get_empty == -EAGAIN,
               "RV2 ISR: kfifo put/peek/is_empty/get(0) ok");
    CONF_CHECK(s_isr_ctx.r_cv_signal == 0 && s_isr_ctx.r_cv_broadcast == 0, "RV2 ISR: condvar signal/broadcast ok");
    CONF_CHECK(s_isr_ctx.r_thrd_resume == 0, "RV2 ISR: thread_resume returns 0");
    CONF_CHECK(s_isr_ctx.r_bar_reset == 0, "RV2 ISR: barrier_reset returns 0");

    uint32_t mq_rx = 0U;
    CONF_CHECK(ek_os_msgq_recv(s_isr_ctx.mq, &mq_rx, 0) == 0 && mq_rx == 0xCAFEU,
               "RV2 ISR: thread receives msg queued by ISR");

    CONF_CHECK(ek_os_thread_join(sem_tid, EK_OS_MSEC(50), NULL) == 0 && s_isr_sem_waiter_err == 0,
               "RV2 ISR: sem_waiter woken by ISR");
    CONF_CHECK(ek_os_thread_join(fifo_tid, EK_OS_MSEC(50), NULL) == 0 && s_isr_fifo_waiter_err == 0 &&
                   s_isr_fifo_waiter_seq == 77U,
               "RV2 ISR: kfifo_waiter got direct handoff from ISR");
    CONF_CHECK(ek_os_thread_join(s_isr_ctx.susp_tid, EK_OS_MSEC(50), NULL) == 0 && s_isr_susp_resumed,
               "RV2 ISR: suspended thread woken by ISR");

    nvic_irq_disable(CONF_SW_IRQn);
    ek_os_sem_destroy(s_isr_ctx.sem);
    ek_os_mutex_destroy(s_isr_ctx.mtx);
    ek_os_condvar_destroy(s_isr_ctx.cv);
    ek_os_barrier_destroy(s_isr_ctx.bar);
    ek_os_msgq_destroy(s_isr_ctx.mq);
    ek_os_kfifo_destroy(s_isr_ctx.fifo_wait);
    ek_os_kfifo_destroy(s_isr_ctx.fifo_idle);
}

/* ========================================================================
 * 组 2：Zephyr mutex_api 风格高级优先级继承与多锁阶梯回落验证
 * ======================================================================== */

static ek_os_mutex_t *s_pi_m1;
static ek_os_mutex_t *s_pi_m2;
static ek_os_sem_t *s_pi_step1;
static ek_os_sem_t *s_pi_step2;

static void *_pi_dual_holder(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER);
    ek_os_mutex_lock(s_pi_m2, EK_OS_FOREVER);
    ek_os_sem_take(s_pi_step1, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_pi_m1);
    ek_os_sem_take(s_pi_step2, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_pi_m2);
    return NULL;
}

static void *_pi_contend_m1(void *arg)
{
    (void)arg;
    if (ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER) == 0)
    {
        ek_os_mutex_unlock(s_pi_m1);
    }
    return NULL;
}

static void *_pi_contend_m2(void *arg)
{
    (void)arg;
    if (ek_os_mutex_lock(s_pi_m2, EK_OS_FOREVER) == 0)
    {
        ek_os_mutex_unlock(s_pi_m2);
    }
    return NULL;
}

static void *_pi_chain_l(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER);
    ek_os_sem_take(s_pi_step1, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_pi_m1);
    return NULL;
}

static void *_pi_chain_m(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_pi_m2, EK_OS_FOREVER);
    ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_pi_m1);
    ek_os_mutex_unlock(s_pi_m2);
    return NULL;
}

static void *_pi_chain_h_timeout(void *arg)
{
    (void)arg;
    int err = ek_os_mutex_lock(s_pi_m2, 15000);
    if (err == 0) ek_os_mutex_unlock(s_pi_m2);
    return (void *)(intptr_t)err;
}

static volatile int s_mtx_order[3];
static volatile int s_mtx_order_idx;

static void *_mtx_order_waiter(void *arg)
{
    int tag = (int)(intptr_t)arg;
    if (ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER) == 0)
    {
        s_mtx_order[s_mtx_order_idx++] = tag;
        ek_os_mutex_unlock(s_pi_m1);
    }
    return NULL;
}

static volatile uint8_t s_setprio_after_unlock;

static void *_pi_setprio_holder(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER);
    ek_os_sem_take(s_pi_step1, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_pi_m1);
    ek_os_thread_get_prio(ek_os_kernel_current_tid(), (uint8_t *)&s_setprio_after_unlock);
    return NULL;
}

static void _test_pi_advanced(void)
{
    /* 同持双锁分步释放阶梯回落 */
    s_pi_m1 = ek_os_mutex_create();
    s_pi_m2 = ek_os_mutex_create();
    s_pi_step1 = ek_os_sem_create(0);
    s_pi_step2 = ek_os_sem_create(0);

    int l_tid = ek_os_thread_create("pi2_l", _pi_dual_holder, NULL, CONF_PRIO_L, CONF_STACK);
    ek_os_thread_sleep(3000);
    int h_tid = ek_os_thread_create("pi2_h", _pi_contend_m1, NULL, CONF_PRIO_H, CONF_STACK);
    int m_tid = ek_os_thread_create("pi2_m", _pi_contend_m2, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(3000);

    uint8_t p = 0xFFU;
    CONF_CHECK(ek_os_thread_get_prio(l_tid, &p) == 0 && p == CONF_PRIO_H, "PI dual: L inherits H from M1");

    ek_os_sem_give(s_pi_step1);
    ek_os_thread_sleep(3000);
    CONF_CHECK(ek_os_thread_join(h_tid, EK_OS_MSEC(50), NULL) == 0, "PI dual: join H after M1 release");
    CONF_CHECK(ek_os_thread_get_prio(l_tid, &p) == 0 && p == CONF_PRIO_M,
               "PI dual: L steps down cleanly to M from held M2");

    ek_os_sem_give(s_pi_step2);
    ek_os_thread_sleep(3000);
    CONF_CHECK(ek_os_thread_join(m_tid, EK_OS_MSEC(50), NULL) == 0, "PI dual: join M after M2 release");
    CONF_CHECK(ek_os_thread_join(l_tid, EK_OS_MSEC(50), NULL) == 0, "PI dual: join L");

    ek_os_mutex_destroy(s_pi_m1);
    ek_os_mutex_destroy(s_pi_m2);
    ek_os_sem_destroy(s_pi_step1);
    ek_os_sem_destroy(s_pi_step2);

    /* 多级 PI 链顶层等待超时时，沿锁链级联回落 */
    s_pi_m1 = ek_os_mutex_create();
    s_pi_m2 = ek_os_mutex_create();
    s_pi_step1 = ek_os_sem_create(0);

    l_tid = ek_os_thread_create("pic_l", _pi_chain_l, NULL, CONF_PRIO_L, CONF_STACK);
    ek_os_thread_sleep(3000);
    m_tid = ek_os_thread_create("pic_m", _pi_chain_m, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(3000);
    h_tid = ek_os_thread_create("pic_h", _pi_chain_h_timeout, NULL, CONF_PRIO_H, CONF_STACK);
    ek_os_thread_sleep(4000);

    uint8_t pm = 0xFFU;
    uint8_t pl = 0xFFU;
    CONF_CHECK(ek_os_thread_get_prio(m_tid, &pm) == 0 && pm == CONF_PRIO_H, "PI cascade: M inherits H");
    CONF_CHECK(ek_os_thread_get_prio(l_tid, &pl) == 0 && pl == CONF_PRIO_H, "PI cascade: L inherits H");

    void *h_ret = NULL;
    CONF_CHECK(ek_os_thread_join(h_tid, EK_OS_MSEC(100), &h_ret) == 0 && (int)(intptr_t)h_ret == -ETIMEDOUT,
               "PI cascade: H lock times out with -ETIMEDOUT");
    CONF_CHECK(ek_os_thread_get_prio(m_tid, &pm) == 0 && pm == CONF_PRIO_M, "PI cascade: M restores to CONF_PRIO_M");
    CONF_CHECK(ek_os_thread_get_prio(l_tid, &pl) == 0 && pl == CONF_PRIO_M,
               "PI cascade: L restores from H to M (not L)");

    ek_os_sem_give(s_pi_step1);
    CONF_CHECK(ek_os_thread_join(m_tid, EK_OS_MSEC(50), NULL) == 0, "PI cascade: join M");
    CONF_CHECK(ek_os_thread_join(l_tid, EK_OS_MSEC(50), NULL) == 0, "PI cascade: join L");

    ek_os_mutex_destroy(s_pi_m1);
    ek_os_mutex_destroy(s_pi_m2);
    ek_os_sem_destroy(s_pi_step1);

    /* Mutex 阻塞队列严格按优先级移交 */
    s_pi_m1 = ek_os_mutex_create();
    s_mtx_order_idx = 0;
    ek_os_mutex_lock(s_pi_m1, EK_OS_FOREVER);

    int w_l = ek_os_thread_create("mo_l", _mtx_order_waiter, (void *)(intptr_t)3, CONF_PRIO_L, CONF_STACK);
    ek_os_thread_sleep(2000);
    int w_m = ek_os_thread_create("mo_m", _mtx_order_waiter, (void *)(intptr_t)2, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    int w_h = ek_os_thread_create("mo_h", _mtx_order_waiter, (void *)(intptr_t)1, CONF_PRIO_H, CONF_STACK);
    ek_os_thread_sleep(2000);

    ek_os_mutex_unlock(s_pi_m1);
    CONF_CHECK(ek_os_thread_join(w_h, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(w_m, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(w_l, EK_OS_MSEC(50), NULL) == 0,
               "Mutex order: join all waiters");
    CONF_CHECK(s_mtx_order_idx == 3 && s_mtx_order[0] == 1 && s_mtx_order[1] == 2 && s_mtx_order[2] == 3,
               "Mutex order: wakeup strictly H -> M -> L");
    ek_os_mutex_destroy(s_pi_m1);

    /* 被提权持锁期间动态修改 base_prio */
    s_pi_m1 = ek_os_mutex_create();
    s_pi_step1 = ek_os_sem_create(0);
    s_setprio_after_unlock = 0xFFU;

    l_tid = ek_os_thread_create("pis_l", _pi_setprio_holder, NULL, CONF_PRIO_L, CONF_STACK);
    ek_os_thread_sleep(3000);
    h_tid = ek_os_thread_create("pis_h", _pi_contend_m1, NULL, CONF_PRIO_H, CONF_STACK);
    ek_os_thread_sleep(3000);

    CONF_CHECK(ek_os_thread_set_prio(l_tid, CONF_PRIO_LL) == 0, "PI set_prio on inherited owner succeeds");
    CONF_CHECK(ek_os_thread_get_prio(l_tid, &p) == 0 && p == CONF_PRIO_H,
               "PI set_prio preserves inherited effective prio while locked");

    ek_os_sem_give(s_pi_step1);
    CONF_CHECK(ek_os_thread_join(h_tid, EK_OS_MSEC(50), NULL) == 0, "PI set_prio: join H");
    CONF_CHECK(ek_os_thread_join(l_tid, EK_OS_MSEC(50), NULL) == 0, "PI set_prio: join L");
    CONF_CHECK(s_setprio_after_unlock == CONF_PRIO_LL, "PI set_prio: unlocks into new lowered base_prio");

    ek_os_mutex_destroy(s_pi_m1);
    ek_os_sem_destroy(s_pi_step1);
}

/* ========================================================================
 * 组 3：Zephyr condvar 风格 Wait Morphing 边界验证
 * ======================================================================== */

static ek_os_condvar_t *s_cvm_cv;
static ek_os_mutex_t *s_cvm_mtx;
static volatile bool s_cvm_waiting;
static volatile bool s_cvm_done;
static volatile int s_cvm_err;
static volatile int s_cvm_bc_order[3];
static volatile int s_cvm_bc_idx;

static void *_cvm_morph_worker(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_cvm_mtx, EK_OS_FOREVER);
    s_cvm_waiting = true;
    s_cvm_err = ek_os_condvar_wait(s_cvm_cv, s_cvm_mtx, 15000);
    s_cvm_done = true;
    ek_os_mutex_unlock(s_cvm_mtx);
    return NULL;
}

static void *_cvm_bc_worker(void *arg)
{
    int tag = (int)(intptr_t)arg;
    ek_os_mutex_lock(s_cvm_mtx, EK_OS_FOREVER);
    ek_os_condvar_wait(s_cvm_cv, s_cvm_mtx, EK_OS_FOREVER);
    s_cvm_bc_order[s_cvm_bc_idx++] = tag;
    ek_os_mutex_unlock(s_cvm_mtx);
    return NULL;
}

static void _test_condvar_morph_edge(void)
{
    s_cvm_cv = ek_os_condvar_create();
    s_cvm_mtx = ek_os_mutex_create();
    s_cvm_waiting = false;
    s_cvm_done = false;
    s_cvm_err = -1;

    int w_tid = ek_os_thread_create("cvm_w", _cvm_morph_worker, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(3000);
    CONF_CHECK(s_cvm_waiting, "CV morph: worker entered condvar_wait");

    /* 持锁 signal 触发原地转移并撤销原 15 ms 超时，持续持锁 25 ms 验证不会提前超时 */
    ek_os_mutex_lock(s_cvm_mtx, EK_OS_FOREVER);
    CONF_CHECK(ek_os_condvar_signal(s_cvm_cv) == 0, "CV morph: signal under held mutex ok");
    ek_os_thread_sleep(25000);
    CONF_CHECK(!s_cvm_done, "CV morph: original condvar timeout cancelled across morph");

    /* 等锁阶段挂起与恢复仍应保留已送达信号 */
    CONF_CHECK(ek_os_thread_suspend(w_tid) == 0, "CV morph: suspend morphed waiter");
    ek_os_thread_sleep(2000);
    CONF_CHECK(ek_os_thread_resume(w_tid) == 0, "CV morph: resume morphed waiter");
    ek_os_mutex_unlock(s_cvm_mtx);

    CONF_CHECK(ek_os_thread_join(w_tid, EK_OS_MSEC(50), NULL) == 0, "CV morph: join waiter");
    CONF_CHECK(s_cvm_done && s_cvm_err == 0, "CV morph: waiter preserved signal across suspend/resume and returned 0");

    /* broadcast 多优先级唤醒顺序 */
    s_cvm_bc_idx = 0;
    int b_l = ek_os_thread_create("cvb_l", _cvm_bc_worker, (void *)(intptr_t)3, CONF_PRIO_L, CONF_STACK);
    int b_m = ek_os_thread_create("cvb_m", _cvm_bc_worker, (void *)(intptr_t)2, CONF_PRIO_M, CONF_STACK);
    int b_h = ek_os_thread_create("cvb_h", _cvm_bc_worker, (void *)(intptr_t)1, CONF_PRIO_H, CONF_STACK);
    ek_os_thread_sleep(3000);

    ek_os_mutex_lock(s_cvm_mtx, EK_OS_FOREVER);
    CONF_CHECK(ek_os_condvar_broadcast(s_cvm_cv) == 0, "CV morph: broadcast under held mutex ok");
    ek_os_mutex_unlock(s_cvm_mtx);

    CONF_CHECK(ek_os_thread_join(b_h, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(b_m, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(b_l, EK_OS_MSEC(50), NULL) == 0,
               "CV morph: join broadcast waiters");
    CONF_CHECK(s_cvm_bc_idx == 3 && s_cvm_bc_order[0] == 1 && s_cvm_bc_order[1] == 2 && s_cvm_bc_order[2] == 3,
               "CV morph: broadcast releases strictly H -> M -> L");

    ek_os_condvar_destroy(s_cvm_cv);
    ek_os_mutex_destroy(s_cvm_mtx);
}

/* ========================================================================
 * 组 4：Tickless 交错超时、取消队头与跨 16 位量程续期验证
 * ======================================================================== */

static volatile int s_sleep_order[3];
static volatile int s_sleep_order_idx;
static volatile uint64_t s_sleep_elapsed[3];

typedef struct
{
    int id;
    int64_t us;
} sleep_arg_t;

static void *_sleep_worker(void *arg)
{
    const sleep_arg_t *cfg = (const sleep_arg_t *)arg;
    uint64_t t0 = ek_os_kernel_time();
    ek_os_thread_sleep(cfg->us);
    uint64_t t1 = ek_os_kernel_time();
    s_sleep_elapsed[cfg->id] = t1 - t0;
    s_sleep_order[s_sleep_order_idx++] = cfg->id;
    return NULL;
}

static ek_os_sem_t *s_tl_sem;
static volatile int s_tl_sem_err = -1;
static volatile uint64_t s_tl_tail_elapsed;

static void *_tl_head_sem_waiter(void *arg)
{
    (void)arg;
    s_tl_sem_err = ek_os_sem_take(s_tl_sem, 10000);
    return NULL;
}

static void *_tl_tail_sleeper(void *arg)
{
    (void)arg;
    uint64_t t0 = ek_os_kernel_time();
    ek_os_thread_sleep(25000);
    s_tl_tail_elapsed = ek_os_kernel_time() - t0;
    return NULL;
}

static void *_tl_mtx_holder_10ms(void *arg)
{
    ek_os_mutex_t *m = (ek_os_mutex_t *)arg;
    ek_os_mutex_lock(m, EK_OS_FOREVER);
    ek_os_thread_sleep(10000);
    ek_os_mutex_unlock(m);
    return NULL;
}

static void *_tl_sleeper_10ms(void *arg)
{
    (void)arg;
    ek_os_thread_sleep(10000);
    return NULL;
}

static void _test_tickless_timeouts(void)
{
    /* 乱序插入更早到期项，校验队头动态替换与唤醒次序 */
    static sleep_arg_t args[3] = {
        { 0, 30000 },
        { 1, 5000 },
        { 2, 12000 },
    };
    s_sleep_order_idx = 0;

    int ta = ek_os_thread_create("slp_a", _sleep_worker, &args[0], CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    int tb = ek_os_thread_create("slp_b", _sleep_worker, &args[1], CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(1000);
    int tc = ek_os_thread_create("slp_c", _sleep_worker, &args[2], CONF_PRIO_M, CONF_STACK);

    CONF_CHECK(ek_os_thread_join(tb, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(tc, EK_OS_MSEC(50), NULL) == 0 &&
                   ek_os_thread_join(ta, EK_OS_MSEC(100), NULL) == 0,
               "Tickless out-of-order: join all");
    CONF_CHECK(s_sleep_order_idx == 3 && s_sleep_order[0] == 1 && s_sleep_order[1] == 2 && s_sleep_order[2] == 0,
               "Tickless out-of-order: wakeup order B -> C -> A");
    CONF_CHECK(s_sleep_elapsed[1] >= 4800ULL && s_sleep_elapsed[1] <= 9000ULL && s_sleep_elapsed[2] >= 11800ULL &&
                   s_sleep_elapsed[2] <= 17000ULL && s_sleep_elapsed[0] >= 29800ULL && s_sleep_elapsed[0] <= 36000ULL,
               "Tickless out-of-order: elapsed durations accurate");

    /* 提前取消队头超时项后，后续闹钟重同步不丢失 */
    s_tl_sem = ek_os_sem_create(0);
    s_tl_sem_err = -1;
    s_tl_tail_elapsed = 0ULL;

    int head_tid = ek_os_thread_create("tl_head", _tl_head_sem_waiter, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(1000);
    int tail_tid = ek_os_thread_create("tl_tail", _tl_tail_sleeper, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);

    CONF_CHECK(ek_os_sem_give(s_tl_sem) == 0, "Tickless cancel head: sem_give wakes head");
    CONF_CHECK(ek_os_thread_join(head_tid, EK_OS_MSEC(50), NULL) == 0 && s_tl_sem_err == 0,
               "Tickless cancel head: head joined with err=0");
    CONF_CHECK(ek_os_thread_join(tail_tid, EK_OS_MSEC(100), NULL) == 0, "Tickless cancel head: tail joined");
    CONF_CHECK(s_tl_tail_elapsed >= 23000ULL && s_tl_tail_elapsed <= 32000ULL,
               "Tickless cancel head: tail sleeper woke accurately");
    ek_os_sem_destroy(s_tl_sem);

    /* 跨 TIMER5 16 位最大量程 (65535 us) 自动续期 */
    uint64_t t0 = ek_os_kernel_time();
    CONF_CHECK(ek_os_thread_sleep(100000) == 0, "Tickless >65ms sleep returns 0");
    uint64_t dt = ek_os_kernel_time() - t0;
    CONF_CHECK(dt >= 98000ULL && dt <= 112000ULL, "Tickless >65ms (100ms) duration within [98, 112] ms");

    /* 全原语有限超时统一返回 -ETIMEDOUT */
    ek_os_sem_t *to_sem = ek_os_sem_create(0);
    ek_os_mutex_t *to_mtx = ek_os_mutex_create();
    ek_os_condvar_t *to_cv = ek_os_condvar_create();
    ek_os_barrier_t *to_bar = ek_os_barrier_create(2);
    ek_os_msgq_t *to_mq = ek_os_msgq_create(sizeof(uint32_t), 1);
    ek_os_kfifo_t *to_fifo = ek_os_kfifo_create();

    CONF_CHECK(ek_os_sem_take(to_sem, 3000) == -ETIMEDOUT, "Timeout: sem_take -> -ETIMEDOUT");

    int mh_tid = ek_os_thread_create("to_mh", _tl_mtx_holder_10ms, to_mtx, CONF_PRIO_H, CONF_STACK);
    ek_os_thread_sleep(1000);
    CONF_CHECK(ek_os_mutex_lock(to_mtx, 3000) == -ETIMEDOUT, "Timeout: mutex_lock -> -ETIMEDOUT");
    ek_os_thread_join(mh_tid, EK_OS_MSEC(50), NULL);

    CONF_CHECK(ek_os_mutex_lock(to_mtx, 0) == 0, "Timeout: lock mtx for condvar");
    CONF_CHECK(ek_os_condvar_wait(to_cv, to_mtx, 3000) == -ETIMEDOUT, "Timeout: condvar_wait -> -ETIMEDOUT");
    CONF_CHECK(ek_os_mutex_unlock(to_mtx) == 0, "Timeout: unlock mtx after condvar timeout");

    CONF_CHECK(ek_os_barrier_wait(to_bar, 3000) == -ETIMEDOUT, "Timeout: barrier_wait -> -ETIMEDOUT");

    uint32_t rx = 0U;
    CONF_CHECK(ek_os_msgq_recv(to_mq, &rx, 3000) == -ETIMEDOUT, "Timeout: msgq_recv -> -ETIMEDOUT");
    uint32_t tx = 1U;
    ek_os_msgq_send(to_mq, &tx, 0);
    CONF_CHECK(ek_os_msgq_send(to_mq, &tx, 3000) == -ETIMEDOUT, "Timeout: msgq_send(full) -> -ETIMEDOUT");

    void *fp = NULL;
    CONF_CHECK(ek_os_kfifo_get(to_fifo, &fp, 3000) == -ETIMEDOUT, "Timeout: kfifo_get -> -ETIMEDOUT");

    int slp_tid = ek_os_thread_create("to_jn", _tl_sleeper_10ms, NULL, CONF_PRIO_L, CONF_STACK);
    CONF_CHECK(ek_os_thread_join(slp_tid, 3000, NULL) == -ETIMEDOUT, "Timeout: thread_join -> -ETIMEDOUT");
    ek_os_thread_join(slp_tid, EK_OS_MSEC(50), NULL);

    /* 极短 1 us 闹钟回归验证：确保底层定时器 CAR >= 1 不发生停表挂死 */
    uint32_t us1_sleep_ok = 0U;
    for (uint32_t i = 0U; i < 50U; i++)
    {
        if (ek_os_thread_sleep(1) == 0) us1_sleep_ok++;
    }
    CONF_CHECK(us1_sleep_ok == 50U, "Tickless 1us sleep x50 never hangs");

    uint32_t us1_sem_to = 0U;
    for (uint32_t i = 0U; i < 20U; i++)
    {
        if (ek_os_sem_take(to_sem, 1) == -ETIMEDOUT) us1_sem_to++;
    }
    CONF_CHECK(us1_sem_to == 20U, "Tickless 1us sem_take x20 returns -ETIMEDOUT");

    ek_os_sem_destroy(to_sem);
    ek_os_mutex_destroy(to_mtx);
    ek_os_condvar_destroy(to_cv);
    ek_os_barrier_destroy(to_bar);
    ek_os_msgq_destroy(to_mq);
    ek_os_kfifo_destroy(to_fifo);
}

/* ========================================================================
 * 组 5：NuttX ostest / Zephyr 风格异步高频硬件 ISR 并发竞态压测（TIMER3）
 * ======================================================================== */

#    define STRESS_KFIFO_POOL_CNT (16U)

static ek_os_sem_t *s_st_sem;
static ek_os_msgq_t *s_st_mq;
static ek_os_kfifo_t *s_st_fifo;
static ek_os_kfifo_t *s_st_free_fifo;
static ek_os_condvar_t *s_st_cv;
static ek_os_mutex_t *s_st_cv_mtx;

static conf_kfifo_item_t s_st_pool[STRESS_KFIFO_POOL_CNT];
static volatile bool s_st_stop;
static volatile uint32_t s_st_lcg = 0x13579BDFUL;
static volatile uint32_t s_st_isr_ticks;
static volatile uint32_t s_st_isr_err;
static volatile uint32_t s_st_sem_given;
static volatile uint32_t s_st_sem_taken;
static volatile uint32_t s_st_msg_sent;
static volatile uint32_t s_st_msg_recvd;
static volatile uint32_t s_st_msg_order_err;
static volatile uint32_t s_st_fifo_put;
static volatile uint32_t s_st_fifo_got;
static volatile uint32_t s_st_fifo_order_err;
static volatile uint32_t s_st_cv_wakes;

static void _stress_timer3_isr(void)
{
    if (!timer_interrupt_flag_get(TIMER3, TIMER_INT_FLAG_UP)) return;
    timer_interrupt_flag_clear(TIMER3, TIMER_INT_FLAG_UP);

    uint32_t x = s_st_lcg * 1664525UL + 1013904223UL;
    s_st_lcg = x;
    uint16_t period = (uint16_t)(200U + ((x >> 16) % 250U));
    timer_autoreload_value_config(TIMER3, period);

    s_st_isr_ticks++;

    if (ek_os_sem_give(s_st_sem) == 0)
    {
        s_st_sem_given++;
    }
    else
    {
        s_st_isr_err++;
    }

    uint32_t next_msg = s_st_msg_sent;
    int mret = ek_os_msgq_send(s_st_mq, &next_msg, EK_OS_FOREVER);
    if (mret == 0)
    {
        s_st_msg_sent++;
    }
    else if (mret != -EAGAIN)
    {
        s_st_isr_err++;
    }

    void *free_node = NULL;
    if (ek_os_kfifo_get(s_st_free_fifo, &free_node, 0) == 0 && free_node)
    {
        conf_kfifo_item_t *item = (conf_kfifo_item_t *)free_node;
        item->seq = s_st_fifo_put;
        if (ek_os_kfifo_put(s_st_fifo, item) == 0)
        {
            s_st_fifo_put++;
        }
        else
        {
            s_st_isr_err++;
        }
    }

    if ((s_st_isr_ticks & 3U) == 0U)
    {
        if (ek_os_condvar_signal(s_st_cv) != 0) s_st_isr_err++;
    }
}

EK_DEFINE_ARCH_IRQ(TIMER3_IRQn, _stress_timer3_isr);

static void *_stress_sem_worker(void *arg)
{
    (void)arg;
    while (!s_st_stop)
    {
        if (ek_os_sem_take(s_st_sem, EK_OS_MSEC(2)) == 0)
        {
            s_st_sem_taken++;
            if ((s_st_sem_taken & 15U) == 0U) ek_os_thread_yield();
        }
    }
    while (ek_os_sem_take(s_st_sem, 0) == 0)
    {
        s_st_sem_taken++;
    }
    return NULL;
}

static void *_stress_msgq_worker(void *arg)
{
    (void)arg;
    uint32_t expected = 0U;
    uint32_t val = 0U;

    while (!s_st_stop)
    {
        if (ek_os_msgq_recv(s_st_mq, &val, EK_OS_MSEC(2)) == 0)
        {
            if (val != expected) s_st_msg_order_err++;
            expected++;
            s_st_msg_recvd++;
            if ((s_st_msg_recvd & 7U) == 0U)
            {
                ek_os_kernel_sched_lock();
                ek_os_kernel_sched_unlock();
            }
        }
    }
    while (ek_os_msgq_recv(s_st_mq, &val, 0) == 0)
    {
        if (val != expected) s_st_msg_order_err++;
        expected++;
        s_st_msg_recvd++;
    }
    return NULL;
}

static void *_stress_kfifo_worker(void *arg)
{
    (void)arg;
    uint32_t expected = 0U;
    void *p = NULL;

    while (!s_st_stop)
    {
        if (ek_os_kfifo_get(s_st_fifo, &p, EK_OS_MSEC(2)) == 0 && p)
        {
            conf_kfifo_item_t *item = (conf_kfifo_item_t *)p;
            if (item->seq != expected) s_st_fifo_order_err++;
            expected++;
            s_st_fifo_got++;
            ek_os_kfifo_put(s_st_free_fifo, item);
        }
    }
    while (ek_os_kfifo_get(s_st_fifo, &p, 0) == 0 && p)
    {
        conf_kfifo_item_t *item = (conf_kfifo_item_t *)p;
        if (item->seq != expected) s_st_fifo_order_err++;
        expected++;
        s_st_fifo_got++;
        ek_os_kfifo_put(s_st_free_fifo, item);
    }
    return NULL;
}

static void *_stress_cv_worker(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_st_cv_mtx, EK_OS_FOREVER);
    while (!s_st_stop)
    {
        if (ek_os_condvar_wait(s_st_cv, s_st_cv_mtx, EK_OS_MSEC(5)) == 0)
        {
            s_st_cv_wakes++;
        }
    }
    ek_os_mutex_unlock(s_st_cv_mtx);
    return NULL;
}

static void _test_hw_isr_stress(void)
{
    s_st_sem = ek_os_sem_create(0);
    s_st_mq = ek_os_msgq_create(sizeof(uint32_t), 8);
    s_st_fifo = ek_os_kfifo_create();
    s_st_free_fifo = ek_os_kfifo_create();
    s_st_cv = ek_os_condvar_create();
    s_st_cv_mtx = ek_os_mutex_create();

    for (uint32_t i = 0U; i < STRESS_KFIFO_POOL_CNT; i++)
    {
        s_st_pool[i].seq = 0U;
        ek_os_kfifo_put(s_st_free_fifo, &s_st_pool[i]);
    }

    s_st_stop = false;
    s_st_isr_ticks = 0U;
    s_st_isr_err = 0U;
    s_st_sem_given = 0U;
    s_st_sem_taken = 0U;
    s_st_msg_sent = 0U;
    s_st_msg_recvd = 0U;
    s_st_msg_order_err = 0U;
    s_st_fifo_put = 0U;
    s_st_fifo_got = 0U;
    s_st_fifo_order_err = 0U;
    s_st_cv_wakes = 0U;

    int t_sem = ek_os_thread_create("st_sem", _stress_sem_worker, NULL, CONF_PRIO_H, CONF_STACK);
    int t_mq = ek_os_thread_create("st_mq", _stress_msgq_worker, NULL, CONF_PRIO_M, CONF_STACK);
    int t_fifo = ek_os_thread_create("st_fifo", _stress_kfifo_worker, NULL, CONF_PRIO_M, CONF_STACK);
    int t_cv = ek_os_thread_create("st_cv", _stress_cv_worker, NULL, CONF_PRIO_L, CONF_STACK);

    /* 启动 TIMER3：APB1*2 = SystemCoreClock / 2 = 120 MHz，分频至 1 MHz */
    uint32_t core_hz = SystemCoreClock ? SystemCoreClock : 240000000U;
    uint32_t psc = (core_hz / 2U) / 1000000U;
    if (psc == 0U) psc = 1U;

    rcu_periph_clock_enable(RCU_TIMER3);
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);
    timer_disable(TIMER3);
    timer_parameter_struct cfg = {
        .prescaler = (uint16_t)(psc - 1U),
        .alignedmode = TIMER_COUNTER_EDGE,
        .counterdirection = TIMER_COUNTER_UP,
        .clockdivision = TIMER_CKDIV_DIV1,
        .period = 299U,
        .repetitioncounter = 0U,
    };
    timer_init(TIMER3, &cfg);
    timer_interrupt_flag_clear(TIMER3, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(TIMER3, TIMER_INT_UP);
    nvic_irq_enable(TIMER3_IRQn, 0, 2);
    timer_enable(TIMER3);

    ek_os_thread_sleep(EK_OS_MSEC(500));

    timer_disable(TIMER3);
    timer_interrupt_disable(TIMER3, TIMER_INT_UP);
    nvic_irq_disable(TIMER3_IRQn);
    timer_interrupt_flag_clear(TIMER3, TIMER_INT_FLAG_UP);

    s_st_stop = true;
    ek_os_condvar_broadcast(s_st_cv);

    CONF_CHECK(ek_os_thread_join(t_sem, EK_OS_MSEC(100), NULL) == 0 &&
                   ek_os_thread_join(t_mq, EK_OS_MSEC(100), NULL) == 0 &&
                   ek_os_thread_join(t_fifo, EK_OS_MSEC(100), NULL) == 0 &&
                   ek_os_thread_join(t_cv, EK_OS_MSEC(100), NULL) == 0,
               "HW ISR stress: all workers joined cleanly");

    uint32_t free_back = 0U;
    void *back = NULL;
    while (ek_os_kfifo_get(s_st_free_fifo, &back, 0) == 0)
    {
        free_back++;
    }

    EK_LOG_INFO("stress stats: isr=%lu sem=%lu/%lu msg=%lu/%lu fifo=%lu/%lu cv=%lu",
                (unsigned long)s_st_isr_ticks,
                (unsigned long)s_st_sem_taken,
                (unsigned long)s_st_sem_given,
                (unsigned long)s_st_msg_recvd,
                (unsigned long)s_st_msg_sent,
                (unsigned long)s_st_fifo_got,
                (unsigned long)s_st_fifo_put,
                (unsigned long)s_st_cv_wakes);

    CONF_CHECK(s_st_isr_ticks >= 800U && s_st_isr_err == 0U, "HW ISR stress: >=800 interrupts, 0 ISR errors");
    CONF_CHECK(s_st_sem_taken == s_st_sem_given, "HW ISR stress: sem consumed == given");
    CONF_CHECK(s_st_msg_recvd == s_st_msg_sent && s_st_msg_order_err == 0U,
               "HW ISR stress: msgq sequence lossless and ordered");
    CONF_CHECK(s_st_fifo_got == s_st_fifo_put && s_st_fifo_order_err == 0U && free_back == STRESS_KFIFO_POOL_CNT,
               "HW ISR stress: kfifo zero-copy pool lossless and ordered");
    CONF_CHECK(s_st_cv_wakes > 0U, "HW ISR stress: condvar woken by ISR");

    ek_os_sem_destroy(s_st_sem);
    ek_os_msgq_destroy(s_st_mq);
    ek_os_kfifo_destroy(s_st_fifo);
    ek_os_kfifo_destroy(s_st_free_fifo);
    ek_os_condvar_destroy(s_st_cv);
    ek_os_mutex_destroy(s_st_cv_mtx);
}

/* ========================================================================
 * 组 6：破坏性销毁（Active Waiter Destroy）全覆盖
 * ======================================================================== */

static union
{
    ek_os_sem_t align;
    uint8_t buf[EK_OS_SEM_STATIC_BUF_SIZE];
} s_act_sem_buf;

static union
{
    ek_os_barrier_t align;
    uint8_t buf[EK_OS_BARRIER_STATIC_BUF_SIZE];
} s_act_bar_buf;

static union
{
    ek_os_kfifo_t align;
    uint8_t buf[EK_OS_KFIFO_STATIC_BUF_SIZE];
} s_act_fifo_buf;

static ek_os_sem_t *s_act_sem;
static ek_os_msgq_t *s_act_mq_rx;
static ek_os_msgq_t *s_act_mq_tx;
static ek_os_condvar_t *s_act_cv;
static ek_os_mutex_t *s_act_cv_mtx;
static ek_os_barrier_t *s_act_bar;
static ek_os_kfifo_t *s_act_fifo;

static void *_act_sem_w(void *arg)
{
    (void)arg;
    return (void *)(intptr_t)ek_os_sem_take(s_act_sem, EK_OS_FOREVER);
}

static void *_act_mq_rx_w(void *arg)
{
    (void)arg;
    uint32_t v = 0U;
    return (void *)(intptr_t)ek_os_msgq_recv(s_act_mq_rx, &v, EK_OS_FOREVER);
}

static void *_act_mq_tx_w(void *arg)
{
    (void)arg;
    uint32_t v = 1U;
    return (void *)(intptr_t)ek_os_msgq_send(s_act_mq_tx, &v, EK_OS_FOREVER);
}

static void *_act_cv_w(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_act_cv_mtx, EK_OS_FOREVER);
    int err = ek_os_condvar_wait(s_act_cv, s_act_cv_mtx, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_act_cv_mtx);
    return (void *)(intptr_t)err;
}

static void *_act_bar_w(void *arg)
{
    (void)arg;
    return (void *)(intptr_t)ek_os_barrier_wait(s_act_bar, EK_OS_FOREVER);
}

static void *_act_fifo_w(void *arg)
{
    (void)arg;
    void *p = NULL;
    return (void *)(intptr_t)ek_os_kfifo_get(s_act_fifo, &p, EK_OS_FOREVER);
}

static void _test_active_destroy(void)
{
    void *ret = NULL;

    /* 静态与动态 sem_destroy */
    s_act_sem = ek_os_sem_create_static(0, s_act_sem_buf.buf, sizeof(s_act_sem_buf.buf));
    int tid = ek_os_thread_create("ad_ssem", _act_sem_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_sem_destroy(s_act_sem);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy static sem wakes waiter with -ECANCELED");
    CONF_CHECK(ek_os_sem_take(s_act_sem, 0) == -ENOENT, "Destroyed static sem rejects further access");

    s_act_sem = ek_os_sem_create(0);
    tid = ek_os_thread_create("ad_dsem", _act_sem_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_sem_destroy(s_act_sem);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy dynamic sem wakes waiter with -ECANCELED");

    /* msgq 接收阻塞与发送阻塞 destroy */
    s_act_mq_rx = ek_os_msgq_create(sizeof(uint32_t), 1);
    s_act_mq_tx = ek_os_msgq_create(sizeof(uint32_t), 1);
    uint32_t fill = 0x55U;
    ek_os_msgq_send(s_act_mq_tx, &fill, 0);
    int tr = ek_os_thread_create("ad_mqr", _act_mq_rx_w, NULL, CONF_PRIO_M, CONF_STACK);
    int ts = ek_os_thread_create("ad_mqt", _act_mq_tx_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_msgq_destroy(s_act_mq_rx);
    ek_os_msgq_destroy(s_act_mq_tx);
    CONF_CHECK(ek_os_thread_join(tr, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy msgq wakes blocked receiver with -ECANCELED");
    CONF_CHECK(ek_os_thread_join(ts, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy msgq wakes blocked sender with -ECANCELED");

    /* condvar_destroy */
    s_act_cv = ek_os_condvar_create();
    s_act_cv_mtx = ek_os_mutex_create();
    tid = ek_os_thread_create("ad_cv", _act_cv_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_condvar_destroy(s_act_cv);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy condvar wakes waiter with -ECANCELED");
    ek_os_mutex_destroy(s_act_cv_mtx);

    /* 静态与动态 barrier_destroy */
    s_act_bar = ek_os_barrier_create_static(s_act_bar_buf.buf, sizeof(s_act_bar_buf.buf), 2);
    tid = ek_os_thread_create("ad_sbar", _act_bar_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_barrier_destroy(s_act_bar);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy static barrier wakes waiter with -ECANCELED");
    CONF_CHECK(ek_os_barrier_wait(s_act_bar, 0) == -ENOENT, "Destroyed static barrier rejects access");

    s_act_bar = ek_os_barrier_create(2);
    tid = ek_os_thread_create("ad_dbar", _act_bar_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_barrier_destroy(s_act_bar);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy dynamic barrier wakes waiter with -ECANCELED");

    /* 静态与动态 kfifo_destroy */
    s_act_fifo = ek_os_kfifo_create_static(s_act_fifo_buf.buf, sizeof(s_act_fifo_buf.buf));
    tid = ek_os_thread_create("ad_sfifo", _act_fifo_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_kfifo_destroy(s_act_fifo);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy static kfifo wakes waiter with -ECANCELED");
    void *dummy = NULL;
    CONF_CHECK(ek_os_kfifo_get(s_act_fifo, &dummy, 0) == -ENOENT, "Destroyed static kfifo rejects access");

    s_act_fifo = ek_os_kfifo_create();
    tid = ek_os_thread_create("ad_dfifo", _act_fifo_w, NULL, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(2000);
    ek_os_kfifo_destroy(s_act_fifo);
    CONF_CHECK(ek_os_thread_join(tid, EK_OS_MSEC(50), &ret) == 0 && (int)(intptr_t)ret == -ECANCELED,
               "Destroy dynamic kfifo wakes waiter with -ECANCELED");
}

/* ========================================================================
 * 组 7：动态生命周期循环压力与 TLSF 零内存泄漏校验
 * ======================================================================== */

static void *_leak_fast_worker(void *arg)
{
    return arg;
}

static void *_leak_sem_worker(void *arg)
{
    ek_os_sem_t *sem = (ek_os_sem_t *)arg;
    ek_os_sem_take(sem, EK_OS_FOREVER);
    return (void *)(intptr_t)99;
}

static void _run_lifecycle_round(void)
{
    ek_os_sem_t *s = ek_os_sem_create(0);
    ek_os_mutex_t *m = ek_os_mutex_create();
    ek_os_msgq_t *q = ek_os_msgq_create(16U, 4U);
    ek_os_condvar_t *c = ek_os_condvar_create();
    ek_os_barrier_t *b = ek_os_barrier_create(1U);
    ek_os_kfifo_t *f = ek_os_kfifo_create();

    int t1 = ek_os_thread_create("lk_fast", _leak_fast_worker, (void *)(intptr_t)7, CONF_PRIO_M, CONF_STACK);
    int t2 = ek_os_thread_create("lk_sem", _leak_sem_worker, s, CONF_PRIO_M, CONF_STACK);
    ek_os_thread_sleep(500);

    ek_os_mutex_lock(m, 0);
    uint8_t msg[16] = { 1U };
    ek_os_msgq_send(q, msg, 0);
    ek_os_msgq_recv(q, msg, 0);
    ek_os_condvar_signal(c);
    ek_os_barrier_wait(b, 0);
    conf_kfifo_item_t node = { .seq = 1U };
    ek_os_kfifo_put(f, &node);
    void *out = NULL;
    ek_os_kfifo_get(f, &out, 0);
    ek_os_mutex_unlock(m);

    ek_os_sem_give(s);
    ek_os_thread_join(t1, EK_OS_MSEC(50), NULL);
    ek_os_thread_join(t2, EK_OS_MSEC(50), NULL);

    ek_os_sem_destroy(s);
    ek_os_mutex_destroy(m);
    ek_os_msgq_destroy(q);
    ek_os_condvar_destroy(c);
    ek_os_barrier_destroy(b);
    ek_os_kfifo_destroy(f);
}

static void _test_heap_leak_stress(void)
{
    _run_lifecycle_round();
    ek_os_thread_sleep(2000);

    size_t used0 = ek_heap_used();
    size_t unused0 = ek_heap_unused();

    for (uint32_t i = 0U; i < 30U; i++)
    {
        _run_lifecycle_round();
    }

    size_t used1 = ek_heap_used();
    size_t unused1 = ek_heap_unused();
    EK_LOG_INFO("heap baseline: used=%zu unused=%zu | after 30 rounds: used=%zu unused=%zu",
                used0,
                unused0,
                used1,
                unused1);
    CONF_CHECK(used1 == used0 && unused1 == unused0, "TLSF heap: 0 bytes leaked across 30 full lifecycle rounds");
}

int os_conformance_test(void)
{
    s_total = 0U;
    s_fail = 0U;

    EK_LOG_INFO("==== os_conformance_test started ====");
    _test_isr_conformance();
    _test_pi_advanced();
    _test_condvar_morph_edge();
    _test_tickless_timeouts();
    _test_hw_isr_stress();
    _test_active_destroy();
    _test_heap_leak_stress();

    uint32_t passed = s_total - s_fail;
    EK_LOG_INFO("conformance_test finished: %s (%lu/%lu passed)",
                s_fail ? "FAILED" : "ALL PASS",
                (unsigned long)passed,
                (unsigned long)s_total);
    return (int)s_fail;
}

#else

int os_conformance_test(void)
{
    return 0;
}

#endif /* EKCFG_RTOS == 1 && !EK_RUN_THREAD_METRIC && !EK_RUN_LATENCY_MEASURE */
