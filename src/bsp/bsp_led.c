#include "bsp_led.h"
#include "ek/core/ek_export.h"

void bsp_led_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOG);

    gpio_mode_set(BSP_LED1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, BSP_LED1_PIN);
    gpio_mode_set(BSP_LED2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, BSP_LED2_PIN);
    gpio_mode_set(BSP_LED3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, BSP_LED3_PIN);
    gpio_mode_set(BSP_LED4_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, BSP_LED4_PIN);

    gpio_output_options_set(BSP_LED1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_LED1_PIN);
    gpio_output_options_set(BSP_LED2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_LED2_PIN);
    gpio_output_options_set(BSP_LED3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_LED3_PIN);
    gpio_output_options_set(BSP_LED4_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_LED4_PIN);

    gpio_bit_reset(BSP_LED1_PORT, BSP_LED1_PIN);
    gpio_bit_reset(BSP_LED2_PORT, BSP_LED2_PIN);
    gpio_bit_reset(BSP_LED3_PORT, BSP_LED3_PIN);
    gpio_bit_reset(BSP_LED4_PORT, BSP_LED4_PIN);
}

EK_EXPORT_BOARD(bsp_led_init, EXPORT_ORDER_BOARD_LED);
