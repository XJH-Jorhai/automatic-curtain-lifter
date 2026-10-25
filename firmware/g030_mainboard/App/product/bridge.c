#include "bridge.h"
#include "product.h"
#include "motor.h"
#include "motion_config.h"
#include "usart.h"
#include "wifi.h"

#define UART_RING_SIZE 256U
enum { CONTROL_OPEN, CONTROL_STOP, CONTROL_CLOSE };
static uint8_t uart_rx_byte, uart_ring[UART_RING_SIZE];
static volatile uint16_t uart_head, uart_tail;
static volatile bool link_fault;
static uint8_t reported_percent = 0xffU, reported_control = 0xffU, reported_target = 0xffU;
static uint32_t reported_at;
static uint32_t calibration_reset_at[2];
static uint8_t calibration_reset_stage[2];
static bool calibration_online;
volatile uint8_t bridge_calibration_pending;
volatile uint32_t bridge_calibration_reset_tx;
volatile uint32_t bridge_rx_frames, bridge_rx_overflows, bridge_uart_errors, bridge_tx_errors;
volatile uint32_t bridge_rejected_commands, bridge_control_rx_count, bridge_stop_accepted;
volatile uint32_t bridge_percent_rx_count, bridge_percent_accepted, bridge_last_percent;
volatile uint32_t bridge_calibration_rx_count, bridge_calibration_saved;
volatile uint8_t bridge_calibration_dpid, bridge_calibration_result;
volatile uint8_t bridge_last_control;
volatile uint8_t bridge_last_command;

static void log_text(const char *message)
{
    const char *end = message;
    while (*end) ++end;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)(end - message), 30U);
}

static uint8_t control(void)
{
    if (!Product_Busy()) return CONTROL_STOP;
    return Product_Opening() ? CONTROL_OPEN : CONTROL_CLOSE;
}

static bool can_remote(void)
{
    return wifi_work_state == 4U && !link_fault;
}

void bridge_init(void)
{
    bridge_calibration_pending = 0U;
    calibration_reset_stage[0] = calibration_reset_stage[1] = 0U;
    calibration_online = false;
    wifi_protocol_init();
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U) != HAL_OK) Error_Handler();
    if (LIFT_TRQ_CAPTURE)
        log_text("G030 TRQ CAPTURE; 360rpm full current; local keys only; EEPROM read-only; M=status X=stop\r\n");
    else log_text("G030 PRODUCT v15; boundary adjustment=hold-to-run; DP102/103 auto-reset; key hold1s=auto in range; PC13 tap=endpoint hold5s=AP; S/M=status X=stop\r\n");
}

void bridge_request_ap(void)
{
    Product_Stop(LIFT_USER_STOP);
    mcu_set_wifi_mode(AP_CONFIG);
}

void bridge_uart_tx(uint8_t value)
{
    if (HAL_UART_Transmit(&huart1, &value, 1U, 5U) != HAL_OK) {
        ++bridge_tx_errors;
        link_fault = true;
        Product_RemoteLostISR();
    }
}

static void report_motion(void)
{
    /* Target and position are separate. Reports never enter cmd06 handlers. */
    uint8_t percent = Product_Percent();
    (void)mcu_dp_enum_update(DPID_CONTROL, control());
    (void)mcu_dp_value_update(DPID_PERCENT_CONTROL, Product_Target());
    (void)mcu_dp_value_update(DPID_PERCENT_STATE, percent);
}

void bridge_all_data_update(void)
{
    report_motion();
    /* Queries always show resting buttons; never execute a calibration. */
    (void)mcu_dp_bool_update(DPID_CALI_UPPER_END, FALSE);
    (void)mcu_dp_bool_update(DPID_CALI_DOWN_END, FALSE);
}

uint8_t bridge_handle_calibration(uint8_t dpid, const uint8_t *value, uint16_t length)
{
    ++bridge_calibration_rx_count;
    if ((dpid != DPID_CALI_UPPER_END && dpid != DPID_CALI_DOWN_END) ||
        !value || length != 1U || value[0] > 1U) {
        ++bridge_rejected_commands;
        return ERROR;
    }
    if (!value[0]) { /* Initialization/reset is never a calibration event. */
        (void)mcu_dp_bool_update(dpid, FALSE);
        return SUCCESS;
    }
    bridge_calibration_dpid = dpid;
    ProductCalibration result = can_remote() ?
        Product_Calibrate(dpid == DPID_CALI_UPPER_END) : PRODUCT_CAL_UNAVAILABLE;
    bridge_calibration_result = (uint8_t)result;
    bool saved = result == PRODUCT_CAL_UPPER || result == PRODUCT_CAL_LOWER;
    if (saved) ++bridge_calibration_saved;
    else ++bridge_rejected_commands;
    report_motion();
    /* Acknowledge the momentary press separately, then publish release from
     * the main loop after300ms and once more1200ms later. Bounded event-only
     * reports outlive the original download callback / panel optimistic state. */
    (void)mcu_dp_bool_update(dpid, TRUE);
    unsigned index = dpid == DPID_CALI_UPPER_END ? 0U : 1U;
    calibration_reset_at[index] = HAL_GetTick();
    calibration_reset_stage[index] = 1U;
    bridge_calibration_pending |= (uint8_t)(1U << index);
    return saved ? SUCCESS : ERROR;
}

