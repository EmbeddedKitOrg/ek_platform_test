/**
 * @file test_os.c
 * @brief RTOS 内核自检：sleep 精度、信号量、互斥量、消息队列、条件变量、屏障、侵入式 FIFO、优先级抢占、调度器锁
 */

#include "test.h"

#if EKCFG_RTOS == 1

#    include "ek/os/barrier.h"
#    include "ek/os/condvar.h"
#    include "ek/os/kernel.h"
#    include "ek/os/kfifo.h"
#    include "ek/os/msgq.h"
#    include "ek/os/mutex.h"
#    include "ek/os/sem.h"
#    include "ek/os/thread.h"

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_DEBUG);

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

#    define PRIO_TEST  (2U)
#    define PRIO_HIGH  (3U)
#    define PRIO_MID   (5U)
#    define PRIO_LOW   (10U)

#    define TEST_STACK (768U)

static ek_os_sem_t *s_sem;

static volatile int s_wake_err = -1;
static volatile bool s_high_ran;
static volatile bool s_done;

static ek_os_mutex_t *s_mtx;
static ek_os_mutex_t *s_mtx_a;
static ek_os_mutex_t *s_mtx_b;

static volatile bool s_mtx_locked;
static volatile int s_mtx_waiter_err;
static volatile bool s_inh_mid_ran;
static volatile bool s_inh_mid_ran_at_unlock;
static volatile uint8_t s_inh_low_prio_after;

static union
{
    ek_os_mutex_t align;
    uint8_t buf[EK_OS_MUTEX_STATIC_BUF_SIZE];
} s_static_mtx_mem;

static ek_os_condvar_t *s_cv;
static ek_os_mutex_t *s_cv_mtx;
static volatile bool s_cv_ready;
static volatile int s_cv_wake_cnt;
static volatile int s_cv_order[4];
static volatile int s_cv_wait_err[4];
static volatile int s_cv_unlock_err[4];

static union
{
    ek_os_condvar_t align;
    uint8_t buf[EK_OS_CONDVAR_STATIC_BUF_SIZE];
} s_static_cv_mem;

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
static void *_sem_waiter(void *arg)
{
    (void)arg;
    s_wake_err = ek_os_sem_take(s_sem, EK_OS_FOREVER);
    return (void *)(intptr_t)42;
}

/* 只置一次标志的高优先级线程 */
static void *_high_thread(void *arg)
{
    (void)arg;
    s_high_ran = true;
    return NULL;
}

static void *_low_thread(void *arg)
{
    (void)arg;
    /* 创建更高优先级线程，期望它立刻抢占本线程 */
    int high_tid = ek_os_thread_create("os_high", _high_thread, NULL, PRIO_HIGH, TEST_STACK);
    ek_os_thread_join(high_tid, -1, NULL);
    return NULL;
}

/* 持锁 20 ms，供其它线程验证 trylock 竞争 */
static void *_mtx_holder(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    s_mtx_locked = true;
    ek_os_thread_sleep(20000);
    ek_os_mutex_unlock(s_mtx);
    s_mtx_locked = false;
    return NULL;
}

/* 尝试拿锁并把结果写入 s_mtx_waiter_err；未返回前该值为哨兵 1 */
static void *_mtx_waiter(void *arg)
{
    (void)arg;
    int err = ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    s_mtx_waiter_err = err;
    if (err == 0) ek_os_mutex_unlock(s_mtx);
    return NULL;
}

/* 持锁后故意不走 unlock 直接退出，验证内核回收锁 */
static void *_mtx_exit_holder(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    ek_os_thread_sleep(20000);
    return NULL;
}

/* 优先级继承：持锁忙等 60 ms，期间高优先级线程抢锁把本线程提权 */
static void *_inh_low(void *arg)
{
    (void)arg;
    int self = ek_os_kernel_current_tid();

    ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);

    /* 忙等而非睡眠：睡眠会把本线程移出就绪队列，MID 仍可拿到 CPU，测不出继承 */
    uint64_t deadline = ek_os_kernel_time() + 60000ULL;
    while (ek_os_kernel_time() < deadline)
    {
    }

    /* 若继承生效，本线程此刻有效优先级为 PRIO_HIGH，MID 抢不到 CPU */
    s_inh_mid_ran_at_unlock = s_inh_mid_ran;
    ek_os_mutex_unlock(s_mtx);

    uint8_t prio = 0;
    ek_os_thread_get_prio(self, &prio);
    s_inh_low_prio_after = prio;
    return NULL;
}

/* 优先级继承：优先级介于 HIGH 与 LOW 之间，继承失效时会抢占 LOW */
static void *_inh_mid(void *arg)
{
    (void)arg;
    ek_os_thread_sleep(35000);
    s_inh_mid_ran = true;
    return NULL;
}

/* 优先级继承：抢锁并阻塞，触发持有者提权 */
static void *_inh_high(void *arg)
{
    (void)arg;
    int err = ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    if (err == 0) ek_os_mutex_unlock(s_mtx);
    return NULL;
}

/* 链式继承：持 s_mtx_a 睡觉 */
static void *_chain_low(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx_a, EK_OS_FOREVER);
    ek_os_thread_sleep(80000);
    ek_os_mutex_unlock(s_mtx_a);
    return NULL;
}

/* 链式继承：先持 s_mtx_b，再等 s_mtx_a，从而把 HIGH 的优先级传给 LOW */
static void *_chain_mid(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx_b, EK_OS_FOREVER);
    ek_os_mutex_lock(s_mtx_a, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_mtx_a);
    ek_os_mutex_unlock(s_mtx_b);
    return NULL;
}

/* 链式继承：等 s_mtx_b，触发 MID 提权并沿等待链回溯到 LOW */
static void *_chain_high(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx_b, EK_OS_FOREVER);
    ek_os_mutex_unlock(s_mtx_b);
    return NULL;
}

static volatile int s_susp_waiter_err;

/* 挂起等待者时持有者继承优先级回落测试 */
static void *_susp_holder(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    ek_os_thread_sleep(40000);
    ek_os_mutex_unlock(s_mtx);
    return NULL;
}

static void *_susp_waiter(void *arg)
{
    (void)arg;
    s_susp_waiter_err = ek_os_mutex_lock(s_mtx, EK_OS_FOREVER);
    if (s_susp_waiter_err == 0) ek_os_mutex_unlock(s_mtx);
    return NULL;
}

