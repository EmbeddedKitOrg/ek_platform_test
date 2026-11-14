#include "bsp_uart.h"
#include "ek/core/export.h"
#include "ek/core/log.h"
#include "ek/container/ringbuf.h"
#include "ek/core/arch.h"
#include "ek/mem/static_alloc.h"
#include "ek/vfs/vfs_core.h"

void bsp_uart_init(void)
{
    // Clock On
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_DMA1);

    // GPIO AF Init
    gpio_af_set(BSP_UART_TX_PORT, GPIO_AF_7, BSP_UART_TX_PIN);
    gpio_af_set(BSP_UART_RX_PORT, GPIO_AF_7, BSP_UART_RX_PIN);

    gpio_mode_set(BSP_UART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART_TX_PIN);
    gpio_mode_set(BSP_UART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART_RX_PIN);

    gpio_output_options_set(BSP_UART_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_UART_TX_PIN);
    gpio_output_options_set(BSP_UART_RX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_UART_RX_PIN);

    // USART Init
    usart_deinit(BSP_UART);
    usart_baudrate_set(BSP_UART, 115200);
    usart_parity_config(BSP_UART, USART_PM_NONE);
    usart_word_length_set(BSP_UART, USART_WL_8BIT);
    usart_stop_bit_set(BSP_UART, USART_STB_1BIT);
    usart_hardware_flow_rts_config(BSP_UART, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(BSP_UART, USART_CTS_DISABLE);

    // Enable UART
    usart_transmit_config(BSP_UART, USART_TRANSMIT_ENABLE);
    usart_receive_config(BSP_UART, USART_RECEIVE_ENABLE);

    usart_enable(BSP_UART); // 使能串口
}

EK_EXPORT_BOARD(bsp_uart_init, EXPORT_ORDER_BOARD_UART);

int bsp_uart_send_data(uint8_t ch)
{
    while (RESET == usart_flag_get(BSP_UART, USART_FLAG_TBE)); // 先等发送缓冲区空闲,否则会覆盖未发出的字节
    usart_data_transmit(BSP_UART, (uint8_t)ch);
    return ch;
}

#define BSP_UART_RX_BUF_SIZE (128) /* SPSC 实际可存 127 字节 */
EK_DEFINE_RINGBUF_SPSC(s_uart_rx_rb, uint8_t, BSP_UART_RX_BUF_SIZE);

static int s_open_count;
static volatile bool s_tx_done;

static void _rx_push(void)
{
    uint8_t ch = (uint8_t)usart_data_receive(BSP_UART);
    (void)ek_ringbuf_write_spsc(s_uart_rx_rb, &ch); // 缓冲区满时丢弃新字节
}

static void _uart_isr(void)
{
    if (RESET != usart_interrupt_flag_get(BSP_UART, USART_INT_FLAG_IDLE))
    {
        // 先读 STAT 再读 DATA 才能清除 IDLE;仅 RBNE 置位时这次读到的才是有效字节
        if (RESET != usart_flag_get(BSP_UART, USART_FLAG_RBNE)) _rx_push();
        else (void)usart_data_receive(BSP_UART);
    }
    if (RESET != usart_interrupt_flag_get(BSP_UART, USART_INT_FLAG_RBNE)) _rx_push();
    if (RESET != usart_interrupt_flag_get(BSP_UART, USART_INT_FLAG_TC))
    {
        usart_flag_clear(BSP_UART, USART_FLAG_TC);
        s_tx_done = true;
    }
}

EK_DEFINE_ARCH_IRQ(USART0_IRQn, _uart_isr);

// 关中断后再检查并睡眠,避免 TC 中断在检查与 WFI 之间到达而永久睡眠
static void _uart_wait_tx_done(void)
{
    while (!s_tx_done)
    {
        __disable_irq();
        if (!s_tx_done) __WFI();
        __enable_irq();
    }
}

// 打开实例的 O_NONBLOCK 状态直接编码在 file_priv 中,每次 open 独立
static bool _is_nonblock(void *file_priv)
{
    return ((uintptr_t)file_priv & O_NONBLOCK) != 0;
}

static int _uart_open(void *node_priv, int flags, void **file_priv)
{
    __EK_UNUSED(node_priv);
    *file_priv = (void *)(uintptr_t)(flags & O_NONBLOCK);
    if (s_open_count++ == 0)
    {
        usart_interrupt_enable(BSP_UART, USART_INT_RBNE);
        usart_interrupt_enable(BSP_UART, USART_INT_IDLE);
        usart_interrupt_enable(BSP_UART, USART_INT_TC);
        nvic_irq_enable(USART0_IRQn, 0, 5);
    }
    return 0;
}

static int _uart_close(void *file_priv)
{
    __EK_UNUSED(file_priv);
    if (s_open_count > 0 && --s_open_count == 0)
    {
        nvic_irq_disable(USART0_IRQn);
        usart_interrupt_disable(BSP_UART, USART_INT_TC);
        usart_interrupt_disable(BSP_UART, USART_INT_IDLE);
        usart_interrupt_disable(BSP_UART, USART_INT_RBNE);
    }
    return 0;
}

static ssize_t _uart_read(void *file_priv, void *buf, size_t count, ek_off_t *offset)
{
    __EK_UNUSED(offset);
    uint8_t *dst = buf;
    bool nonblock = _is_nonblock(file_priv);
    size_t n = 0;
    while (n < count)
    {
        if (ek_ringbuf_read_spsc(s_uart_rx_rb, &dst[n]) == 0)
        {
            n++;
            continue;
        }
        if (n > 0 || nonblock) break; // 阻塞模式在无数据时自旋等待首字节
    }
    if (n == 0 && count > 0 && nonblock) return -EAGAIN;
    return (ssize_t)n;
}

static ssize_t _uart_write(void *file_priv, const void *buf, size_t count, ek_off_t *offset)
{
    __EK_UNUSED(offset);
    const uint8_t *src = buf;
    bool nonblock = _is_nonblock(file_priv);
    size_t n = 0;
    while (n < count)
    {
        if (nonblock && RESET == usart_flag_get(BSP_UART, USART_FLAG_TBE)) break;
        // 最后一个字节的 TC 才代表本次写完成,发送前清掉之前的完成事件
        if (!nonblock && n + 1 == count) s_tx_done = false;
        bsp_uart_send_data(src[n++]);
    }
    if (n == 0 && count > 0 && nonblock) return -EAGAIN;
    if (!nonblock && n > 0) _uart_wait_tx_done();
    return (ssize_t)n;
}

static const ek_vfs_ops_t s_uart_ops = {
    .open = _uart_open,
    .read = _uart_read,
    .write = _uart_write,
    .close = _uart_close,
};

EK_DEFINE_VFS_PATH("/dev/uart0", EK_VFS_DEV, &s_uart_ops, NULL);