static void calibration_reset_poll(void)
{
    if (!can_remote()) { calibration_online = false; return; }
    if (!calibration_online) {
        /* Also clear a stale cloud button after boot/reconnect without relying
         * on the module issuing a status query. */
        (void)mcu_dp_bool_update(DPID_CALI_UPPER_END, FALSE);
        (void)mcu_dp_bool_update(DPID_CALI_DOWN_END, FALSE);
        calibration_online = true;
    }
    for (unsigned i = 0U; i < 2U; ++i) {
        unsigned stage = calibration_reset_stage[i];
        if (!stage || (uint32_t)(HAL_GetTick() - calibration_reset_at[i]) <
            (stage == 1U ? 300U : 1200U)) continue;
        (void)mcu_dp_bool_update(i ? DPID_CALI_DOWN_END : DPID_CALI_UPPER_END, FALSE);
        ++bridge_calibration_reset_tx;
        calibration_reset_at[i] = HAL_GetTick();
        calibration_reset_stage[i] = stage == 1U ? 2U : 0U;
        if (stage == 2U) bridge_calibration_pending &= (uint8_t)~(1U << i);
    }
}

uint8_t bridge_handle_control(const uint8_t *value, uint16_t length)
{
    ++bridge_control_rx_count;
    if (!value || length != 1U || value[0] > CONTROL_CLOSE) {
        ++bridge_rejected_commands; return ERROR;
    }
    bridge_last_control = value[0];
    if (value[0] == CONTROL_STOP) {
        Product_Stop(LIFT_USER_STOP);
        ++bridge_stop_accepted;
    } else if (!can_remote() ||
               !Product_Request(value[0] == CONTROL_OPEN ? 100U : 0U, PRODUCT_REMOTE)) {
        ++bridge_rejected_commands;
        bridge_all_data_update();
        return ERROR;
    }
    bridge_all_data_update();
    return SUCCESS;
}

uint8_t bridge_handle_percent(const uint8_t *value, uint16_t length)
{
    ++bridge_percent_rx_count;
    if (!value || length != 4U) { ++bridge_rejected_commands; return ERROR; }
    uint32_t target = ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
                      ((uint32_t)value[2] << 8) | value[3];
    bridge_last_percent = target;
    if (target > 100U || !can_remote() || !Product_Request((uint8_t)target, PRODUCT_REMOTE)) {
        ++bridge_rejected_commands;
        bridge_all_data_update();
        return ERROR;
    }
    ++bridge_percent_accepted;
    bridge_all_data_update();
    return SUCCESS;
}

static void console_poll(void)
{
    uint8_t byte;
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE)) __HAL_UART_CLEAR_OREFLAG(&huart2);
    if (HAL_UART_Receive(&huart2, &byte, 1U, 0U) != HAL_OK) return;
    if (byte == 'X') {
        Product_Stop(LIFT_USER_STOP);
        bridge_all_data_update();
    } else if (byte == 'S' || byte == 'M' || byte == '?') {
        char report[512];
        Product_Report(report, sizeof(report));
        log_text(report);
    }
    /* Bench F/B/T/H, raw EEPROM writes and music commands are absent. */
}

void bridge_poll(void)
{
    if (Product_RemoteActive() && !can_remote()) Product_Stop(LIFT_LINK_LOST);
    /* Bounded batches prevent a noisy module from starving keys / ADC / planner. */
    unsigned drained = 0U;
    while (uart_tail != uart_head && drained++ < 64U) {
        uint8_t value = uart_ring[uart_tail];
        uart_tail = (uint16_t)((uart_tail + 1U) & (UART_RING_SIZE - 1U));
        uart_receive_input(value);
        if ((drained & 15U) == 0U) wifi_uart_service();
    }
    wifi_uart_service();
    if (Product_RemoteActive() && !can_remote()) Product_Stop(LIFT_LINK_LOST);
    calibration_reset_poll();
    uint32_t now = HAL_GetTick();
    uint8_t state = control(), percent = Product_Percent(), target = Product_Target();
    if (wifi_work_state != 4U) reported_control = reported_percent = reported_target = 0xffU;
    if (wifi_work_state == 4U && (state != reported_control ||
        target != reported_target ||
        (percent != reported_percent && (uint32_t)(now - reported_at) >= 500U))) {
        reported_control = state;
        reported_percent = percent;
        reported_target = target;
        reported_at = now;
        report_motion();
    }
    console_poll();
}

void bridge_on_command(uint8_t command) { ++bridge_rx_frames; bridge_last_command = command; }
void bridge_on_wifi_test(uint8_t result, uint8_t rssi) { (void)result; (void)rssi; }

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1) return;
    uint16_t next = (uint16_t)((uart_head + 1U) & (UART_RING_SIZE - 1U));
    if (next == uart_tail) {
        ++bridge_rx_overflows;
        link_fault = true;
        Product_RemoteLostISR();
    } else {
        uart_ring[uart_head] = uart_rx_byte;
        uart_head = next;
    }
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U) != HAL_OK) {
        ++bridge_uart_errors;
        link_fault = true;
        Product_RemoteLostISR();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1) return;
    ++bridge_uart_errors;
    link_fault = true;
    Product_RemoteLostISR();
    (void)HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U);
}