/* 互斥量用例：基础加锁、重入、跨线程竞争、优先级继承、销毁、持锁退出、静态创建 */
static void _mutex_test(void)
{
    /* ---- 5. 参数边界 ---- */
    OS_CHECK(ek_os_mutex_lock(NULL, 0) == -ENOENT, "lock(NULL) returns -ENOENT");
    OS_CHECK(ek_os_mutex_unlock(NULL) == -ENOENT, "unlock(NULL) returns -ENOENT");

    s_mtx = ek_os_mutex_create();
    OS_CHECK(s_mtx != NULL, "mutex create");
    if (!s_mtx)
    {
        s_done = true;
        return;
    }

    /* ---- 6. 基础加锁与重入 ---- */
    OS_CHECK(ek_os_mutex_unlock(s_mtx) == -EPERM, "unlock free mutex returns -EPERM");
    OS_CHECK(ek_os_mutex_lock(s_mtx, 0) == 0, "lock on free mutex");
    OS_CHECK(ek_os_mutex_lock(s_mtx, 0) == 0, "relock is reentrant");
    OS_CHECK(ek_os_mutex_unlock(s_mtx) == 0, "unlock inner level");
    OS_CHECK(ek_os_mutex_unlock(s_mtx) == 0, "unlock outer level");
    OS_CHECK(ek_os_mutex_trylock(s_mtx) == 0, "trylock on free mutex");
    OS_CHECK(ek_os_mutex_trylock(s_mtx) == 0, "trylock is reentrant");
    OS_CHECK(ek_os_mutex_unlock(s_mtx) == 0, "unlock trylock inner");
    OS_CHECK(ek_os_mutex_unlock(s_mtx) == 0, "unlock trylock outer");

    /* ---- 7. 跨线程竞争：trylock / 非阻塞 lock 返回 -EAGAIN ---- */
    s_mtx_locked = false;
    int holder_tid = ek_os_thread_create("mtx_holder", _mtx_holder, NULL, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_mtx_locked, "holder acquired mutex");
    OS_CHECK(ek_os_mutex_trylock(s_mtx) == -EAGAIN, "trylock on held mutex returns -EAGAIN");
    OS_CHECK(ek_os_mutex_lock(s_mtx, 0) == -EAGAIN, "non-blocking lock on held returns -EAGAIN");
    OS_CHECK(ek_os_thread_join(holder_tid, -1, NULL) == 0, "join mutex holder");
    OS_CHECK(!s_mtx_locked, "holder released mutex");

    /* ---- 8. 优先级继承：等待者优先级传给持有者，释锁后回落 ---- */
    s_inh_mid_ran = false;
    s_inh_mid_ran_at_unlock = false;
    s_inh_low_prio_after = 0;

    int inh_low_tid = ek_os_thread_create("inh_low", _inh_low, NULL, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(5000);
    int inh_mid_tid = ek_os_thread_create("inh_mid", _inh_mid, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(2000);
    int inh_high_tid = ek_os_thread_create("inh_high", _inh_high, NULL, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(2000);

    uint8_t low_prio = 0;
    OS_CHECK(ek_os_thread_get_prio(inh_low_tid, &low_prio) == 0, "query owner priority");
    OS_CHECK(low_prio == PRIO_HIGH, "owner inherits waiter priority");

    OS_CHECK(ek_os_thread_join(inh_high_tid, -1, NULL) == 0, "join inheriting waiter");
    OS_CHECK(!s_inh_mid_ran_at_unlock, "inheritance keeps mid priority thread out");
    OS_CHECK(ek_os_thread_join(inh_low_tid, -1, NULL) == 0, "join inherited owner");
    OS_CHECK(s_inh_low_prio_after == PRIO_LOW, "owner priority restored after unlock");
    OS_CHECK(ek_os_thread_join(inh_mid_tid, -1, NULL) == 0, "join mid thread");

    ek_os_mutex_destroy(s_mtx);
    s_mtx = NULL;

    /* ---- 9. destroy 唤醒等待者返回 -ECANCELED ---- */
    s_mtx = ek_os_mutex_create();
    OS_CHECK(s_mtx != NULL, "mutex create for destroy");
    s_mtx_waiter_err = 1;
    if (s_mtx)
    {
        OS_CHECK(ek_os_mutex_lock(s_mtx, 0) == 0, "test thread holds mutex");
        int cancel_tid = ek_os_thread_create("mtx_cancel", _mtx_waiter, NULL, PRIO_LOW, TEST_STACK);
        ek_os_thread_sleep(5000);
        OS_CHECK(s_mtx_waiter_err == 1, "waiter still blocked on mutex");

        ek_os_mutex_destroy(s_mtx);
        s_mtx = NULL;

        OS_CHECK(ek_os_thread_join(cancel_tid, -1, NULL) == 0, "join cancelled waiter");
        OS_CHECK(s_mtx_waiter_err == -ECANCELED, "destroy wakes waiter with -ECANCELED");
    }

    /* ---- 10. 持锁线程退出时由内核清理锁 ---- */
    s_mtx = ek_os_mutex_create();
    OS_CHECK(s_mtx != NULL, "mutex create for owner exit");
    s_mtx_waiter_err = 1;
    if (s_mtx)
    {
        int holder_exit_tid = ek_os_thread_create("mtx_exit_hold", _mtx_exit_holder, NULL, PRIO_LOW, TEST_STACK);
        ek_os_thread_sleep(3000);
        int waiter_exit_tid = ek_os_thread_create("mtx_exit_wait", _mtx_waiter, NULL, PRIO_LOW, TEST_STACK);
        ek_os_thread_sleep(3000);
        OS_CHECK(s_mtx_waiter_err == 1, "waiter blocked on exiting owner");

        OS_CHECK(ek_os_thread_join(holder_exit_tid, -1, NULL) == 0, "join owner that exits holding lock");
        OS_CHECK(ek_os_thread_join(waiter_exit_tid, -1, NULL) == 0, "join waiter released by owner exit");
        OS_CHECK(s_mtx_waiter_err == -ECANCELED, "owner exit wakes waiter with -ECANCELED");

        ek_os_mutex_destroy(s_mtx);
        s_mtx = NULL;
    }

    /* ---- 11. 优先级继承沿等待链传播 ---- */
    s_mtx_a = ek_os_mutex_create();
    s_mtx_b = ek_os_mutex_create();
    OS_CHECK(s_mtx_a != NULL && s_mtx_b != NULL, "mutex create for inheritance chain");
    if (s_mtx_a && s_mtx_b)
    {
        int chain_low_tid = ek_os_thread_create("chain_low", _chain_low, NULL, PRIO_LOW, TEST_STACK);
        ek_os_thread_sleep(5000);
        int chain_mid_tid = ek_os_thread_create("chain_mid", _chain_mid, NULL, PRIO_MID, TEST_STACK);
        ek_os_thread_sleep(5000);

        uint8_t p_low = 0;
        uint8_t p_mid = 0;
        OS_CHECK(ek_os_thread_get_prio(chain_low_tid, &p_low) == 0, "query chain low priority");
        OS_CHECK(p_low == PRIO_MID, "chain: low inherits mid priority");

        int chain_high_tid = ek_os_thread_create("chain_high", _chain_high, NULL, PRIO_HIGH, TEST_STACK);
        ek_os_thread_sleep(5000);

        OS_CHECK(ek_os_thread_get_prio(chain_mid_tid, &p_mid) == 0, "query chain mid priority");
        OS_CHECK(p_mid == PRIO_HIGH, "chain: mid inherits high priority");
        OS_CHECK(ek_os_thread_get_prio(chain_low_tid, &p_low) == 0, "query chain low priority again");
        OS_CHECK(p_low == PRIO_HIGH, "chain: inheritance reaches low owner");

        OS_CHECK(ek_os_thread_join(chain_low_tid, -1, NULL) == 0, "join chain low");
        OS_CHECK(ek_os_thread_join(chain_mid_tid, -1, NULL) == 0, "join chain mid");
        OS_CHECK(ek_os_thread_join(chain_high_tid, -1, NULL) == 0, "join chain high");

        ek_os_mutex_destroy(s_mtx_a);
        ek_os_mutex_destroy(s_mtx_b);
        s_mtx_a = NULL;
        s_mtx_b = NULL;
    }

    /* 挂起等待锁的高优先级线程时，原持锁者的继承优先级应立即回落 */
    s_mtx = ek_os_mutex_create();
    OS_CHECK(s_mtx != NULL, "mutex create for suspend inheritance restore");
    if (s_mtx)
    {
        int susp_low = ek_os_thread_create("susp_low", _susp_holder, NULL, PRIO_LOW, TEST_STACK);
        ek_os_thread_sleep(5000);
        s_susp_waiter_err = 1;
        int susp_high = ek_os_thread_create("susp_high", _susp_waiter, NULL, PRIO_HIGH, TEST_STACK);
        ek_os_thread_sleep(5000);

        uint8_t p = 0;
        OS_CHECK(ek_os_thread_get_prio(susp_low, &p) == 0 && p == PRIO_HIGH,
                 "owner inherits waiter prio before suspend");
        OS_CHECK(ek_os_thread_suspend(susp_high) == 0, "suspend mutex waiter");
        OS_CHECK(ek_os_thread_get_prio(susp_low, &p) == 0 && p == PRIO_LOW,
                 "owner priority restored after waiter suspended");
        OS_CHECK(ek_os_thread_resume(susp_high) == 0, "resume suspended waiter");
        OS_CHECK(ek_os_thread_join(susp_high, -1, NULL) == 0, "join suspended waiter");
        OS_CHECK(s_susp_waiter_err == -EINTR, "suspended waiter returns -EINTR");
        OS_CHECK(ek_os_thread_join(susp_low, -1, NULL) == 0, "join suspend holder");

        ek_os_mutex_destroy(s_mtx);
        s_mtx = NULL;
    }

    /* ---- 12. 静态缓冲区创建与销毁失效 ---- */
    ek_os_mutex_t *st_mtx = ek_os_mutex_create_static(s_static_mtx_mem.buf, sizeof(s_static_mtx_mem.buf));
    OS_CHECK(st_mtx != NULL, "mutex create_static");
    if (st_mtx)
    {
        OS_CHECK(ek_os_mutex_lock(st_mtx, 0) == 0, "lock static mutex");
        OS_CHECK(ek_os_mutex_unlock(st_mtx) == 0, "unlock static mutex");
        ek_os_mutex_destroy(st_mtx);
        OS_CHECK(ek_os_mutex_trylock(st_mtx) == -ENOENT, "destroyed static mutex rejected");
    }

    /* ---- 13. 优先级查询边界 ---- */
    uint8_t dummy_prio = 0;
    OS_CHECK(ek_os_thread_get_prio(ek_os_kernel_current_tid(), NULL) == -EINVAL, "get_prio(NULL) returns -EINVAL");
    OS_CHECK(ek_os_thread_get_prio(9999, &dummy_prio) == -ESRCH, "get_prio(bad tid) returns -ESRCH");
}

/* mutex 用例需要随时抢占被提权到 PRIO_HIGH 的持有者，故跑在更高的优先级上 */
static void *_mutex_main(void *arg)
{
    (void)arg;
    _mutex_test();
    return NULL;
}

/* ---------------- 消息队列自检 ---------------- */

static ek_os_msgq_t *s_q;
static volatile int s_msgq_send_err;
static volatile int s_msgq_recv_err;
static volatile uint32_t s_msgq_recv_val;

/* 往容量为 1 的队列再发一条，会被阻塞到主控取走第一条 */
static void *_msgq_sender(void *arg)
{
    (void)arg;
    uint32_t v = 0x1234U;
    s_msgq_send_err = ek_os_msgq_send(s_q, &v, EK_OS_FOREVER);
    return NULL;
}

/* 从空队列收一条，会被阻塞到主控发送 */
static void *_msgq_receiver(void *arg)
{
    (void)arg;
    uint32_t v = 0;
    s_msgq_recv_err = ek_os_msgq_recv(s_q, &v, EK_OS_FOREVER);
    s_msgq_recv_val = v;
    return NULL;
}

static void _msgq_test(void)
{
    /* ---- 参数校验 ---- */
    OS_CHECK(ek_os_msgq_create(0, 4) == NULL, "msgq create rejects zero item_size");
    OS_CHECK(ek_os_msgq_create(4, 0) == NULL, "msgq create rejects zero depth");
    OS_CHECK(ek_os_msgq_create_static(4, 4, NULL, 64) == NULL, "msgq create_static rejects NULL buf");

    /* ---- 动态创建、空队列与非阻塞语义 ---- */
    s_q = ek_os_msgq_create(sizeof(uint32_t), 4);
    OS_CHECK(s_q != NULL, "msgq create");
    if (!s_q)
    {
        EK_LOG_INFO("msgq_test finished: FAILED");
        s_done = true;
        return;
    }

    uint32_t v = 0;
    OS_CHECK(ek_os_msgq_count(s_q) == 0, "msgq starts empty");
    OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == -EAGAIN, "recv on empty returns -EAGAIN");

    for (uint32_t i = 0; i < 4; i++)
    {
        v = i;
        OS_CHECK(ek_os_msgq_send(s_q, &v, 0) == 0, "send fills queue");
    }
    OS_CHECK(ek_os_msgq_count(s_q) == 4, "msgq count reaches capacity");
    v = 99;
    OS_CHECK(ek_os_msgq_send(s_q, &v, 0) == -EAGAIN, "send on full returns -EAGAIN");

    for (uint32_t i = 0; i < 4; i++)
    {
        v = 0xFFFFFFFFU;
        OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == 0, "recv drains queue");
        OS_CHECK(v == i, "recv is FIFO");
    }
    OS_CHECK(ek_os_msgq_count(s_q) == 0, "msgq empty after drain");
    OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == -EAGAIN, "recv after drain returns -EAGAIN");

    /* ---- 环形复用：读写指针错位后仍按 FIFO 工作 ---- */
    for (uint32_t round = 0; round < 3; round++)
    {
        for (uint32_t i = 0; i < 3; i++)
        {
            v = round * 10U + i;
            OS_CHECK(ek_os_msgq_send(s_q, &v, 0) == 0, "ring: send wraps");
        }
        for (uint32_t i = 0; i < 3; i++)
        {
            v = 0;
            OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == 0, "ring: recv wraps");
            OS_CHECK(v == round * 10U + i, "ring: FIFO across wrap");
        }
        OS_CHECK(ek_os_msgq_count(s_q) == 0, "ring: empty after round");
    }

    ek_os_msgq_destroy(s_q);
    s_q = NULL;

    /* ---- 满队列阻塞发送者，被 recv 唤醒 ---- */
    s_q = ek_os_msgq_create(sizeof(uint32_t), 1);
    OS_CHECK(s_q != NULL, "msgq create depth 1");
    if (!s_q)
    {
        EK_LOG_INFO("msgq_test finished: FAILED");
        s_done = true;
        return;
    }

    v = 7;
    OS_CHECK(ek_os_msgq_send(s_q, &v, 0) == 0, "fill depth-1 queue");

    s_msgq_send_err = -1;
    int sender_tid = ek_os_thread_create("msgq_tx", _msgq_sender, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(20000);
    OS_CHECK(s_msgq_send_err == -1, "sender blocked on full queue");

    v = 0;
    OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == 0, "recv releases blocked sender");
    OS_CHECK(v == 7, "blocked sender kept FIFO order");
    OS_CHECK(ek_os_thread_join(sender_tid, -1, NULL) == 0, "join blocked sender");
    OS_CHECK(s_msgq_send_err == 0, "sender resumed after recv");

    v = 0;
    OS_CHECK(ek_os_msgq_recv(s_q, &v, 0) == 0, "receive sender payload");
    OS_CHECK(v == 0x1234U, "sender payload intact");

    /* ---- 空队列阻塞接收者，被 send 唤醒 ---- */
    s_msgq_recv_err = -1;
    int recv_tid = ek_os_thread_create("msgq_rx", _msgq_receiver, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(20000);
    OS_CHECK(s_msgq_recv_err == -1, "receiver blocked on empty queue");

    v = 0x55AAU;
    OS_CHECK(ek_os_msgq_send(s_q, &v, 0) == 0, "send releases blocked receiver");
    OS_CHECK(ek_os_thread_join(recv_tid, -1, NULL) == 0, "join blocked receiver");
    OS_CHECK(s_msgq_recv_err == 0, "receiver resumed after send");
    OS_CHECK(s_msgq_recv_val == 0x55AAU, "receiver got payload");

    /* ---- destroy 唤醒阻塞等待者 ---- */
    s_msgq_recv_err = -1;
    int wait_tid = ek_os_thread_create("msgq_cancel", _msgq_receiver, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(20000);
    OS_CHECK(s_msgq_recv_err == -1, "waiter blocked before destroy");

    ek_os_msgq_destroy(s_q);
    s_q = NULL;
    OS_CHECK(ek_os_thread_join(wait_tid, -1, NULL) == 0, "join waiter cancelled by destroy");
    OS_CHECK(s_msgq_recv_err == -ECANCELED, "destroy wakes waiter with -ECANCELED");

    /* ---- 静态缓冲区创建 ---- */
    static union
    {
        ek_os_msgq_t align;
        uint8_t buf[EK_OS_MSGQ_STATIC_BUF_SIZE(sizeof(uint32_t), 2)];
    } st_mem;

    ek_os_msgq_t *st_q = ek_os_msgq_create_static(sizeof(uint32_t), 2, st_mem.buf, sizeof(st_mem.buf));
    OS_CHECK(st_q != NULL, "msgq create_static");
    if (st_q)
    {
        v = 0xABCDU;
        OS_CHECK(ek_os_msgq_send(st_q, &v, 0) == 0, "static: send");
        v = 0;
        OS_CHECK(ek_os_msgq_recv(st_q, &v, 0) == 0, "static: recv");
        OS_CHECK(v == 0xABCDU, "static: payload intact");

        ek_os_msgq_destroy(st_q);
        OS_CHECK(ek_os_msgq_count(st_q) == -ENOENT, "destroyed static msgq rejected");
    }

    EK_LOG_INFO("msgq_test finished: %s", s_done ? "FAILED" : "ALL PASS");
}

static void *_msgq_main(void *arg)
{
    (void)arg;
    _msgq_test();
    return NULL;
}

static void *_cv_signal_waiter(void *arg)
{
    int id = (int)(intptr_t)arg;
    int err = 0;

    ek_os_mutex_lock(s_cv_mtx, EK_OS_FOREVER);
    while (!s_cv_ready)
    {
        err = ek_os_condvar_wait(s_cv, s_cv_mtx, EK_OS_FOREVER);
        if (err != 0) break;
    }
    s_cv_wait_err[id] = err;
    if (err == 0)
    {
        s_cv_order[s_cv_wake_cnt++] = id;
    }
    s_cv_unlock_err[id] = ek_os_mutex_unlock(s_cv_mtx);
    return NULL;
}

static void *_cv_broadcast_waiter(void *arg)
{
    int id = (int)(intptr_t)arg;
    int err = 0;

    ek_os_mutex_lock(s_cv_mtx, EK_OS_FOREVER);
    while (!s_cv_ready)
    {
        err = ek_os_condvar_wait(s_cv, s_cv_mtx, EK_OS_FOREVER);
        if (err != 0) break;
    }
    s_cv_wait_err[id] = err;
    if (err == 0)
    {
        s_cv_wake_cnt++;
    }
    s_cv_unlock_err[id] = ek_os_mutex_unlock(s_cv_mtx);
    return NULL;
}

static void *_cv_cancel_waiter(void *arg)
{
    (void)arg;
    ek_os_mutex_lock(s_cv_mtx, EK_OS_FOREVER);
    s_cv_wait_err[0] = ek_os_condvar_wait(s_cv, s_cv_mtx, EK_OS_FOREVER);
    s_cv_unlock_err[0] = ek_os_mutex_unlock(s_cv_mtx);
    return NULL;
}

/* 条件变量用例：参数边界、超时重持锁、signal 优先级唤醒、broadcast、destroy 取消、静态创建 */
static void _condvar_test(void)
{
    /* 参数边界与前置条件 */
    OS_CHECK(ek_os_condvar_wait(NULL, NULL, 0) == -ENOENT, "condvar wait(NULL, NULL) returns -ENOENT");
    OS_CHECK(ek_os_condvar_signal(NULL) == -ENOENT, "condvar signal(NULL) returns -ENOENT");
    OS_CHECK(ek_os_condvar_broadcast(NULL) == -ENOENT, "condvar broadcast(NULL) returns -ENOENT");

    s_cv = ek_os_condvar_create();
    s_cv_mtx = ek_os_mutex_create();
    OS_CHECK(s_cv != NULL && s_cv_mtx != NULL, "condvar and mutex create");
    if (!s_cv || !s_cv_mtx)
    {
        if (s_cv)
        {
            ek_os_condvar_destroy(s_cv);
            s_cv = NULL;
        }
        if (s_cv_mtx)
        {
            ek_os_mutex_destroy(s_cv_mtx);
            s_cv_mtx = NULL;
        }
        s_done = true;
        return;
    }

    OS_CHECK(ek_os_condvar_wait(s_cv, NULL, 0) == -ENOENT, "condvar wait with NULL mutex returns -ENOENT");
    OS_CHECK(ek_os_condvar_wait(NULL, s_cv_mtx, 0) == -ENOENT, "condvar wait with NULL condvar returns -ENOENT");
    OS_CHECK(ek_os_condvar_wait(s_cv, s_cv_mtx, 1000) == -EPERM, "condvar wait without holding mutex returns -EPERM");

    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "lock mutex for zero timeout wait");
    OS_CHECK(ek_os_condvar_wait(s_cv, s_cv_mtx, 0) == -ETIMEDOUT, "condvar wait(0) returns -ETIMEDOUT");
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "mutex remains held after wait(0)");

    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "lock mutex level 1");
    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "lock mutex level 2");
    OS_CHECK(ek_os_condvar_wait(s_cv, s_cv_mtx, 1000) == -EDEADLK, "condvar wait with nested mutex returns -EDEADLK");
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "unlock mutex level 2");
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "unlock mutex level 1");

    /* 无等待者时不记忆信号，超时后仍重新持有互斥量 */
    OS_CHECK(ek_os_condvar_signal(s_cv) == 0, "signal with no waiters returns 0");
    OS_CHECK(ek_os_condvar_broadcast(s_cv) == 0, "broadcast with no waiters returns 0");

    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "lock mutex for timeout wait");
    uint64_t t0 = ek_os_kernel_time();
    int err = ek_os_condvar_wait(s_cv, s_cv_mtx, 5000);
    uint64_t t1 = ek_os_kernel_time();
    OS_CHECK(err == -ETIMEDOUT, "condvar timed wait returns -ETIMEDOUT");
    OS_CHECK((t1 - t0) >= 4000ULL, "condvar timed wait lasts >= 4 ms");
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "mutex re-acquired after timed wait");

    /* 单唤醒 signal：每次只唤醒一个，且按优先级高者先醒 */
    s_cv_ready = false;
    s_cv_wake_cnt = 0;
    for (int i = 0; i < 4; i++)
    {
        s_cv_order[i] = -1;
        s_cv_wait_err[i] = -1;
        s_cv_unlock_err[i] = -1;
    }

    int sig_low_tid = ek_os_thread_create("cv_sig_l", _cv_signal_waiter, (void *)(intptr_t)0, PRIO_LOW, TEST_STACK);
    int sig_high_tid = ek_os_thread_create("cv_sig_h", _cv_signal_waiter, (void *)(intptr_t)1, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);

    OS_CHECK(s_cv_wake_cnt == 0, "signal waiters blocked before signal");
    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "condvar wait released mutex atomically");
    s_cv_ready = true;
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "unlock mutex before signal");

    OS_CHECK(ek_os_condvar_signal(s_cv) == 0, "signal first waiter");
    ek_os_thread_sleep(5000);
    OS_CHECK(s_cv_wake_cnt == 1, "signal wakes exactly one waiter");
    OS_CHECK(s_cv_order[0] == 1, "signal wakes highest priority waiter first");

    OS_CHECK(ek_os_condvar_signal(s_cv) == 0, "signal second waiter");
    OS_CHECK(ek_os_thread_join(sig_high_tid, -1, NULL) == 0, "join signal high waiter");
    OS_CHECK(ek_os_thread_join(sig_low_tid, -1, NULL) == 0, "join signal low waiter");
    OS_CHECK(s_cv_wake_cnt == 2 && s_cv_order[1] == 0, "second signal wakes remaining waiter");
    OS_CHECK(s_cv_wait_err[0] == 0 && s_cv_wait_err[1] == 0, "signal waiters return 0");
    OS_CHECK(s_cv_unlock_err[0] == 0 && s_cv_unlock_err[1] == 0, "signal waiters re-acquired mutex");

    /* 广播唤醒 broadcast：一次唤醒所有等待者 */
    s_cv_ready = false;
    s_cv_wake_cnt = 0;
    for (int i = 0; i < 3; i++)
    {
        s_cv_wait_err[i] = -1;
        s_cv_unlock_err[i] = -1;
    }

    int bc_tid0 = ek_os_thread_create("cv_bc0", _cv_broadcast_waiter, (void *)(intptr_t)0, PRIO_MID, TEST_STACK);
    int bc_tid1 = ek_os_thread_create("cv_bc1", _cv_broadcast_waiter, (void *)(intptr_t)1, PRIO_LOW, TEST_STACK);
    int bc_tid2 = ek_os_thread_create("cv_bc2", _cv_broadcast_waiter, (void *)(intptr_t)2, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_cv_wake_cnt == 0, "broadcast waiters blocked before broadcast");

    OS_CHECK(ek_os_mutex_lock(s_cv_mtx, 0) == 0, "lock mutex before broadcast");
    s_cv_ready = true;
    OS_CHECK(ek_os_mutex_unlock(s_cv_mtx) == 0, "unlock mutex before broadcast");
    OS_CHECK(ek_os_condvar_broadcast(s_cv) == 0, "broadcast wakes all waiters");

    OS_CHECK(ek_os_thread_join(bc_tid0, -1, NULL) == 0, "join broadcast waiter 0");
    OS_CHECK(ek_os_thread_join(bc_tid1, -1, NULL) == 0, "join broadcast waiter 1");
    OS_CHECK(ek_os_thread_join(bc_tid2, -1, NULL) == 0, "join broadcast waiter 2");
    OS_CHECK(s_cv_wake_cnt == 3, "all 3 broadcast waiters woken");
    OS_CHECK(s_cv_wait_err[0] == 0 && s_cv_wait_err[1] == 0 && s_cv_wait_err[2] == 0, "broadcast waiters return 0");
    OS_CHECK(s_cv_unlock_err[0] == 0 && s_cv_unlock_err[1] == 0 && s_cv_unlock_err[2] == 0,
             "broadcast waiters re-acquired mutex");

    /* destroy 唤醒等待者返回 -ECANCELED 且仍重持锁 */
    s_cv_wait_err[0] = 1;
    s_cv_unlock_err[0] = 1;
    int cancel_tid = ek_os_thread_create("cv_cancel", _cv_cancel_waiter, NULL, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_cv_wait_err[0] == 1, "cancel waiter still blocked on condvar");

    ek_os_condvar_destroy(s_cv);
    s_cv = NULL;

    OS_CHECK(ek_os_thread_join(cancel_tid, -1, NULL) == 0, "join cancelled condvar waiter");
    OS_CHECK(s_cv_wait_err[0] == -ECANCELED, "destroy wakes condvar waiter with -ECANCELED");
    OS_CHECK(s_cv_unlock_err[0] == 0, "cancelled waiter re-acquired mutex");

    ek_os_mutex_destroy(s_cv_mtx);
    s_cv_mtx = NULL;

    /* 静态创建边界与销毁后拒绝访问 */
    OS_CHECK(ek_os_condvar_create_static(NULL, sizeof(s_static_cv_mem.buf)) == NULL,
             "condvar create_static(NULL) returns NULL");
    OS_CHECK(ek_os_condvar_create_static(s_static_cv_mem.buf, EK_OS_CONDVAR_STATIC_BUF_SIZE - 1U) == NULL,
             "condvar create_static undersized returns NULL");

    _Alignas(ek_os_condvar_t) uint8_t unaligned_buf[EK_OS_CONDVAR_STATIC_BUF_SIZE + sizeof(void *)];
    OS_CHECK(ek_os_condvar_create_static(unaligned_buf + 1U, EK_OS_CONDVAR_STATIC_BUF_SIZE) == NULL,
             "condvar create_static unaligned returns NULL");

    ek_os_condvar_t *st_cv = ek_os_condvar_create_static(s_static_cv_mem.buf, sizeof(s_static_cv_mem.buf));
    OS_CHECK(st_cv != NULL, "condvar create_static");
    if (st_cv)
    {
        OS_CHECK(ek_os_condvar_signal(st_cv) == 0, "signal static condvar");
        OS_CHECK(ek_os_condvar_broadcast(st_cv) == 0, "broadcast static condvar");
        ek_os_condvar_destroy(st_cv);
        OS_CHECK(ek_os_condvar_signal(st_cv) == -ENOENT, "destroyed static condvar rejects signal");
        OS_CHECK(ek_os_condvar_broadcast(st_cv) == -ENOENT, "destroyed static condvar rejects broadcast");
    }

    EK_LOG_INFO("condvar_test finished: %s", s_done ? "FAILED" : "ALL PASS");
}

