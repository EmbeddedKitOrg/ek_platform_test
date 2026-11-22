#include "tm_api.h"

#include "main.h"
#include "bsp_os_timer.h"

#include "ek/core/arch.h"
#include "ek/core/io.h"
#include "ek/mem/heap.h"
#include "ek/os/kernel.h"
#include "ek/os/msgq.h"
#include "ek/os/sem.h"
#include "ek/os/thread.h"

#define TM_MAX_THREADS     (8)
#define TM_THREAD_STACK    (1024U)

#define TM_MAX_QUEUES      (4)
#define TM_QUEUE_DEPTH     (10U)
#define TM_QUEUE_ITEM_SIZE (4U * sizeof(unsigned long))

#define TM_MAX_SEMS        (4)

#define TM_MAX_POOLS       (4)
#define TM_POOL_MEM_SIZE   (4096U)
#define TM_POOL_BLOCK_SIZE (128U)

/* 使用未占用的 CAN1_TX_IRQn（IRQ 63）作为软件触发中断源，避免与板级 TIMER6 冲突 */
#define TM_SW_IRQn         CAN1_TX_IRQn

typedef void (*tm_entry_fn_t)(void);

static int s_tm_tids[TM_MAX_THREADS];
static tm_entry_fn_t s_tm_entries[TM_MAX_THREADS];
static ek_os_msgq_t *s_tm_queues[TM_MAX_QUEUES];
static ek_os_sem_t *s_tm_sems[TM_MAX_SEMS];

static uint8_t s_tm_pool_mem[TM_MAX_POOLS][TM_POOL_MEM_SIZE] __EK_ALIGNED(8);
static tlsf_t s_tm_pools[TM_MAX_POOLS];

#if defined(TM_BENCH_CASE_INTERRUPT)
extern void tm_interrupt_handler(void);
#elif defined(TM_BENCH_CASE_INTERRUPT_PREEMPTION)
extern void tm_interrupt_preemption_handler(void);
#endif

static void *_tm_thread_trampoline(void *arg)
{
    int idx = (int)(intptr_t)arg;
    if (idx >= 0 && idx < TM_MAX_THREADS && s_tm_entries[idx])
    {
        s_tm_entries[idx]();
    }
    return NULL;
}

static void _tm_irq_handler(void)
{
    NVIC_ClearPendingIRQ(TM_SW_IRQn);
#if defined(TM_BENCH_CASE_INTERRUPT)
    tm_interrupt_handler();
#elif defined(TM_BENCH_CASE_INTERRUPT_PREEMPTION)
    tm_interrupt_preemption_handler();
#endif
}

EK_DEFINE_ARCH_IRQ(TM_SW_IRQn, _tm_irq_handler);

void tm_port_cause_interrupt(void)
{
    NVIC_SetPendingIRQ(TM_SW_IRQn);
    ek_arch_dsb();
    ek_arch_isb();
}

void tm_initialize(void (*test_initialization_function)(void))
{
    if (!test_initialization_function) return;

    nvic_irq_enable(TM_SW_IRQn, 0, 2);
    test_initialization_function();
    ek_os_kernel_start(&g_bsp_os_timer_ops);
}

