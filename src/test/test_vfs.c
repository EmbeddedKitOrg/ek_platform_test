#include "test.h"
#include "ek/vfs/vfs_core.h"
#include "ek/vfs/vfs_bus.h"
#include "ek/mem/static_alloc.h"
#include "ek/core/log.h"

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_NONE);

static const ek_vfs_bus_ops_t _dt_bus_ops;
static const ek_vfs_ops_t _dt_dev_ops;
static int _dt_bus_ctx;
static int _dt_dev_ctx;

// 静态设备树示例：路径、总线、设备由 EK_DEFINE_* 在启动时自动注册
EK_DEFINE_VFS_PATH("/dev/dt", EK_VFS_DIR, NULL, NULL);
EK_DEFINE_VFS_BUS(dt_bus, "dt", &_dt_bus_ops, &_dt_bus_ctx);
EK_DEFINE_VFS_BUS_DEV(dt_eeprom, "dt", "eeprom", &_dt_dev_ops, &_dt_dev_ctx);

void vfs_test(void)
{
    int err;
    ek_vfs_ops_t ops = { 0 };

    err = ek_static_alloc_get_init_error();
    if (err < 0) EK_LOG_ERROR("static allocation: %s.", strerror(-err));

    err = ek_vfs_open("/dev/bus/dt/eeprom", O_RDONLY);
    if (err < 0) EK_LOG_ERROR("static bus device open: %s.", strerror(-err));
    else ek_vfs_close(err);

    err = ek_vfs_register("/dev/a", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));
    err = ek_vfs_register("/dev/a", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    err = ek_vfs_register("/dev/b", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    err = ek_vfs_register("/dev/c", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    err = ek_vfs_register("/dev/d/e/f", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    err = ek_vfs_register("/fs", EK_VFS_DEV, &ops, NULL);
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    EK_LOG_INFO("OK");

    char buf[1024] = { 0 };
    ek_vfs_tree_dump(buf, 1024);
    printf("%s\n", buf);

    err = ek_vfs_unregister("/");
    if (err < 0) EK_LOG_ERROR("%s.", strerror(-err));

    EK_LOG_INFO("available node: %d", ek_vfs_node_avail());
}
