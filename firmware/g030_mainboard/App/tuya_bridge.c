#include "tuya_bridge.h"
#include "main.h"
#include "usart.h"

/* Fixed 9600 8N1 for authorization. No in-band commands: all bytes are data.
 * Poll both receivers before either transmitter; never wait for TX completion.
 * Separate 2048-byte queues absorb bursts, not sustained clock mismatch.
 * CEN reset can produce malformed UART data: count/drop it and keep listening.
 * The flashing tool owns retry/CRC; transport errors must never latch the bridge.
 */
#define QUEUE_SIZE 2048U
typedef struct {
    uint8_t data[QUEUE_SIZE];
    uint32_t head, tail;
} BridgeQueue;
/* Diagnostics reuse queue RAM; no extra 4KB allocation on the 8KB MCU. */
static union {
    struct { BridgeQueue module, host; } queues;
    uint8_t test_data[4096];
} storage;
#define to_module storage.queues.module
#define to_host storage.queues.host
static UART_HandleTypeDef module_uart;

/* SWD-only mailbox. No serial escape commands can corrupt flashing data.
 * mode 0=normal, 1=capture PC bytes (module TX isolated), 2=replay to PC,
 * 3=idle diagnostics, 4=both-queue loopback to PC.
 * host sets mode; firmware acknowledges before host touches test_data.
 * Return to normal with a reset after diagnostics, never reuse test RAM as queues.
 */
volatile uint32_t bridge_test_mode, bridge_test_ack, bridge_test_count;
uint8_t * const bridge_test_buffer = storage.test_data;
typedef struct {
    uint32_t rx, tx, framing, noise, overrun, parity, dropped, high_water;
} BridgeStats;
volatile BridgeStats bridge_stats[2]; /* 0 PC->module, 1 module->PC */
typedef struct { uint32_t side, status, rx, tx; } BridgeError;
volatile BridgeError bridge_errors[16];
volatile uint32_t bridge_error_index;

/* Readable over SWD; deliberately never printed on the binary transport. */
volatile uint32_t tuya_bridge_fault;
volatile uint32_t tuya_bridge_to_module_bytes;
volatile uint32_t tuya_bridge_to_host_bytes;
volatile uint32_t tuya_bridge_rx_errors;
volatile uint32_t tuya_bridge_queue_drops;
volatile uint32_t tuya_bridge_last_error;

static void fail(uint32_t reason)
{
    tuya_bridge_fault = reason;
    HAL_GPIO_WritePin(GPIOB, DRV_ENABLE_Pin | DRV_NSLEEP_Pin, GPIO_PIN_RESET);
    /* LED off = fault. Ready is steady on. */
    HAL_GPIO_WritePin(GPIOB, LED_STATUS_Pin, GPIO_PIN_SET);
    __disable_irq();
    for (;;) { }
}

static void receive(USART_TypeDef *uart, BridgeQueue *queue, uint32_t fault_base)
{
    uint32_t side = (fault_base == 0x100U) ? 0U : 1U;
    volatile BridgeStats *stats = &bridge_stats[side];
    uint32_t status = uart->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        ++tuya_bridge_rx_errors;
        tuya_bridge_last_error = fault_base | (status & 15U);
        stats->framing += !!(status & USART_ISR_FE);
        stats->noise += !!(status & USART_ISR_NE);
        stats->overrun += !!(status & USART_ISR_ORE);
        stats->parity += !!(status & USART_ISR_PE);
        uint32_t index = bridge_error_index++ % 16U;
        bridge_errors[index] = (BridgeError){side, status, stats->rx, stats->tx};
        /* Drop only the suspect byte; queued bytes were already received intact.
         * Purging that queue would silently lose valid data after a CEN glitch. */
        if (status & USART_ISR_RXNE_RXFNE) {
            (void)uart->RDR;
            ++stats->dropped;
        }
        uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        return;
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = (uint8_t)uart->RDR;
        ++stats->rx;
        if (bridge_test_ack == 1U && side == 0U) {
            if (bridge_test_count < 4096U)
                storage.test_data[bridge_test_count++] = byte;
            else ++stats->dropped;
            return;
        }
        uint32_t next = (queue->head + 1U) % QUEUE_SIZE;
        if (next == queue->tail) {
            ++tuya_bridge_queue_drops;
            ++stats->dropped;
            tuya_bridge_last_error = fault_base | 0x10U;
            return;
        }
        queue->data[queue->head] = byte;
        queue->head = next;
        uint32_t used = (queue->head - queue->tail) % QUEUE_SIZE;
        if (used > stats->high_water) stats->high_water = used;
    }
}

static void transmit(USART_TypeDef *uart, BridgeQueue *queue, volatile uint32_t *count)
{
    if (queue->head != queue->tail && (uart->ISR & USART_ISR_TXE_TXFNF)) {
        uart->TDR = queue->data[queue->tail];
        queue->tail = (queue->tail + 1U) % QUEUE_SIZE;
        ++*count;
        ++bridge_stats[uart == USART1 ? 0U : 1U].tx;
    }
}

void TuyaBridge_Run(void)
{
    GPIO_InitTypeDef pins = {0};
    /* MX_GPIO_Init has already established motor disabled/asleep, STEP low.
     * No ADC, I2C, TIM14, music, switch service or EEPROM access is started. */
    MX_USART2_UART_Init();
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    __HAL_RCC_USART1_CLK_ENABLE();
    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &pins);
    module_uart.Instance = USART1;
    module_uart.Init = huart2.Init;
    module_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&module_uart) != HAL_OK) fail(0x300U);

    /* No runtime HAL calls or tick dependency after initialization. */
    __disable_irq();
    HAL_GPIO_WritePin(GPIOB, LED_STATUS_Pin, GPIO_PIN_RESET);
    for (;;) {
        if (bridge_test_mode != 0U) {
            if (bridge_test_ack != bridge_test_mode) {
                bridge_test_count = 0;
                if (bridge_test_mode == 4U) {
                    to_module.head = to_module.tail = 0;
                    to_host.head = to_host.tail = 0;
                }
                bridge_test_ack = bridge_test_mode;
            }
            if (bridge_test_ack == 1U)
                receive(USART2, &to_module, 0x100U);
            else if (bridge_test_ack == 2U && bridge_test_count < 4096U &&
                     (USART2->ISR & USART_ISR_TXE_TXFNF))
                USART2->TDR = storage.test_data[bridge_test_count++];
            else if (bridge_test_ack == 4U) {
                /* SWD-only queue loopback: exercise both rings across wraparound
                 * while keeping all test bytes away from the Tuya module. */
                receive(USART2, &to_module, 0x100U);
                uint32_t next = (to_host.head + 1U) % QUEUE_SIZE;
                if (to_module.head != to_module.tail && next != to_host.tail) {
                    to_host.data[to_host.head] = to_module.data[to_module.tail];
                    to_module.tail = (to_module.tail + 1U) % QUEUE_SIZE;
                    to_host.head = next;
                }
                transmit(USART2, &to_host, &tuya_bridge_to_host_bytes);
            }
            /* Module RX is ignored and module TX never written in diagnostics. */
            continue;
        }
        if (bridge_test_ack != 0U) fail(0x400U); /* reset required after test */
        receive(USART2, &to_module, 0x100U);
        receive(USART1, &to_host, 0x200U);
        transmit(USART1, &to_module, &tuya_bridge_to_module_bytes);
        transmit(USART2, &to_host, &tuya_bridge_to_host_bytes);
    }
}