static void *_condvar_main(void *arg)
{
    (void)arg;
    _condvar_test();
    return NULL;
}

/* ---------------- 屏障自检 ---------------- */

static ek_os_barrier_t *s_bar;
static volatile int s_bar_entered[3];
static volatile int s_bar_passed[3];
static volatile int s_bar_cycle_err[2][3];
static volatile int s_bar_single_err;

static union
{
    ek_os_barrier_t align;
    uint8_t buf[EK_OS_BARRIER_STATIC_BUF_SIZE];
} s_static_bar_mem;

#if EKCFG_STATIC_ALLOC == 1
EK_DEFINE_OS_BARRIER(s_def_bar, 2U);
#endif

static void *_bar_cycle_worker(void *arg)
{
    int idx = (int)(intptr_t)arg;
    for (int r = 0; r < 3; r++)
    {
        s_bar_entered[r]++;
        int err = ek_os_barrier_wait(s_bar, EK_OS_FOREVER);
        s_bar_cycle_err[idx][r] = err;
        if (err == 0) s_bar_passed[r]++;
    }
    return NULL;
}

static void *_bar_single_waiter(void *arg)
{
    ek_os_barrier_t *bar = (ek_os_barrier_t *)arg;
    s_bar_single_err = ek_os_barrier_wait(bar, EK_OS_FOREVER);
    return NULL;
}

