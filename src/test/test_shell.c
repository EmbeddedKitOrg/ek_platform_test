#include "test.h"

#if EKCFG_SHELL == 1

#    include "ek/core/io.h"
#    include "ek/core/log.h"
#    include "ek/shell/shell.h"
#    include "ek/vfs/vfs_core.h"

#    if EKCFG_RTOS == 1
#        include "ek/os/kernel.h"
#        include "ek/os/thread.h"
#    endif

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_INFO);

static void _shell_run(void)
{
#    ifndef EKCFG_IO_CONSOLE_PATH
    int err = ek_vfs_bind_stdio("/dev/uart0");
    if (err < 0)
    {
        ek_printf("shell: bind stdio failed: %d\r\n", err);
        return;
    }
#    endif

    // RTOS 下需要非阻塞读取输入流，避免无输入时占用全部 CPU 无法调度其它线程
    int in_fd = ek_vfs_open("/dev/uart0", O_RDONLY | O_NONBLOCK);
    if (in_fd < 0)
    {
        in_fd = EK_VFS_STDIN;
    }

    ek_shell_cfg_t cfg = {
        .in_fd = in_fd,
        .out_fd = EK_VFS_STDOUT,
    };
    ek_shell_init(&cfg);

    char buf[32];
    while (1)
    {
        int n = ek_shell_step(buf, sizeof(buf));
#    if EKCFG_RTOS == 1
        if (n <= 0)
        {
            ek_os_thread_sleep(EK_OS_MSEC(5));
        }
#    else
        __EK_UNUSED(n);
#    endif
    }
}

#    if EKCFG_RTOS == 1

static void *_shell_thread(void *arg)
{
    __EK_UNUSED(arg);
    _shell_run();
    return NULL;
}

void shell_test(void)
{
    // 为命令回调预留充足栈空间（含栈哨兵检测开销）
    int tid = ek_os_thread_create("shell", _shell_thread, NULL, 8U, 3072U);
    if (tid < 0)
    {
        EK_LOG_ERROR("shell: thread create failed: %d", tid);
        return;
    }
    EK_LOG_INFO("shell: thread created tid=%d", tid);
}

#    else

void shell_test(void)
{
    _shell_run();
}

#    endif /* EKCFG_RTOS == 1 */

#else

void shell_test(void)
{
}

#endif /* EKCFG_SHELL == 1 */
