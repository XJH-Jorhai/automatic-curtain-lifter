#include "bridge.h"
#include "usart.h"
#include "wifi.h"

/* IRQ writes head; main loop writes tail.  There is deliberately no motor GPIO. */
#define UART_RING_SIZE 256u
static uint8_t uart_rx_byte;
static uint8_t uart_ring[UART_RING_SIZE];
static volatile uint16_t uart_head;
static volatile uint16_t uart_tail;
static uint8_t requested_percent;
static uint8_t last_wifi_state = 0xffu;
static uint8_t pending_wifi_action;

volatile uint32_t bridge_rx_frames;
volatile uint32_t bridge_rx_overflows;
volatile uint32_t bridge_uart_errors;
volatile uint32_t bridge_tx_errors;
volatile uint32_t bridge_rejected_commands;
volatile uint8_t bridge_last_command;
volatile uint8_t bridge_wifi_test_result = 0xffu;
volatile uint8_t bridge_wifi_test_rssi;

static void log_text(const char *message)
{
    const char *end = message;
    while (*end != '\0') ++end;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                            (uint16_t)(end - message), 100u);
}

static void log_hex(const char *label, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[40];
    uint8_t i = 0u;
    while (label[i] != '\0' && i < 32u) {
        line[i] = label[i];
        ++i;
    }
    line[i++] = hex[value >> 4];
    line[i++] = hex[value & 15u];
    line[i++] = '\r';
    line[i++] = '\n';
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)line, i, 100u);
}

void bridge_init(void)
{
    uart_head = 0u;
    uart_tail = 0u;
    requested_percent = 0u;
    wifi_protocol_init();
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK) {
        Error_Handler();
    }
    log_text("G030/T1-3S bench bridge ready; motor disabled\r\n");
}

void bridge_poll(void)
{
    uint8_t console_byte;
    uint8_t drained = 0u;
    while (uart_tail != uart_head) {
        uint8_t value = uart_ring[uart_tail];
        uart_tail = (uint16_t)((uart_tail + 1u) & (UART_RING_SIZE - 1u));
        uart_receive_input(value);
        if (++drained >= 32u) {
            wifi_uart_service();
            drained = 0u;
        }
    }
    wifi_uart_service();
    if (wifi_work_state != last_wifi_state) {
        last_wifi_state = wifi_work_state;
        log_hex("WIFI_STATE ", last_wifi_state);
    }
    if (pending_wifi_action == 1u &&
        mcu_get_wifimode_flag() == SET_WIFICONFIG_SUCCESS) {
        log_text("PAIR mode ACK from module\r\n");
        pending_wifi_action = 0u;
    } else if (pending_wifi_action == 2u &&
               mcu_get_reset_wifi_flag() == RESET_WIFI_SUCCESS) {
        log_text("WIFI reset ACK from module\r\n");
        pending_wifi_action = 0u;
    }
    if (HAL_UART_Receive(&huart2, &console_byte, 1u, 0u) == HAL_OK) {
        if (console_byte == 'P' || console_byte == 'p') {
            log_text("Request AP pairing mode\r\n");
            pending_wifi_action = 1u;
            mcu_set_wifi_mode(AP_CONFIG);
        } else if (console_byte == 'E' || console_byte == 'e') {
            log_text("Request EZ pairing mode\r\n");
            pending_wifi_action = 1u;
            mcu_set_wifi_mode(SMART_CONFIG);
        } else if (console_byte == 'R' || console_byte == 'r') {
            log_text("Request WiFi reset\r\n");
            pending_wifi_action = 2u;
            mcu_reset_wifi();
        } else if (console_byte == '?' || console_byte == 'S' ||
                   console_byte == 's') {
            log_text("P=AP E=EZ R=reset S=status\r\n");
            log_hex("WIFI_STATE ", wifi_work_state);
        }
    }
}

void bridge_uart_tx(uint8_t value)
{
    if (HAL_UART_Transmit(&huart1, &value, 1u, 20u) != HAL_OK) {
        ++bridge_tx_errors;
    }
}

void bridge_all_data_update(void)
{
    /* DP1 stays stopped and DP101 stays at zero until real motion is integrated. */
    (void)mcu_dp_enum_update(DPID_CONTROL, 1u);
    (void)mcu_dp_value_update(DPID_PERCENT_CONTROL, requested_percent);
    (void)mcu_dp_value_update(DPID_PERCENT_STATE, 0u);
}

uint8_t bridge_handle_control(const uint8_t *value, uint16_t length)
{
    if (length == 1u && value[0] == 1u) {
        log_text("DP1 stop accepted\r\n");
        (void)mcu_dp_enum_update(DPID_CONTROL, 1u);
        return SUCCESS;
    }
    ++bridge_rejected_commands;
    log_text("DP1 motion rejected: no motor safety inputs\r\n");
    (void)mcu_dp_enum_update(DPID_CONTROL, 1u);
    return ERROR;
}

uint8_t bridge_handle_percent(const uint8_t *value, uint16_t length)
{
    if (length != 4u || value[0] != 0u || value[1] != 0u || value[2] != 0u ||
        value[3] > 100u) {
        ++bridge_rejected_commands;
        log_text("DP2 invalid target rejected\r\n");
        (void)mcu_dp_value_update(DPID_PERCENT_CONTROL, requested_percent);
        return ERROR;
    }
    requested_percent = value[3];
    log_hex("DP2 target (hex) ", requested_percent);
    (void)mcu_dp_value_update(DPID_PERCENT_CONTROL, requested_percent);
    (void)mcu_dp_value_update(DPID_PERCENT_STATE, 0u);
    return SUCCESS;
}

void bridge_on_command(uint8_t command)
{
    ++bridge_rx_frames;
    bridge_last_command = command;
    if (command == 0x00u || command == 0x01u || command == 0x02u ||
        command == 0x06u || command == 0x08u || command == 0x0eu) {
        log_hex("RX cmd ", command);
    }
}

void bridge_on_wifi_test(uint8_t result, uint8_t rssi)
{
    bridge_wifi_test_result = result;
    bridge_wifi_test_rssi = rssi;
    log_hex("WIFI_TEST result ", result);
    log_hex("WIFI_TEST RSSI/code ", rssi);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    uint16_t next = (uint16_t)((uart_head + 1u) & (UART_RING_SIZE - 1u));
    if (next == uart_tail) {
        ++bridge_rx_overflows;
    } else {
        uart_ring[uart_head] = uart_rx_byte;
        uart_head = next;
    }
    (void)HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    ++bridge_uart_errors;
    (void)HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u);
}