int tm_thread_create(int thread_id, int priority, void (*entry_function)(void))
{
    if (thread_id < 0 || thread_id >= TM_MAX_THREADS || !entry_function) return TM_ERROR;
    if (priority < 0 || priority > (int)EKCFG_OS_THREAD_MIN_PRIO) return TM_ERROR;
    if (s_tm_tids[thread_id] > 0) return TM_ERROR;

    s_tm_entries[thread_id] = entry_function;

    char name[12];
    (void)ek_snprintf(name, sizeof(name), "tm_%d", thread_id);

    /* 若在内核运行期创建，锁住调度防止新建线程在挂起前提前抢占执行 */
    bool started = (ek_os_kernel_current_tid() > 0);
    if (started) (void)ek_os_kernel_sched_lock();

    int tid = ek_os_thread_create(name, _tm_thread_trampoline, (void *)(intptr_t)thread_id, (uint8_t)priority,
                                  TM_THREAD_STACK);
    if (tid > 0)
    {
        s_tm_tids[thread_id] = tid;
        (void)ek_os_thread_suspend(tid);
    }

    if (started) (void)ek_os_kernel_sched_unlock();

    return (tid > 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_thread_resume(int thread_id)
{
    if (thread_id < 0 || thread_id >= TM_MAX_THREADS || s_tm_tids[thread_id] <= 0) return TM_ERROR;
    return (ek_os_thread_resume(s_tm_tids[thread_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_thread_suspend(int thread_id)
{
    if (thread_id < 0 || thread_id >= TM_MAX_THREADS || s_tm_tids[thread_id] <= 0) return TM_ERROR;
    return (ek_os_thread_suspend(s_tm_tids[thread_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

void tm_thread_relinquish(void)
{
    (void)ek_os_thread_yield();
}

void tm_thread_sleep(int seconds)
{
    (void)ek_os_thread_sleep(EK_OS_SEC(seconds));
}

int tm_queue_create(int queue_id)
{
    if (queue_id < 0 || queue_id >= TM_MAX_QUEUES || s_tm_queues[queue_id] != NULL) return TM_ERROR;
    s_tm_queues[queue_id] = ek_os_msgq_create(TM_QUEUE_ITEM_SIZE, TM_QUEUE_DEPTH);
    return s_tm_queues[queue_id] ? TM_SUCCESS : TM_ERROR;
}

int tm_queue_send(int queue_id, unsigned long *message_ptr)
{
    if (queue_id < 0 || queue_id >= TM_MAX_QUEUES || !s_tm_queues[queue_id] || !message_ptr) return TM_ERROR;
    return (ek_os_msgq_send(s_tm_queues[queue_id], message_ptr, EK_OS_FOREVER) == 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_queue_receive(int queue_id, unsigned long *message_ptr)
{
    if (queue_id < 0 || queue_id >= TM_MAX_QUEUES || !s_tm_queues[queue_id] || !message_ptr) return TM_ERROR;
    return (ek_os_msgq_recv(s_tm_queues[queue_id], message_ptr, EK_OS_FOREVER) == 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_semaphore_create(int semaphore_id)
{
    if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMS || s_tm_sems[semaphore_id] != NULL) return TM_ERROR;
    s_tm_sems[semaphore_id] = ek_os_sem_create(1U);
    return s_tm_sems[semaphore_id] ? TM_SUCCESS : TM_ERROR;
}

int tm_semaphore_get(int semaphore_id)
{
    if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMS || !s_tm_sems[semaphore_id]) return TM_ERROR;
    return (ek_os_sem_take(s_tm_sems[semaphore_id], EK_OS_FOREVER) == 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_semaphore_put(int semaphore_id)
{
    if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMS || !s_tm_sems[semaphore_id]) return TM_ERROR;
    return (ek_os_sem_give(s_tm_sems[semaphore_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

int tm_memory_pool_create(int pool_id)
{
    if (pool_id < 0 || pool_id >= TM_MAX_POOLS || s_tm_pools[pool_id] != NULL) return TM_ERROR;
    s_tm_pools[pool_id] = tlsf_create_with_pool(s_tm_pool_mem[pool_id], TM_POOL_MEM_SIZE);
    return s_tm_pools[pool_id] ? TM_SUCCESS : TM_ERROR;
}

int tm_memory_pool_allocate(int pool_id, unsigned char **memory_ptr)
{
    if (pool_id < 0 || pool_id >= TM_MAX_POOLS || !s_tm_pools[pool_id] || !memory_ptr) return TM_ERROR;

    bool locked = (ek_os_kernel_sched_lock() > 0);
    void *ptr = tlsf_malloc(s_tm_pools[pool_id], TM_POOL_BLOCK_SIZE);
    if (locked) (void)ek_os_kernel_sched_unlock();

    *memory_ptr = (unsigned char *)ptr;
    return ptr ? TM_SUCCESS : TM_ERROR;
}

int tm_memory_pool_deallocate(int pool_id, unsigned char *memory_ptr)
{
    if (pool_id < 0 || pool_id >= TM_MAX_POOLS || !s_tm_pools[pool_id] || !memory_ptr) return TM_ERROR;

    bool locked = (ek_os_kernel_sched_lock() > 0);
    tlsf_free(s_tm_pools[pool_id], memory_ptr);
    if (locked) (void)ek_os_kernel_sched_unlock();

    return TM_SUCCESS;
}
