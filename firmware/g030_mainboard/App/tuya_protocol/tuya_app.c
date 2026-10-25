#include "main.h"
#include "usart.h"
#include "bridge.h"
#include "wifi.h"
#include "motor.h"
#include "tim.h"

UART_HandleTypeDef huart1;
volatile uint32_t pairing_press_events;
volatile uint32_t pairing_button_down;
volatile uint32_t pairing_request_ms;

/* SWD-only diagnostics. Mode 1 exercises the production parser with UART1
 * isolated, mode 2 exercises the production button state machine. Reset exits.
 * No test data or synthetic pairing command ever reaches the real module. */
volatile uint32_t app_test_mode, app_test_ack, app_test_seq, app_test_done;
volatile uint32_t app_test_length, app_test_tx_length;
volatile uint8_t app_test_input[128], app_test_output[512];
volatile uint32_t app_test_now, app_test_pressed, app_test_button_result;
static uint8_t raw_last, stable, fired, armed;
static uint32_t changed_at, pressed_at;

static void button_init(uint32_t now, uint8_t down)
{
    raw_last = stable = down;
    fired = 0; armed = !down; changed_at = pressed_at = now;
}

static uint8_t button_step(uint32_t now, uint8_t down)
{
    if (down != raw_last) { raw_last = down; changed_at = now; }
    if (raw_last != stable && (uint32_t)(now - changed_at) >= 30U) {
        stable = raw_last;
        if (stable) { pressed_at = now; fired = 0; }
        else { armed = 1; fired = 0; }
    }
    pairing_button_down = stable;
    if (armed && stable && !fired && (uint32_t)(now - pressed_at) >= 5000U) {
        fired = 1;
        return 1;
    }
    return 0;
}

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }

void TuyaApp_Run(void)
{
    MX_TIM14_Init();
    Motor_Init();
    MX_USART2_UART_Init(); /* CN1 console 115200 */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_AF_PP; pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH; pins.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &pins);
    huart1.Instance = USART1; huart1.Init = huart2.Init;
    huart1.Init.BaudRate = 9600;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    button_init(HAL_GetTick(), HAL_GPIO_ReadPin(GPIOC, NETCONTROL_BTN_Pin) == GPIO_PIN_RESET);
    bridge_init();
    for (;;) {
        uint32_t now = HAL_GetTick();
        if (app_test_mode) {
            if (app_test_ack != app_test_mode) {
                Motor_Stop();
                HAL_NVIC_DisableIRQ(USART1_IRQn);
                (void)HAL_UART_AbortReceive(&huart1);
                wifi_protocol_init();
                button_init(app_test_now, app_test_pressed != 0);
                app_test_ack = app_test_mode;
            }
            if (app_test_seq != app_test_done) {
                app_test_tx_length = 0;
                if (app_test_mode == 1 && app_test_length <= sizeof(app_test_input)) {
                    for (uint32_t i = 0; i < app_test_length; ++i) {
                        uart_receive_input(app_test_input[i]);
                        wifi_uart_service();
                    }
                    wifi_uart_service();
                } else if (app_test_mode == 2) {
                    app_test_button_result = button_step(app_test_now, app_test_pressed != 0);
                }
                app_test_done = app_test_seq;
            }
            continue;
        }
        if (app_test_ack) Error_Handler(); /* reset required after diagnostics */
        Motor_KeepAlive();
        bridge_poll();
        if (button_step(now, HAL_GPIO_ReadPin(GPIOC, NETCONTROL_BTN_Pin) == GPIO_PIN_RESET)) {
            ++pairing_press_events; pairing_request_ms = now;
            bridge_request_ap();
        }
        /* AP: slow flash; EZ: fast flash; cloud: steady; offline: dark. */
        uint8_t on = wifi_work_state == 4 ||
                     (wifi_work_state == 1 && (now % 1000U) < 500U) ||
                     ((wifi_work_state == 0 || wifi_work_state == 6) && (now % 250U) < 125U);
        HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}
