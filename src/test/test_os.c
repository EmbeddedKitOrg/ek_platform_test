/**
 * @file test_os.c
 * @brief RTOS 内核自检：sleep 精度、信号量阻塞唤醒、优先级抢占、调度器锁
 */

#include "test.h"

#if EKCFG_RTOS == 1

#    include "ek/os/kernel.h"
#    include "ek/os/sem.h"
#    include "ek/os/thread.h"

EK_LOG_MODULE("test_os", EK_LOG_LEVEL_DEBUG);

#    define PRIO_HIGH  (3U)
#    define PRIO_MID   (5U)
#    define PRIO_LOW   (10U)

#    define TEST_STACK (768U)

static ek_os_sem_t *s_sem;

static volatile int s_wake_err = -1;
static volatile bool s_high_ran;
static volatile bool s_done;

#    define OS_CHECK(cond, msg)                  \
        do                                       \
        {                                        \
            if (cond)                            \
            {                                    \
                EK_LOG_INFO("PASS: %s", (msg));  \
            }                                    \
            else                                 \
            {                                    \
                EK_LOG_ERROR("FAIL: %s", (msg)); \
                s_done = true;                   \
            }                                    \
        } while (0)

/* 阻塞在信号量上的等待者 */
static void _sem_waiter(void *arg)
{
    (void)arg;
    s_wake_err = ek_os_sem_take(s_sem, EK_OS_FOREVER);
    for (;;) ek_os_thread_sleep(1000);
}

/* 只置一次标志的高优先级线程 */
static void _high_thread(void *arg)
{
    (void)arg;
    s_high_ran = true;
    for (;;) ek_os_thread_sleep(1000);
}

static void _low_thread(void *arg)
{
    (void)arg;
    /* 创建更高优先级线程，期望它立刻抢占本线程 */
    ek_os_thread_create("os_high", _high_thread, NULL, PRIO_HIGH, TEST_STACK);
    for (;;) ek_os_thread_sleep(1000);
}

static void _test_main(void *arg)
{
    (void)arg;

    EK_LOG_INFO("os_main started");

    /* ---- 0. DWT 时间源必须递增 ---- */
    uint64_t t0 = ek_os_kernel_time();
    for (volatile int i = 0; i < 10000; i++)
    {
    }
    uint64_t t1 = ek_os_kernel_time();
    EK_LOG_INFO("clock probe: %llu -> %llu us", (unsigned long long)t0, (unsigned long long)t1);
    OS_CHECK(t1 > t0, "time source advances");

    /* ---- 1. sleep 精度 ---- */
    t0 = ek_os_kernel_time();
    int err = ek_os_thread_sleep(5000);
    t1 = ek_os_kernel_time();
    EK_LOG_INFO("sleep(5000 us) -> err=%d elapsed=%llu us", err, (unsigned long long)(t1 - t0));
    OS_CHECK(err == 0, "sleep returns 0");
    OS_CHECK((t1 - t0) >= 4000ULL, "sleep lasts >= 4 ms");

    /* ---- 2. 信号量：阻塞 + give 唤醒 ---- */
    s_sem = ek_os_sem_create(0);
    OS_CHECK(s_sem != NULL, "sem create");
    if (!s_sem)
    {
        s_done = true;
        for (;;) ek_os_thread_sleep(1000);
    }

    ek_os_thread_create("os_waiter", _sem_waiter, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(2000); /* 让 waiter 进入阻塞 */
    EK_LOG_INFO("waiter err before give = %d", s_wake_err);
    OS_CHECK(s_wake_err == -1, "waiter blocked on sem");

    OS_CHECK(ek_os_sem_give(s_sem) == 0, "sem give ok");
    ek_os_thread_sleep(2000);
    EK_LOG_INFO("waiter err after give = %d", s_wake_err);
    OS_CHECK(s_wake_err == 0, "waiter woken by give");

    /* ---- 3. 优先级抢占 ---- */
    s_high_ran = false;
    ek_os_thread_create("os_low", _low_thread, NULL, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(2000);
    OS_CHECK(s_high_ran, "higher prio thread preempts");

    /* ---- 4. 调度器锁阻止抢占 ---- */
    s_high_ran = false;
    OS_CHECK(ek_os_kernel_sched_lock() >= 1, "sched lock");
    ek_os_thread_create("os_high2", _high_thread, NULL, PRIO_HIGH, TEST_STACK);
    OS_CHECK(!s_high_ran, "sched lock blocks preemption");
    OS_CHECK(ek_os_kernel_sched_unlock() == 0, "sched unlock");
    ek_os_thread_sleep(2000);
    OS_CHECK(s_high_ran, "unlock allows preemption");

    EK_LOG_INFO("os_test finished: %s", s_done ? "FAILED" : "ALL PASS");
    s_done = true;

    for (;;)
    {
        ek_os_thread_sleep(1000);
    }
}

void os_test(void)
{
    int tid = ek_os_thread_create("os_main", _test_main, NULL, PRIO_MID, 1024);
    EK_LOG_INFO("os_test: create os_main tid=%d", tid);
}

#else

void os_test(void)
{
}

#endif /* EKCFG_RTOS == 1 */
