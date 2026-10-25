#include "bridge.h"
#include "usart.h"
#include "wifi.h"
#include "motor.h"

/* IRQ writes head; main loop writes tail. */
#define UART_RING_SIZE 256u
enum { CONTROL_OPEN = 0u, CONTROL_STOP = 1u, CONTROL_CLOSE = 2u };
enum { WIFI_CLOUD_CONNECTED = 4u, CONTROL_NONE = 0xffu };
static uint8_t uart_rx_byte;
static uint8_t uart_ring[UART_RING_SIZE];
static volatile uint16_t uart_head;
static volatile uint16_t uart_tail;
static uint8_t motion_control = CONTROL_STOP;
static uint8_t last_motion_request = CONTROL_NONE;
static uint8_t last_wifi_state = 0xffu;
static uint8_t pending_wifi_action;
extern volatile uint32_t app_test_mode, app_test_tx_length;
extern volatile uint8_t app_test_output[512];

volatile uint32_t bridge_rx_frames;
volatile uint32_t bridge_rx_overflows;
volatile uint32_t bridge_uart_errors;
volatile uint32_t bridge_tx_errors;
volatile uint32_t bridge_rejected_commands;
volatile uint8_t bridge_last_command;
volatile uint8_t bridge_wifi_test_result = 0xffu;
volatile uint8_t bridge_wifi_test_rssi;
volatile uint32_t bridge_control_rx_count, bridge_stop_accepted;
volatile uint32_t bridge_last_control = 0xffffffffu;
volatile uint32_t bridge_wire_tx_count;
volatile uint8_t bridge_wire_tx[512];

static void log_text(const char *message)
{
    if (app_test_mode) return;
    const char *end = message;
    while (*end != '\0') ++end;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                            (uint16_t)(end - message), 100u);
}

static void log_hex(const char *label, uint8_t value)
{
    if (app_test_mode) return;
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
    motion_control = CONTROL_STOP;
    wifi_protocol_init();
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK) {
        Error_Handler();
    }
    log_text("G030 mainboard TuyaApp: module=9600 console=115200; motor timed 2s; PC13 hold 5s=AP\r\n");
}

void bridge_request_ap(void)
{
    Motor_Stop();
    log_text("Request AP pairing mode\r\n");
    pending_wifi_action = 1u;
    mcu_set_wifi_mode(AP_CONFIG);
}

static void console_poll(void)
{
    uint8_t console_byte;
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(&huart2);
    if (HAL_UART_Receive(&huart2, &console_byte, 1u, 0u) == HAL_OK) {
        if (console_byte == 'X' || console_byte == 'F' || console_byte == 'B') {
            uint8_t control = console_byte == 'X' ? CONTROL_STOP :
                              console_byte == 'F' ? CONTROL_OPEN : CONTROL_CLOSE;
            (void)bridge_handle_control(&control, 1u);
        } else if (console_byte == 'H') {
            /* Diagnostic: stall main loop; IRQ watchdog must disable drive. */
            HAL_Delay(300u);
        } else if (console_byte == 'T') {
            Motor_Stop();
            log_text(Motor_SelfTest() && Motor_IOTest() ? "SAFETY PASS\r\n" : "SAFETY FAIL\r\n");
        } else if (console_byte == 'M') {
            char report[256]; Motor_Report(report, sizeof(report)); log_text(report);
        } else if (console_byte == 'P' || console_byte == 'p') {
            bridge_request_ap();
        } else if (console_byte == 'E' || console_byte == 'e') {
            Motor_Stop();
            log_text("Request EZ pairing mode\r\n");
            pending_wifi_action = 1u;
            mcu_set_wifi_mode(SMART_CONFIG);
        } else if (console_byte == 'R' || console_byte == 'r') {
            Motor_Stop();
            log_text("Request WiFi reset\r\n");
            pending_wifi_action = 2u;
            mcu_reset_wifi();
        } else if (console_byte == '?' || console_byte == 'S' ||
                   console_byte == 's') {
            log_text("P=AP E=EZ R=reset S=network M=motor X=stop F/B=timed motion T=selftest H=stall test\r\n");
            log_hex("WIFI_STATE ", wifi_work_state);
        }
    }
}

void bridge_poll(void)
{
    if (motion_control != CONTROL_STOP && (wifi_work_state != WIFI_CLOUD_CONNECTED ||
        bridge_rx_overflows || bridge_uart_errors || bridge_tx_errors)) Motor_Stop();
    if (motion_control != CONTROL_STOP && !Motor_Active()) {
        motion_control = CONTROL_STOP;
        log_text("Motor stopped (not position reached)\r\n");
        (void)mcu_dp_enum_update(DPID_CONTROL, CONTROL_STOP);
    }
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
    console_poll();
}

void bridge_uart_tx(uint8_t value)
{
    if (app_test_mode) {
        if (app_test_tx_length < sizeof(app_test_output))
            app_test_output[app_test_tx_length++] = value;
        return;
    }
    bridge_wire_tx[bridge_wire_tx_count % sizeof(bridge_wire_tx)] = value;
    ++bridge_wire_tx_count;
    if (HAL_UART_Transmit(&huart1, &value, 1u, 20u) != HAL_OK) {
        ++bridge_tx_errors;
    }
}

static void report_control(void)
{
    (void)mcu_dp_enum_update(DPID_CONTROL, Motor_Active() ? motion_control : CONTROL_STOP);
}

void bridge_all_data_update(void)
{
    report_control();
    /* No calibrated position: do not fabricate DP101 or acknowledge DP2. */
}

uint8_t bridge_handle_control(const uint8_t *value, uint16_t length)
{
    ++bridge_control_rx_count;
    bridge_last_control = length == 1u ? value[0] : 0xffffffffu;
    if (length == 1u && value[0] == CONTROL_STOP) {
        Motor_Stop(); /* Disable drive before any blocking UART/report work. */
        motion_control = CONTROL_STOP;
        last_motion_request = CONTROL_NONE;
        ++bridge_stop_accepted;
        log_text("DP1 stop accepted\r\n");
        (void)mcu_dp_enum_update(DPID_CONTROL, CONTROL_STOP);
        return SUCCESS;
    }
    if (length == 1u && (value[0] == CONTROL_OPEN || value[0] == CONTROL_CLOSE) &&
        !app_test_mode && wifi_work_state == WIFI_CLOUD_CONNECTED && !bridge_rx_overflows &&
        !bridge_uart_errors && !bridge_tx_errors) {
        if (Motor_Active()) {
            if (motion_control == value[0]) {
                /* Retransmission cannot extend the absolute ISR deadline. */
                (void)mcu_dp_enum_update(DPID_CONTROL, motion_control);
                return SUCCESS;
            }
            Motor_Stop(); /* Opposite command stops; never reverse on the fly. */
            motion_control = CONTROL_STOP;
        } else if (last_motion_request != value[0] && Motor_StartTimed(value[0] == CONTROL_CLOSE, MOTOR_TIMED_MAX_MS)) {
            motion_control = last_motion_request = value[0];
            log_hex("DP1 timed motion ", motion_control);
            (void)mcu_dp_enum_update(DPID_CONTROL, motion_control);
            return SUCCESS;
        }
    }
    ++bridge_rejected_commands;
    log_text("DP1 rejected/interlocked; stop before retry\r\n");
    report_control();
    return ERROR;
}

uint8_t bridge_handle_percent(const uint8_t *value, uint16_t length)
{
    (void)value; (void)length;
    ++bridge_rejected_commands;
    log_text("DP2 unsupported: position not calibrated; no position report\r\n");
    return ERROR;
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
