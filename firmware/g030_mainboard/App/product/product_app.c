#include "main.h"
#include "usart.h"
#include "tim.h"
#include "adc.h"
#include "i2c.h"
#include "motor.h"
#include "product.h"
#include "bridge.h"
#include "wifi.h"

UART_HandleTypeDef huart1;

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }

void TuyaApp_Run(void)
{
    MX_TIM14_Init();
    Motor_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    MX_USART2_UART_Init();
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &pins);
    huart1.Instance = USART1;
    huart1.Init = huart2.Init;
    huart1.Init.BaudRate = 9600U;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
    HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    Product_Init();
    bridge_init();
    Product_PlayStartup();
    for (;;) {
        Motor_KeepAlive();
        Product_Poll();
        bridge_poll();
        uint32_t now = HAL_GetTick();
        if (Product_PairingRequested()) bridge_request_ap();
        bool on = wifi_work_state == 4U ||
            (wifi_work_state == 1U && now % 1000U < 500U) ||
            ((wifi_work_state == 0U || wifi_work_state == 6U) && now % 250U < 125U);
        HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

