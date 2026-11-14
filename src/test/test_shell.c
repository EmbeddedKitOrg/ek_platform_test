#include "test.h"
#include "ek/shell/ek_shell.h"
#include "ek/vfs/ek_vfs_core.h"

// 暂时屏蔽本模块日志，避免干扰 Shell 交互输出
EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_MAX);

void shell_test(void)
{
    int err = ek_vfs_bind_stdio("/dev/uart0");
    if (err < 0)
    {
        printf("shell: bind stdio failed: %d\r\n", err);
        return;
    }

    ek_shell_cfg_t cfg = {
        .in_fd = 0,
        .out_fd = 1,
    };
    ek_shell_init(&cfg);

    char buf[32];
    while (1)
    {
        ek_shell_step(buf, sizeof(buf));
    }
}