static void _barrier_test(void)
{
    /* 参数边界 */
    OS_CHECK(ek_os_barrier_create(0U) == NULL, "barrier create(0) returns NULL");
    OS_CHECK(ek_os_barrier_wait(NULL, 0) == -ENOENT, "barrier wait(NULL) returns -ENOENT");
    OS_CHECK(ek_os_barrier_reset(NULL, 2U) == -ENOENT, "barrier reset(NULL) returns -ENOENT");

    /* 单线程 expected == 1 与周期复用 */
    ek_os_barrier_t *b1 = ek_os_barrier_create(1U);
    OS_CHECK(b1 != NULL, "barrier create(1)");
    if (b1)
    {
        for (int r = 0; r < 3; r++)
        {
            OS_CHECK(ek_os_barrier_wait(b1, 0) == 0, "barrier expected=1 wait(0) succeeds repeatedly");
        }
        ek_os_barrier_destroy(b1);
    }

    /* 多线程 3 线程 x 3 轮周期同步 + 等待中 reset 返回 -EBUSY */
    s_bar = ek_os_barrier_create(3U);
    OS_CHECK(s_bar != NULL, "barrier create(3)");
    if (!s_bar)
    {
        s_done = true;
        return;
    }

    for (int r = 0; r < 3; r++)
    {
        s_bar_entered[r] = 0;
        s_bar_passed[r] = 0;
        s_bar_cycle_err[0][r] = -1;
        s_bar_cycle_err[1][r] = -1;
    }

    int w0 = ek_os_thread_create("bar_w0", _bar_cycle_worker, (void *)(intptr_t)0, PRIO_MID, TEST_STACK);
    int w1 = ek_os_thread_create("bar_w1", _bar_cycle_worker, (void *)(intptr_t)1, PRIO_MID, TEST_STACK);

    for (int r = 0; r < 3; r++)
    {
        ek_os_thread_sleep(5000);
        OS_CHECK(s_bar_entered[r] == 2 && s_bar_passed[r] == 0, "two workers blocked at barrier round");
        if (r == 0)
        {
            OS_CHECK(ek_os_barrier_reset(s_bar, 2U) == -EBUSY, "reset while waiting returns -EBUSY");
        }
        OS_CHECK(ek_os_barrier_wait(s_bar, 0) == 0, "last arrival with timeout=0 releases all");
        if (r == 2)
        {
            ek_os_thread_sleep(5000);
        }
    }

    OS_CHECK(ek_os_thread_join(w0, -1, NULL) == 0, "join barrier cycle worker 0");
    OS_CHECK(ek_os_thread_join(w1, -1, NULL) == 0, "join barrier cycle worker 1");

    for (int r = 0; r < 3; r++)
    {
        OS_CHECK(s_bar_passed[r] == 2 && s_bar_cycle_err[0][r] == 0 && s_bar_cycle_err[1][r] == 0,
                 "barrier cycle round released both workers with 0");
    }
    OS_CHECK(s_bar->count == 0U, "barrier count reset to 0 after cycles");

    /* 非阻塞 -EAGAIN、超时 -ETIMEDOUT、count 自动回滚及 reset */
    OS_CHECK(ek_os_barrier_wait(s_bar, 0) == -EAGAIN, "non-blocking wait not full returns -EAGAIN");
    OS_CHECK(s_bar->count == 0U, "barrier count unchanged after -EAGAIN");

    uint64_t t0 = ek_os_kernel_time();
    int terr = ek_os_barrier_wait(s_bar, 5000);
    uint64_t t1 = ek_os_kernel_time();
    OS_CHECK(terr == -ETIMEDOUT, "timed wait returns -ETIMEDOUT");
    OS_CHECK((t1 - t0) >= 4000ULL, "barrier timed wait lasts >= 4 ms");
    OS_CHECK(s_bar->count == 0U, "barrier count rolled back after -ETIMEDOUT");
    OS_CHECK(ek_os_barrier_reset(s_bar, 0U) == -EINVAL, "reset(0) returns -EINVAL");
    OS_CHECK(ek_os_barrier_reset(s_bar, 2U) == 0, "reset(2) succeeds when idle");

    /* 等待期间被 suspend 打断返回 -EINTR 与 count 回滚 */
    s_bar_single_err = 1;
    int susp_tid = ek_os_thread_create("bar_susp", _bar_single_waiter, s_bar, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_bar_single_err == 1, "waiter blocked before suspend");
    OS_CHECK(ek_os_thread_suspend(susp_tid) == 0, "suspend barrier waiter");
    OS_CHECK(ek_os_thread_resume(susp_tid) == 0, "resume suspended barrier waiter");
    OS_CHECK(ek_os_thread_join(susp_tid, -1, NULL) == 0, "join suspended barrier waiter");
    OS_CHECK(s_bar_single_err == -EINTR, "suspended barrier waiter returns -EINTR");
    OS_CHECK(s_bar->count == 0U, "barrier count rolled back after -EINTR");
    OS_CHECK(ek_os_barrier_wait(s_bar, 0) == -EAGAIN, "barrier still requires 2 threads after -EINTR rollback");

    /* destroy 唤醒等待者返回 -ECANCELED */
    s_bar_single_err = 1;
    int cancel_tid = ek_os_thread_create("bar_cancel", _bar_single_waiter, s_bar, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_bar_single_err == 1, "waiter blocked before barrier destroy");
    ek_os_barrier_destroy(s_bar);
    s_bar = NULL;
    OS_CHECK(ek_os_thread_join(cancel_tid, -1, NULL) == 0, "join cancelled barrier waiter");
    OS_CHECK(s_bar_single_err == -ECANCELED, "destroy wakes barrier waiter with -ECANCELED");

    /* 静态缓冲区创建边界与销毁后拒绝访问 */
    OS_CHECK(ek_os_barrier_create_static(NULL, sizeof(s_static_bar_mem.buf), 1U) == NULL,
             "barrier create_static(NULL) returns NULL");
    OS_CHECK(ek_os_barrier_create_static(s_static_bar_mem.buf, EK_OS_BARRIER_STATIC_BUF_SIZE - 1U, 1U) == NULL,
             "barrier create_static undersized returns NULL");

    _Alignas(ek_os_barrier_t) uint8_t unaligned_bar_buf[EK_OS_BARRIER_STATIC_BUF_SIZE + sizeof(void *)];
    OS_CHECK(ek_os_barrier_create_static(unaligned_bar_buf + 1U, EK_OS_BARRIER_STATIC_BUF_SIZE, 1U) == NULL,
             "barrier create_static unaligned returns NULL");
    OS_CHECK(ek_os_barrier_create_static(s_static_bar_mem.buf, sizeof(s_static_bar_mem.buf), 0U) == NULL,
             "barrier create_static(0) returns NULL");

    ek_os_barrier_t *st_bar = ek_os_barrier_create_static(s_static_bar_mem.buf, sizeof(s_static_bar_mem.buf), 1U);
    OS_CHECK(st_bar != NULL, "barrier create_static");
    if (st_bar)
    {
        OS_CHECK(ek_os_barrier_wait(st_bar, 0) == 0, "wait static barrier(1)");
        OS_CHECK(ek_os_barrier_reset(st_bar, 2U) == 0, "reset static barrier");
        ek_os_barrier_destroy(st_bar);
        OS_CHECK(ek_os_barrier_wait(st_bar, 0) == -ENOENT, "destroyed static barrier rejects wait");
        OS_CHECK(ek_os_barrier_reset(st_bar, 1U) == -ENOENT, "destroyed static barrier rejects reset");
    }

#if EKCFG_STATIC_ALLOC == 1
    /* EK_DEFINE_OS_BARRIER 自动注册验证 */
    OS_CHECK(s_def_bar != NULL, "EK_DEFINE_OS_BARRIER initialized");
    if (s_def_bar)
    {
        s_bar_single_err = 1;
        int def_tid = ek_os_thread_create("bar_def", _bar_single_waiter, s_def_bar, PRIO_MID, TEST_STACK);
        ek_os_thread_sleep(5000);
        OS_CHECK(ek_os_barrier_wait(s_def_bar, 0) == 0, "wait EK_DEFINE_OS_BARRIER releases waiter");
        OS_CHECK(ek_os_thread_join(def_tid, -1, NULL) == 0, "join EK_DEFINE_OS_BARRIER waiter");
        OS_CHECK(s_bar_single_err == 0, "EK_DEFINE_OS_BARRIER waiter returns 0");
    }
#endif

    EK_LOG_INFO("barrier_test finished: %s", s_done ? "FAILED" : "ALL PASS");
}

