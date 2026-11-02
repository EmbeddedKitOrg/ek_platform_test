#include "bsp_uart.h"
#include "ek/core/ek_export.h"
#include "ek/core/ek_log.h"

void bsp_uart_init(void)
{
    // Clock On
    rcu_periph_clock_enable(RCU_GPIOA); /* USART TX、RX引脚 */
    rcu_periph_clock_enable(RCU_USART0); /* USART0外设时钟 */
    rcu_periph_clock_enable(RCU_DMA1); /* USART DMA时钟 */

    // GPIO AF Init
    gpio_af_set(BSP_UART_TX_PORT, GPIO_AF_7, BSP_UART_TX_PIN);
    gpio_af_set(BSP_UART_RX_PORT, GPIO_AF_7, BSP_UART_RX_PIN);

    gpio_mode_set(BSP_UART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART_TX_PIN);
    gpio_mode_set(BSP_UART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART_RX_PIN);

    gpio_output_options_set(BSP_UART_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_UART_TX_PIN);
    gpio_output_options_set(BSP_UART_RX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_UART_RX_PIN);

    // USART Init
    usart_deinit(BSP_UART); // 复位串口
    usart_baudrate_set(BSP_UART, 115200); // 设置波特率
    usart_parity_config(BSP_UART, USART_PM_NONE); // 没有校验位
    usart_word_length_set(BSP_UART, USART_WL_8BIT); // 8位数据位
    usart_stop_bit_set(BSP_UART, USART_STB_1BIT); // 1位停止位
    usart_hardware_flow_rts_config(BSP_UART, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(BSP_UART, USART_CTS_DISABLE);

    // Enable UART
    usart_transmit_config(BSP_UART, USART_TRANSMIT_ENABLE); // 使能串口发送
    usart_receive_config(BSP_UART, USART_RECEIVE_ENABLE); // 使能串口接收

    usart_enable(BSP_UART); // 使能串口
}

EK_EXPORT_BOARD(bsp_uart_init, EK_EXPORT_ORDER_BOARD_UART);

int bsp_uart_send_data(uint8_t ch)
{
    usart_data_transmit(BSP_UART, (uint8_t)ch);
    while (RESET == usart_flag_get(BSP_UART, USART_FLAG_TBE)); // 等待发送数据缓冲区标志置位
    return ch;
}
