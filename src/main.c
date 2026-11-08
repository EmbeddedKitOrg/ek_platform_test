#include "main.h"
#include "test.h"

#include "bsp_uart.h"
#include "bsp_led.h"
#include "bsp_timer.h"

#include "ek/core/ek_export.h"
#include "ek/core/ek_log.h"
#include "ek/sched/ek_picothread.h"
#include "ek/vfs/ek_vfs_core.h"

static void _restore_clock_to_240m(void);
static void _uart_test(void);

int main(void)
{
    evoke_test();
    picothread_test_init();
    ringbuf_test();
    stack_test();
    snapshot_test();
    str_test();
    str_view_test();
    vec_test();
    vfs_test();
    // _uart_test();

    while (1)
    {
    }
}

static void _uart_test(void)
{
    char tree[512];
    uint8_t ch;
    ssize_t n;
    int fd;

    ek_vfs_tree_dump(tree, sizeof(tree));
    printf("%s", tree);

    fd = ek_vfs_open("/dev/uart0", O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        printf("nonblock open: %s\r\n", strerror(-fd));
        return;
    }
    n = ek_vfs_read(fd, &ch, 1);
    printf("nonblock read: %d %s\r\n", (int)n, n < 0 ? strerror(-n) : "ok");
    n = ek_vfs_write(fd, "nonblock\r\n", sizeof("nonblock\r\n") - 1);
    printf("nonblock write: %d\r\n", (int)n);
    ek_vfs_close(fd);

    fd = ek_vfs_open("/dev/uart0", O_RDWR);
    if (fd < 0)
    {
        printf("blocking open: %s\r\n", strerror(-fd));
        return;
    }
    n = ek_vfs_write(fd, "blocking\r\n", sizeof("blocking\r\n") - 1);
    printf("blocking write done: %d\r\n", (int)n);
    n = ek_vfs_read(fd, &ch, 1);
    printf("blocking read: %d 0x%02X\r\n", (int)n, ch);
    ek_vfs_write(fd, &ch, 1);
    ek_vfs_close(fd);
}

int ek_port_io_fputc(int ch)
{
    bsp_uart_send_data(ch);
    return ch;
}

static void _restore_clock_to_240m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do
    {
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    } while ((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if (0U == (RCU_CTL & RCU_CTL_HXTALSTB))
    {
        while (1)
        {
        }
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/2 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV2;
    /* APB1 = AHB/4 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV4;

    /* Configure the main PLL, PSC = 25, PLL_N = 480, PLL_P = 2, PLL_Q = 10 */
    RCU_PLL = (25U | (480U << 6U) | (((2U >> 1U) - 1U) << 16U) | (RCU_PLLSRC_HXTAL) | (10U << 24U));

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while (0U == (RCU_CTL & RCU_CTL_PLLSTB))
    {
    }

    /* Enable the high-drive to extend the clock frequency to 240 Mhz */
    PMU_CTL |= PMU_CTL_HDEN;
    while (0U == (PMU_CS & PMU_CS_HDRF))
    {
    }

    /* select the high-drive mode */
    PMU_CTL |= PMU_CTL_HDS;
    while (0U == (PMU_CS & PMU_CS_HDSRF))
    {
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLLP;

    /* wait until PLL is selected as system clock */
    while (0U == (RCU_CFG0 & RCU_SCSS_PLLP))
    {
    }
}