static void *_barrier_main(void *arg)
{
    (void)arg;
    _barrier_test();
    return NULL;
}

/* ---------------- 侵入式 FIFO 自检 ---------------- */

typedef struct
{
    ek_os_kfifo_node_t node;
    uint32_t id;
} kfifo_item_t;

static ek_os_kfifo_t *s_kfifo;
static kfifo_item_t s_kfifo_items[4];
static volatile int s_kfifo_get_err[2];
static void *volatile s_kfifo_got[2];
static volatile int s_kfifo_wake_order[2];
static volatile int s_kfifo_wake_idx;

static union
{
    ek_os_kfifo_t align;
    uint8_t buf[EK_OS_KFIFO_STATIC_BUF_SIZE];
} s_static_kfifo_mem;

#if EKCFG_STATIC_ALLOC == 1
EK_DEFINE_OS_KFIFO(s_def_kfifo);
#endif

static void *_kfifo_waiter(void *arg)
{
    int idx = (int)(intptr_t)arg;
    void *p = NULL;
    s_kfifo_got[idx] = NULL;
    int err = ek_os_kfifo_get(s_kfifo, &p, EK_OS_FOREVER);
    s_kfifo_get_err[idx] = err;
    if (err == 0)
    {
        s_kfifo_got[idx] = p;
        s_kfifo_wake_order[s_kfifo_wake_idx++] = idx;
    }
    return NULL;
}

