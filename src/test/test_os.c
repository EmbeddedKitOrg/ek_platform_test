/**
 * @file test_os.c
 * @brief RTOS 内核自检：sleep 精度、信号量、互斥量、消息队列、条件变量、优先级抢占、调度器锁
 */

#include "test.h"

#if EKCFG_RTOS == 1

#    include "ek/os/condvar.h"
#    include "ek/os/kernel.h"
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
