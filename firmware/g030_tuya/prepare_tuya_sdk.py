"""Port the local, untracked Tuya MCU SDK snapshot without editing the original.

The generated SDK is a template with deliberate #error markers and example DP
handlers.  This script makes only the replacements needed by the G030 bench
bridge and fails if the expected snapshot shape has changed.
"""

from pathlib import Path
import argparse


SOURCES = ("mcu_api.c", "mcu_api.h", "protocol.c", "protocol.h", "system.c",
           "system.h", "tuya_type.h", "wifi.h")


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected one match, found {count}")
    return source.replace(old, new, 1)


def replace_function(source: str, signature: str, replacement: str) -> str:
    start = source.find(signature)
    if start < 0 or source.find(signature, start + 1) >= 0:
        raise ValueError(f"function signature missing or duplicated: {signature}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise ValueError(f"opening brace missing: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[:start] + replacement + source[index + 1:]
    raise ValueError(f"closing brace missing: {signature}")


def port(snapshot: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    content = {name: (snapshot / name).read_text(encoding="utf-8") for name in SOURCES}

    protocol = content["protocol.c"]
    protocol = replace_once(protocol, '#include "wifi.h"',
                            '#include "wifi.h"\n#include "bridge.h"', "bridge include")
    protocol = replace_once(protocol, '  {DPID_PERCENT_STATE, DP_TYPE_VALUE},\n',
                            '#ifdef PRODUCT_FIRMWARE\n'
                            '  {DPID_CALI_UPPER_END, DP_TYPE_BOOL},\n'
                            '  {DPID_CALI_DOWN_END, DP_TYPE_BOOL},\n'
                            '#endif\n', "Product calibration DP overlay / read-only DP removal")
    protocol = replace_once(protocol, '                case DPID_CONTROL:',
                            '#ifdef PRODUCT_FIRMWARE\n'
                            '        case DPID_CALI_UPPER_END:\n'
                            '        case DPID_CALI_DOWN_END:\n'
                            '            return bridge_handle_calibration(dpid, value, length);\n'
                            '#endif\n'
                            '                case DPID_CONTROL:', "Product calibration dispatch")
    protocol = replace_function(
        protocol, 'void uart_transmit_output(u8 value)',
        'void uart_transmit_output(u8 value) { bridge_uart_tx(value); }')
    protocol = replace_function(
        protocol, 'void all_data_update(void)',
        'void all_data_update(void) { bridge_all_data_update(); }')
    protocol = replace_function(
        protocol,
        'static unsigned char dp_download_control_handle(const unsigned char value[], unsigned short length)',
        'static unsigned char dp_download_control_handle(const unsigned char value[], unsigned short length) '
        '{ return bridge_handle_control(value, length); }')
    protocol = replace_function(
        protocol,
        'static unsigned char dp_download_percent_control_handle(const unsigned char value[], unsigned short length)',
        'static unsigned char dp_download_percent_control_handle(const unsigned char value[], unsigned short length) '
        '{ return bridge_handle_percent(value, length); }')
    protocol = replace_function(
        protocol, 'void wifi_test_result(u8 result, u8 rssi)',
        'void wifi_test_result(u8 result, u8 rssi) { bridge_on_wifi_test(result, rssi); }')
    content["protocol.c"] = protocol

    header = content["protocol.h"]
    header = replace_once(header, '#define DPID_PERCENT_STATE 101',
                          '#define DPID_PERCENT_STATE 101\n'
                          '/* Published Product panel: momentary Bool commands, false at rest. */\n'
                          '#define DPID_CALI_UPPER_END 102\n'
                          '#define DPID_CALI_DOWN_END 103', "Product calibration IDs")
    header = replace_once(header, '#define WIFI_UART_RECV_BUF_LMT          16',
                          '#define WIFI_UART_RECV_BUF_LMT          128', "RX buffer")
    header = replace_once(header, '#define WIFI_DATA_PROCESS_LMT           24',
                          '#define WIFI_DATA_PROCESS_LMT           96', "frame buffer")
    header = replace_once(header, '#define WIFIR_UART_SEND_BUF_LMT         48',
                          '#define WIFIR_UART_SEND_BUF_LMT         80', "TX buffer")
    content["protocol.h"] = header

    api = content["mcu_api.c"]
    for marker in (
        '请在串口接收中断中调用uart_receive_input(value)',
        '请在需要一次缓存多个字节串口数据处调用此函数',
        '请直接在main函数的while(1){}中添加wifi_uart_service()',
        '请在main函数中添加wifi_protocol_init()',
    ):
        matching = [line for line in api.splitlines(keepends=True)
                    if line.startswith('#error') and marker in line]
        if len(matching) != 1:
            raise ValueError(f"missing mandatory SDK marker: {marker}")
        api = api.replace(matching[0], '', 1)
    api = replace_once(api,
                       'rx_value_len > sizeof(wifi_data_process_buf) + PROTOCOL_HEAD',
                       'rx_value_len > sizeof(wifi_data_process_buf)',
                       'frame length bound')
    content["mcu_api.c"] = api

    system = content["system.c"]
    system = replace_once(system, '#include "wifi.h"',
                          '#include "wifi.h"\n#include "bridge.h"', "system bridge include")
    system = replace_once(system,
                          '    if (dp_type != download_cmd[index].dp_type) {',
                          '    if (index >= get_download_cmd_total()) return FALSE;\n'
                          '    if (dp_type != download_cmd[index].dp_type) {',
                          'unknown DP bound')
    system = replace_once(system,
                          '    switch (cmd_type) {',
                          '    bridge_on_command(cmd_type);\n    switch (cmd_type) {',
                          'RX event hook')
    system = replace_once(system,
                          '        case WIFI_STATE_CMD:  // wifi工作状态\n'
                          '            wifi_work_state = wifi_data_process_buf[offset + DATA_START];',
                          '        case WIFI_STATE_CMD:  // wifi工作状态\n'
                          '            if (total_len < 1) break;\n'
                          '            wifi_work_state = wifi_data_process_buf[offset + DATA_START];',
                          'Wi-Fi state length')
    system = replace_once(system,
                          '        case WIFI_TEST_CMD:  // wifi功能测试（扫描指定路由）\n'
                          '            result = wifi_data_process_buf[offset + DATA_START];',
                          '        case WIFI_TEST_CMD:  // wifi功能测试（扫描指定路由）\n'
                          '            if (total_len < 2) break;\n'
                          '            result = wifi_data_process_buf[offset + DATA_START];',
                          'Wi-Fi test length')
    system = replace_once(system,
                          '            for (i = 0; i < total_len;) {\n'
                          '                dp_len = wifi_data_process_buf[offset + DATA_START + i + 2] * 0x100;\n'
                          '                dp_len += wifi_data_process_buf[offset + DATA_START + i + 3];',
                          '            for (i = 0; i < total_len;) {\n'
                          '                if (total_len - i < 4) break;\n'
                          '                dp_len = wifi_data_process_buf[offset + DATA_START + i + 2] * 0x100;\n'
                          '                dp_len += wifi_data_process_buf[offset + DATA_START + i + 3];\n'
                          '                if (dp_len > total_len - i - 4) break;',
                          'DP payload length')
    content["system.c"] = system

    for name, source in content.items():
        (output / name).write_text(source, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    port(args.snapshot, args.output)