static void _kfifo_test(void)
{
    void *out = NULL;

    /* 参数与无效句柄边界 */
    OS_CHECK(ek_os_kfifo_is_empty(NULL), "kfifo is_empty(NULL) returns true");
    OS_CHECK(ek_os_kfifo_put(NULL, &s_kfifo_items[0]) == -ENOENT, "kfifo put(NULL) returns -ENOENT");
    OS_CHECK(ek_os_kfifo_get(NULL, &out, 0) == -ENOENT, "kfifo get(NULL) returns -ENOENT");
    OS_CHECK(ek_os_kfifo_peek(NULL, &out) == -ENOENT, "kfifo peek(NULL) returns -ENOENT");

    s_kfifo = ek_os_kfifo_create();
    OS_CHECK(s_kfifo != NULL, "kfifo create");
    if (!s_kfifo)
    {
        s_done = true;
        return;
    }

    _Alignas(ek_os_kfifo_node_t) uint8_t unaligned_item[sizeof(kfifo_item_t) + sizeof(void *)];
    OS_CHECK(ek_os_kfifo_put(s_kfifo, NULL) == -EINVAL, "kfifo put(NULL data) returns -EINVAL");
    OS_CHECK(ek_os_kfifo_put(s_kfifo, unaligned_item + 1U) == -EINVAL, "kfifo put unaligned data returns -EINVAL");
    OS_CHECK(ek_os_kfifo_get(s_kfifo, NULL, 0) == -EINVAL, "kfifo get(NULL out) returns -EINVAL");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, NULL) == -EINVAL, "kfifo peek(NULL out) returns -EINVAL");

    /* 空队列查询与超时 */
    OS_CHECK(ek_os_kfifo_is_empty(s_kfifo), "kfifo starts empty");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, &out) == -EAGAIN, "kfifo peek on empty returns -EAGAIN");
    OS_CHECK(ek_os_kfifo_get(s_kfifo, &out, 0) == -EAGAIN, "kfifo get(0) on empty returns -EAGAIN");

    uint64_t t0 = ek_os_kernel_time();
    int terr = ek_os_kfifo_get(s_kfifo, &out, 5000);
    uint64_t t1 = ek_os_kernel_time();
    OS_CHECK(terr == -ETIMEDOUT, "kfifo timed get returns -ETIMEDOUT");
    OS_CHECK((t1 - t0) >= 4000ULL, "kfifo timed get lasts >= 4 ms");

    /* FIFO 顺序、peek 只读、零拷贝地址一致及取空后二次入队 */
    for (uint32_t i = 0; i < 4U; i++)
    {
        s_kfifo_items[i].node.next = (ek_os_kfifo_node_t *)(uintptr_t)0xDEADBEEFU;
        s_kfifo_items[i].id = (i + 1U) * 10U;
    }

    for (uint32_t i = 0; i < 3U; i++)
    {
        OS_CHECK(ek_os_kfifo_put(s_kfifo, &s_kfifo_items[i]) == 0, "kfifo put item");
    }

    OS_CHECK(!ek_os_kfifo_is_empty(s_kfifo), "kfifo non-empty after put");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, &out) == 0 && out == &s_kfifo_items[0], "kfifo peek returns head without pop");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, &out) == 0 && out == &s_kfifo_items[0], "kfifo second peek still returns head");

    for (uint32_t i = 0; i < 3U; i++)
    {
        out = NULL;
        OS_CHECK(ek_os_kfifo_get(s_kfifo, &out, 0) == 0 && out == &s_kfifo_items[i], "kfifo get returns exact item ptr");
        kfifo_item_t *got = (kfifo_item_t *)out;
        OS_CHECK(got->id == (i + 1U) * 10U && got->node.next == NULL, "kfifo item payload intact and next cleared");
    }

    OS_CHECK(ek_os_kfifo_is_empty(s_kfifo), "kfifo empty after popping all items");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, &out) == -EAGAIN, "kfifo peek returns -EAGAIN after drained");
    OS_CHECK(ek_os_kfifo_get(s_kfifo, &out, 0) == -EAGAIN, "kfifo get(0) returns -EAGAIN after drained");

    OS_CHECK(ek_os_kfifo_put(s_kfifo, &s_kfifo_items[3]) == 0, "kfifo put after drained");
    OS_CHECK(s_kfifo_items[2].node.next == NULL, "previous tail node not corrupted on re-enqueue");
    OS_CHECK(ek_os_kfifo_peek(s_kfifo, &out) == 0 && out == &s_kfifo_items[3], "kfifo peek sees re-enqueued item");
    out = NULL;
    OS_CHECK(ek_os_kfifo_get(s_kfifo, &out, 0) == 0 && out == &s_kfifo_items[3] &&
                 ((kfifo_item_t *)out)->id == 40U,
             "kfifo get retrieves re-enqueued item");
    OS_CHECK(ek_os_kfifo_is_empty(s_kfifo), "kfifo empty again after draining re-enqueued item");

    /* 阻塞直达交接与优先级唤醒顺序 */
    s_kfifo_get_err[0] = 1;
    s_kfifo_get_err[1] = 1;
    s_kfifo_got[0] = NULL;
    s_kfifo_got[1] = NULL;
    s_kfifo_wake_idx = 0;

    int low_tid = ek_os_thread_create("kfifo_low", _kfifo_waiter, (void *)(intptr_t)0, PRIO_LOW, TEST_STACK);
    int high_tid = ek_os_thread_create("kfifo_high", _kfifo_waiter, (void *)(intptr_t)1, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_kfifo_get_err[0] == 1 && s_kfifo_get_err[1] == 1, "both waiters blocked on empty kfifo");

    OS_CHECK(ek_os_kfifo_put(s_kfifo, &s_kfifo_items[0]) == 0, "kfifo put handoffs directly to waiter");
    OS_CHECK(ek_os_kfifo_is_empty(s_kfifo), "direct handoff keeps kfifo list empty");
    ek_os_thread_sleep(5000);
    OS_CHECK(s_kfifo_get_err[1] == 0 && s_kfifo_got[1] == &s_kfifo_items[0] && s_kfifo_wake_order[0] == 1 &&
                 s_kfifo_get_err[0] == 1,
             "highest priority waiter woken first via direct handoff");

    OS_CHECK(ek_os_kfifo_put(s_kfifo, &s_kfifo_items[1]) == 0, "kfifo put handoffs to remaining waiter");
    OS_CHECK(ek_os_thread_join(high_tid, -1, NULL) == 0, "join high kfifo waiter");
    OS_CHECK(ek_os_thread_join(low_tid, -1, NULL) == 0, "join low kfifo waiter");
    OS_CHECK(s_kfifo_get_err[0] == 0 && s_kfifo_got[0] == &s_kfifo_items[1] && s_kfifo_wake_order[1] == 0,
             "low waiter receives second item");
    OS_CHECK(ek_os_kfifo_is_empty(s_kfifo), "kfifo empty after handoffs");

    /* 等待期间被 suspend 打断返回 -EINTR */
    s_kfifo_get_err[0] = 1;
    int susp_tid = ek_os_thread_create("kfifo_susp", _kfifo_waiter, (void *)(intptr_t)0, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(ek_os_thread_suspend(susp_tid) == 0, "suspend kfifo waiter");
    OS_CHECK(ek_os_thread_resume(susp_tid) == 0, "resume suspended kfifo waiter");
    OS_CHECK(ek_os_thread_join(susp_tid, -1, NULL) == 0, "join suspended kfifo waiter");
    OS_CHECK(s_kfifo_get_err[0] == -EINTR && s_kfifo_got[0] == NULL, "suspended kfifo waiter returns -EINTR");

    /* destroy 唤醒等待者返回 -ECANCELED */
    s_kfifo_get_err[0] = 1;
    int cancel_tid = ek_os_thread_create("kfifo_cancel", _kfifo_waiter, (void *)(intptr_t)0, PRIO_HIGH, TEST_STACK);
    ek_os_thread_sleep(5000);
    OS_CHECK(s_kfifo_get_err[0] == 1, "waiter blocked before kfifo destroy");
    ek_os_kfifo_destroy(s_kfifo);
    s_kfifo = NULL;
    OS_CHECK(ek_os_thread_join(cancel_tid, -1, NULL) == 0, "join cancelled kfifo waiter");
    OS_CHECK(s_kfifo_get_err[0] == -ECANCELED, "destroy wakes kfifo waiter with -ECANCELED");

    /* 静态缓冲区创建边界与销毁后拒绝访问 */
    OS_CHECK(ek_os_kfifo_create_static(NULL, sizeof(s_static_kfifo_mem.buf)) == NULL,
             "kfifo create_static(NULL) returns NULL");
    OS_CHECK(ek_os_kfifo_create_static(s_static_kfifo_mem.buf, EK_OS_KFIFO_STATIC_BUF_SIZE - 1U) == NULL,
             "kfifo create_static undersized returns NULL");

    _Alignas(ek_os_kfifo_t) uint8_t unaligned_kfifo_buf[EK_OS_KFIFO_STATIC_BUF_SIZE + sizeof(void *)];
    OS_CHECK(ek_os_kfifo_create_static(unaligned_kfifo_buf + 1U, EK_OS_KFIFO_STATIC_BUF_SIZE) == NULL,
             "kfifo create_static unaligned returns NULL");

    ek_os_kfifo_t *st_fifo = ek_os_kfifo_create_static(s_static_kfifo_mem.buf, sizeof(s_static_kfifo_mem.buf));
    OS_CHECK(st_fifo != NULL, "kfifo create_static");
    if (st_fifo)
    {
        OS_CHECK(ek_os_kfifo_put(st_fifo, &s_kfifo_items[0]) == 0, "put static kfifo");
        OS_CHECK(ek_os_kfifo_peek(st_fifo, &out) == 0 && out == &s_kfifo_items[0], "peek static kfifo");
        OS_CHECK(ek_os_kfifo_get(st_fifo, &out, 0) == 0 && out == &s_kfifo_items[0], "get static kfifo");
        ek_os_kfifo_destroy(st_fifo);
        OS_CHECK(ek_os_kfifo_is_empty(st_fifo), "destroyed static kfifo reports empty");
        OS_CHECK(ek_os_kfifo_put(st_fifo, &s_kfifo_items[0]) == -ENOENT, "destroyed static kfifo rejects put");
        OS_CHECK(ek_os_kfifo_get(st_fifo, &out, 0) == -ENOENT, "destroyed static kfifo rejects get");
        OS_CHECK(ek_os_kfifo_peek(st_fifo, &out) == -ENOENT, "destroyed static kfifo rejects peek");
    }

#if EKCFG_STATIC_ALLOC == 1
    /* EK_DEFINE_OS_KFIFO 自动注册验证 */
    OS_CHECK(s_def_kfifo != NULL && ek_os_kfifo_is_empty(s_def_kfifo), "EK_DEFINE_OS_KFIFO initialized and empty");
    if (s_def_kfifo)
    {
        OS_CHECK(ek_os_kfifo_put(s_def_kfifo, &s_kfifo_items[0]) == 0, "put EK_DEFINE_OS_KFIFO");
        OS_CHECK(ek_os_kfifo_peek(s_def_kfifo, &out) == 0 && out == &s_kfifo_items[0], "peek EK_DEFINE_OS_KFIFO");
        out = NULL;
        OS_CHECK(ek_os_kfifo_get(s_def_kfifo, &out, 0) == 0 && out == &s_kfifo_items[0], "get EK_DEFINE_OS_KFIFO");
        OS_CHECK(ek_os_kfifo_is_empty(s_def_kfifo), "EK_DEFINE_OS_KFIFO empty after get");
    }
#endif

    EK_LOG_INFO("kfifo_test finished: %s", s_done ? "FAILED" : "ALL PASS");
}

