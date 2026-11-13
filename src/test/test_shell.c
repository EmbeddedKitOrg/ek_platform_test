#include "test.h"
#include "ek/shell/ek_shell.h"
#include "ek/vfs/ek_vfs_core.h"

// 暂时屏蔽本模块日志，避免干扰 Shell 交互输出
EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_MAX);

// 树输出缓冲区放到静态区，避免占用本就紧张的 1KB 主栈
static char s_tree_buf[1024];

static int _cmd_hello(int argc, char *argv[])
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);

    printf("hello, ek_platform!\r\n");
    return 0;
}

static int _cmd_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        printf("%s%s", argv[i], (i + 1 < argc) ? " " : "");
    }
    printf("\r\n");

    return 0;
}

static int _cmd_tree(int argc, char *argv[])
{
    __EK_UNUSED(argc);
    __EK_UNUSED(argv);

    int len = ek_vfs_tree_dump(s_tree_buf, sizeof(s_tree_buf));
    if (len < 0) return len;

    printf("%s", s_tree_buf);
    return 0;
}

EK_DEFINE_SHELL(hello, _cmd_hello);
EK_DEFINE_SHELL(echo, _cmd_echo);
EK_DEFINE_SHELL(tree, _cmd_tree);

void shell_test(void)
{
    printf("\r\n=== ek shell ===\r\n");

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
