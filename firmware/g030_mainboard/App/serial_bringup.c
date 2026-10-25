#include "serial_bringup.h"
#include "main.h"
#include "usart.h"
#include "board_test.h"
#include "motor.h"
#include "music.h"
#include "switch_control.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char rx_line[192];
static unsigned rx_length;
static bool rx_overflow;
static uint32_t last_heartbeat;
static uint32_t heartbeat_count;
static bool heartbeat_enabled; /* Quiet by default on the shared wireless link. */
static uint8_t irq_byte;
static volatile uint8_t rx_ring[256];
static volatile unsigned rx_head, rx_tail;
static volatile bool rx_drop;
static volatile uint32_t rx_errors, rx_drops;
static const char *boot_music;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != &huart2) return;
    unsigned next = (rx_head + 1U) & 255U;
    if (next == rx_tail) {
        rx_drop = true; ++rx_drops;
    } else {
        rx_ring[rx_head] = irq_byte;
        rx_head = next;
    }
    (void)HAL_UART_Receive_IT(&huart2, &irq_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &huart2) return;
    ++rx_errors;
    rx_drop = true;
    (void)HAL_UART_AbortReceive(uart);
    (void)HAL_UART_Receive_IT(uart, &irq_byte, 1);
}

static void motor_disable(void)
{
    /* Safe boot state before accepting any explicit bench-run command. */
    HAL_GPIO_WritePin(GPIOB, DRV_ENABLE_Pin | DRV_NSLEEP_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin,
                     GPIO_PIN_RESET);
}

static void send_text(const char *text)
{
    (void)HAL_UART_Transmit(&huart2, (const uint8_t *)text,
                           (uint16_t)strlen(text), 100);
}

void SerialBringup_Init(void)
{
    motor_disable();
    BoardTest_Init();
    SwitchControl_Init();
    if (HAL_UART_Receive_IT(&huart2, &irq_byte, 1) != HAL_OK) Error_Handler();
    send_text("\r\nG030 MAINBOARD BENCH v8; SWITCH JOG 400/200 LOW CURRENT\r\n"
              "TUYA_SKIPPED; EEPROM EVENT DRIVEN; BOOT MUSIC ONCE\r\n");
    last_heartbeat = HAL_GetTick();
    /* Explicit one-time startup event. Never save, retry, or loop playback.
     * Peripherals and safety state are initialized before loading the score. */
    boot_music = Music_PlayStored();
    send_text("BOOT MUSIC ");
    send_text(boot_music ? boot_music : "STARTED");
    send_text("\r\n");
}

void SerialBringup_Poll(void)
{
    uint8_t byte;
    Motor_KeepAlive();
    SwitchControl_Poll();
    if (rx_drop) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        rx_tail = rx_head;
        rx_drop = false;
        __set_PRIMASK(primask);
        rx_length = 0;
        rx_overflow = true; /* Discard through the next line terminator. */
    }
    if (rx_tail != rx_head) {
        byte = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1U) & 255U;
        if (byte == '\r' || byte == '\n') {
            if (rx_overflow) {
                send_text("ERR line too long\r\n");
            } else if (rx_length != 0) {
                rx_line[rx_length] = '\0';
                if (strncmp(rx_line, "PING ", 5) == 0) {
                    send_text("PONG ");
                    send_text(rx_line + 5);
                    send_text("\r\n");
                } else if (strcmp(rx_line, "PING") == 0) {
                    send_text("PONG\r\n");
                } else if (strcmp(rx_line, "HB ON") == 0) {
                    heartbeat_enabled = true;
                    last_heartbeat = HAL_GetTick();
                    send_text("HB ENABLED\r\n");
                } else if (strcmp(rx_line, "HB OFF") == 0) {
                    heartbeat_enabled = false;
                    send_text("HB DISABLED\r\n");
                } else if (strcmp(rx_line, "BOOT") == 0) {
                    send_text("BOOT MUSIC ");
                    send_text(boot_music ? boot_music : "STARTED");
                    send_text("\r\n");
                } else if (strcmp(rx_line, "UART") == 0) {
                    char reply[80];
                    snprintf(reply, sizeof(reply), "UART errors=%lu drops=%lu\r\n",
                             (unsigned long)rx_errors, (unsigned long)rx_drops);
                    send_text(reply);
                } else {
                    char reply[192];
                    if (BoardTest_Command(rx_line, reply, sizeof(reply))) {
                        send_text(reply);
                        send_text("\r\n");
                    } else {
                        send_text("ERR unknown command\r\n");
                    }
                }
            }
            rx_length = 0;
            rx_overflow = false;
        } else if (!rx_overflow) {
            if (byte < 0x20 || byte > 0x7e || rx_length >= sizeof(rx_line) - 1) {
                rx_overflow = true;
            } else {
                rx_line[rx_length++] = (char)byte;
            }
        }
    }

    uint32_t now = HAL_GetTick();
    if (heartbeat_enabled && (uint32_t)(now - last_heartbeat) >= 1000U) {
        char message[128];
        last_heartbeat = now;
        ++heartbeat_count;
        (void)snprintf(message, sizeof(message),
                       "HB %lu ms=%lu MOTOR_%s ENABLE=%u nSLEEP=%u STEP=%u\r\n",
                       (unsigned long)heartbeat_count, (unsigned long)now,
                       Motor_Active() ? "ACTIVE" : "IDLE",
                       (unsigned)((GPIOB->ODR & DRV_ENABLE_Pin) != 0U),
                       (unsigned)((GPIOB->ODR & DRV_NSLEEP_Pin) != 0U),
                       (unsigned)((GPIOA->ODR & DRV_STEP_Pin) != 0U));
        send_text(message);
    }
}
