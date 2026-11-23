#include "test.h"

/*
 * 将与 RTOS 兼容的测试用例注册为 Shell 命令。
 * 以下模块与当前模式不兼容或属于独立隔离镜像，未在此注册：
 * - evoke_test: 与 EKCFG_RTOS == 1 编译期互斥
 * - picothread_test_init: 裸机协作微线程调度器，依赖主循环轮询 ek_pt_schedule()
 * - os_stackoverflow_test: 需覆写弱钩子并主动破坏测试栈，保留在 EK_TEST_STACKOVERFLOW 独立构建中
 */
#if EKCFG_RTOS == 1 && EKCFG_SHELL == 1 && !defined(EK_RUN_THREAD_METRIC) && !defined(EK_RUN_LATENCY_MEASURE) && \
    !defined(EK_RUN_STACKOVERFLOW_TEST)

#    include "ek/core/io.h"
#    include "ek/os/kernel.h"
#    include "ek/os/thread.h"
#    include "ek/shell/shell.h"

#    if EKCFG_RINGBUF == 1 && EKCFG_RINGBUF_SPSC == 1
static int _cmd_test_ringbuf(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    ringbuf_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_ringbuf, _cmd_test_ringbuf);
#    endif

#    if EKCFG_STACK == 1
static int _cmd_test_stack(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    stack_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_stack, _cmd_test_stack);
#    endif

#    if EKCFG_SNAPSHOT == 1
static int _cmd_test_snapshot(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    snapshot_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_snapshot, _cmd_test_snapshot);
#    endif

#    if EKCFG_STR == 1
static int _cmd_test_str(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    str_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_str, _cmd_test_str);
#    endif

#    if EKCFG_STR_VIEW == 1
static int _cmd_test_str_view(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    str_view_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_str_view, _cmd_test_str_view);
#    endif

#    if EKCFG_VEC == 1
static int _cmd_test_vec(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    vec_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_vec, _cmd_test_vec);
#    endif

#    if EKCFG_VFS == 1
static int _cmd_test_vfs(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    vfs_test();
    return 0;
}
EK_DEFINE_SHELL_CMD(test_vfs, _cmd_test_vfs);
#    endif

static int _cmd_test_os(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    return os_test_run();
}
EK_DEFINE_SHELL_CMD(test_os, _cmd_test_os);

static void *_conf_thread(void *arg)
{
    __EK_UNUSED(arg);
    return (void *)(intptr_t)os_conformance_test();
}

static int _cmd_test_os_conf(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);

    int tid = ek_os_thread_create("conf_main", _conf_thread, NULL, 2U, 2048U);
    if (tid < 0) return tid;

    void *ret = NULL;
    int err = ek_os_thread_join(tid, EK_OS_FOREVER, &ret);
    if (err < 0) return err;

    return (int)(intptr_t)ret;
}
EK_DEFINE_SHELL_CMD(test_os_conf, _cmd_test_os_conf);

#    if defined(__ARM_FP) && (__ARM_FP != 0)
static int _cmd_test_fpu(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);
    __EK_UNUSED(sh);
    return fpu_test();
}
EK_DEFINE_SHELL_CMD(test_fpu, _cmd_test_fpu);
#    endif

// 默认跑完所有快速无外设测试；传入 "os" 时额外执行完整 RTOS 套件
static int _cmd_test_all(int argc, char *argv[], void *sh)
{
    __EK_UNUSED(sh);

#    if EKCFG_RINGBUF == 1 && EKCFG_RINGBUF_SPSC == 1
    ringbuf_test();
#    endif
#    if EKCFG_STACK == 1
    stack_test();
#    endif
#    if EKCFG_SNAPSHOT == 1
    snapshot_test();
#    endif
#    if EKCFG_STR == 1
    str_test();
#    endif
#    if EKCFG_STR_VIEW == 1
    str_view_test();
#    endif
#    if EKCFG_VEC == 1
    vec_test();
#    endif
#    if EKCFG_VFS == 1
    vfs_test();
#    endif

    int err = 0;
#    if defined(__ARM_FP) && (__ARM_FP != 0)
    err += fpu_test();
#    endif
    if (argc > 1 && strcmp(argv[1], "os") == 0)
    {
        err += os_test_run();
    }

    ek_printf("test_all finished: %s" CRLF, (err == 0) ? "ALL PASS" : "FAILED");
    return err;
}
EK_DEFINE_SHELL_CMD(test_all, _cmd_test_all);

#endif
