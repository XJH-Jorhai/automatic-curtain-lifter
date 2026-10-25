#ifndef G030_TUYA_BRIDGE_H
#define G030_TUYA_BRIDGE_H

#include <stdint.h>

void bridge_init(void);
void bridge_poll(void);
void bridge_uart_tx(uint8_t value);
void bridge_all_data_update(void);
uint8_t bridge_handle_control(const uint8_t *value, uint16_t length);
uint8_t bridge_handle_percent(const uint8_t *value, uint16_t length);
void bridge_on_command(uint8_t command);
void bridge_on_wifi_test(uint8_t result, uint8_t rssi);

#endif