static void *_kfifo_main(void *arg)
{
    (void)arg;
    _kfifo_test();
    return NULL;
}

static void *_test_main(void *arg)
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

    int waiter_tid = ek_os_thread_create("os_waiter", _sem_waiter, NULL, PRIO_MID, TEST_STACK);
    ek_os_thread_sleep(2000); /* 让 waiter 进入阻塞 */
    EK_LOG_INFO("waiter err before give = %d", s_wake_err);
    OS_CHECK(s_wake_err == -1, "waiter blocked on sem");
    OS_CHECK(ek_os_thread_join(waiter_tid, 0, NULL) == -EAGAIN, "join(0) on running thread returns -EAGAIN");
    OS_CHECK(ek_os_thread_join(ek_os_kernel_current_tid(), 0, NULL) == -EDEADLK, "join self returns -EDEADLK");

    OS_CHECK(ek_os_sem_give(s_sem) == 0, "sem give ok");
    void *join_ret = NULL;
    int join_err = ek_os_thread_join(waiter_tid, -1, &join_ret);
    EK_LOG_INFO("waiter err after give = %d, join_ret = %d", s_wake_err, (int)(intptr_t)join_ret);
    OS_CHECK(s_wake_err == 0, "waiter woken by give");
    OS_CHECK(join_err == 0 && (int)(intptr_t)join_ret == 42, "join returns child exit code 42");
    OS_CHECK(ek_os_thread_join(waiter_tid, 0, NULL) == -ESRCH, "re-join returns -ESRCH");

    /* ---- 3. 优先级抢占（兼测先死后 join） ---- */
    s_high_ran = false;
    int low_tid = ek_os_thread_create("os_low", _low_thread, NULL, PRIO_LOW, TEST_STACK);
    ek_os_thread_sleep(2000);
    OS_CHECK(s_high_ran, "higher prio thread preempts");
    OS_CHECK(ek_os_thread_join(low_tid, -1, NULL) == 0, "join already-dead thread returns 0");

    /* ---- 4. 调度器锁阻止抢占 ---- */
    s_high_ran = false;
    OS_CHECK(ek_os_kernel_sched_lock() >= 1, "sched lock");
    int high2_tid = ek_os_thread_create("os_high2", _high_thread, NULL, PRIO_HIGH, TEST_STACK);
    OS_CHECK(!s_high_ran, "sched lock blocks preemption");
    OS_CHECK(ek_os_kernel_sched_unlock() == 0, "sched unlock");
    ek_os_thread_sleep(2000);
    OS_CHECK(s_high_ran, "unlock allows preemption");
    OS_CHECK(ek_os_thread_join(high2_tid, -1, NULL) == 0, "join high2 returns 0");

    int mtx_tid = ek_os_thread_create("mtx_main", _mutex_main, NULL, PRIO_TEST, 1536);
    OS_CHECK(ek_os_thread_join(mtx_tid, -1, NULL) == 0, "join mutex test thread");

    int msgq_tid = ek_os_thread_create("msgq_main", _msgq_main, NULL, PRIO_TEST, 1536);
    OS_CHECK(ek_os_thread_join(msgq_tid, -1, NULL) == 0, "join msgq test thread");

    int cv_tid = ek_os_thread_create("cv_main", _condvar_main, NULL, PRIO_TEST, 1536);
    OS_CHECK(ek_os_thread_join(cv_tid, -1, NULL) == 0, "join condvar test thread");

    int bar_tid = ek_os_thread_create("bar_main", _barrier_main, NULL, PRIO_TEST, 1536);
    OS_CHECK(ek_os_thread_join(bar_tid, -1, NULL) == 0, "join barrier test thread");

    int kfifo_tid = ek_os_thread_create("kfifo_main", _kfifo_main, NULL, PRIO_TEST, 1536);
    OS_CHECK(ek_os_thread_join(kfifo_tid, -1, NULL) == 0, "join kfifo test thread");

    EK_LOG_INFO("os_test finished: %s", s_done ? "FAILED" : "ALL PASS");
    s_done = true;

    return NULL;
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
