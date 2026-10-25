#include "product.h"
#include "product_motor.h"
#include "motion_config.h"
#include "motor.h"
#include "motor_tune.h"
#include "music_scores.h"
#include "main.h"
#include "tim.h"
#include "adc.h"
#include "usart.h"
#include "eeprom.h"
#include "bridge.h"
#include "wifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef fake_a, fake_b, fake_c;
TIM_TypeDef fake_timer;
TIM_HandleTypeDef htim14 = {&fake_timer};
ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart1 = {USART1}, huart2 = {USART2};
uint32_t fake_primask;
static uint32_t now, edge_us, ee_reads, ee_writes, music_reads;
static uint8_t ee[2048], transmitted[8192], console;
static unsigned tx_length;
static bool adc_error, music_read_error, observe_music, adc_stall, probe_stall, ee_write_error;
static bool probe_up_reverse, release_on_confirmation, torque_plateau;
static unsigned low_probe_samples;
static uint32_t observed_music_pulses;
static uint16_t adc_free_raw = 600U, adc_stall_raw = 115U;
static int32_t observed_music_offset;
extern volatile uint32_t bridge_rejected_commands, bridge_rx_frames;
extern volatile uint32_t bridge_percent_rx_count, bridge_percent_accepted;
extern volatile uint32_t bridge_calibration_rx_count, bridge_calibration_saved;
extern volatile uint8_t bridge_calibration_result;
extern volatile uint8_t bridge_calibration_pending;
extern volatile uint32_t bridge_calibration_reset_tx;

uint32_t HAL_GetTick(void) { return now; }
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
void Error_Handler(void) { assert(!"Error_Handler"); }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{ (void)adc; (void)timeout; return adc_error ? HAL_TIMEOUT : HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
    (void)adc;
    if (torque_plateau) return 2880U; /* Actual first-probe plateau with observed mechanical stall. */
    bool probe_up = probe_stall && !!(fake_a.IDR & DRV_DIR_Pin) == probe_up_reverse;
    LiftFeedback current; ProductMotor_Feedback(&current);
    bool low = adc_stall || (probe_up && (fake_a.IDR & LOW_CURRENT_SEL_Pin));
    if (LIFT_PROBE_HZ > 18000U && probe_up && current.running_ms >= 900U && !(fake_a.IDR & LOW_CURRENT_SEL_Pin))
        return 2880U; /* High-current high-speed stall is not yet distinguishable. */
    if (release_on_confirmation && low) {
        LiftFeedback feedback; ProductMotor_Feedback(&feedback);
        if (feedback.running_ms >= LIFT_RAMP_MS + LIFT_SETTLE_MS && ++low_probe_samples == 17U) {
            fake_c.IDR = KEY_UP_Pin | KEY_DOWN_Pin | NETCONTROL_BTN_Pin;
            Product_TickISR(); /* Release arrives inside the ADC acquisition. */
        }
    }
    return low ? adc_stall_raw : adc_free_raw;
}
bool Eeprom_Read(uint16_t address, uint8_t *data, uint16_t length)
{
    if ((address == 0U || address == 0x100U) && length == 208U) {
        ++music_reads;
        if (music_read_error) return false;
    } else {
        assert(address >= 0x200U && address + length <= 0x220U && length == 16U);
        ++ee_reads;
    }
    memcpy(data, ee + address, length); return true;
}
bool Eeprom_Update(uint16_t address, const uint8_t *data, uint16_t length)
{
    assert(address >= 0x200U && address + length <= 0x220U && length == 16U);
    ++ee_writes; memcpy(ee + address, data, length); return true;
}
bool Eeprom_UpdateLiftRecord(uint16_t address, const uint8_t record[16])
{
    assert((address == 0x200U || address == 0x210U) && !Motor_Active());
    ++ee_writes;
    if (ee_write_error) return false;
    memcpy(ee + address, record, 16U); return true;
}
bool Eeprom_WriteScoreSlot(unsigned slot, const uint8_t record[208])
{
    (void)slot; (void)record;
    assert(!"Startup must never save/provision a music record");
    return false;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t size, uint32_t timeout)
{
    (void)timeout;
    if (uart == &huart1) {
        assert(tx_length + size < sizeof(transmitted));
        memcpy(transmitted + tx_length, bytes, size); tx_length += size;
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t size, uint32_t timeout)
{
    (void)uart; (void)size; (void)timeout;
    if (!console) return HAL_TIMEOUT;
    *bytes = console; console = 0U; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte, uint16_t size)
{ (void)uart; (void)byte; (void)size; return HAL_OK; }

static void apply_outputs(void)
{
    GPIO_TypeDef *ports[] = {GPIOA, GPIOB};
    const uint32_t masks[] = {DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin,
                              DRV_ENABLE_Pin | DRV_NSLEEP_Pin};
    for (unsigned i = 0; i < 2U; ++i) {
        GPIO_TypeDef *port = ports[i];
        port->ODR &= ~(port->BRR | (port->BSRR >> 16));
        port->ODR |= port->BSRR & 0xffffU;
        port->BSRR = port->BRR = 0U;
        port->IDR = (port->IDR & ~masks[i]) | (port->ODR & masks[i]);
    }
}

static void tick(unsigned milliseconds, bool poll)
{
    apply_outputs();
    while (milliseconds--) {
        ++now;
        Product_TickISR(); apply_outputs();
        Motor_TickISR(); apply_outputs();
        if (fake_timer.CR1 & TIM_CR1_CEN) {
            edge_us += 1000U;
            while ((fake_timer.CR1 & TIM_CR1_CEN) && edge_us >= fake_timer.ARR + 1U) {
                edge_us -= fake_timer.ARR + 1U;
                uint32_t before = fake_a.ODR;
                bool sounding = ProductMotor_MusicActive();
                ProductMotor_TimerISR(); apply_outputs();
                if (observe_music && sounding && !(before & DRV_STEP_Pin) &&
                    (fake_a.ODR & DRV_STEP_Pin)) {
                    ++observed_music_pulses;
                    observed_music_offset += before & DRV_DIR_Pin ? -1 : 1;
                    assert(observed_music_offset >= 0 && observed_music_offset <= 5);
                }
            }
        } else edge_us = 0U;
        if (poll) { Motor_KeepAlive(); Product_Poll(); apply_outputs(); }
    }
}

static uint32_t count(void)
{
    LiftFeedback f; ProductMotor_Feedback(&f); return f.count;
}

static void send_frame(uint8_t command, const uint8_t *payload, unsigned length, bool corrupt)
{
    uint8_t frame[128] = {0x55, 0xaa, 0x00, command, (uint8_t)(length >> 8), (uint8_t)length};
    assert(length + 7U <= sizeof(frame));
    if (length) memcpy(frame + 6, payload, length);
    uint8_t sum = 0;
    for (unsigned i = 0; i < length + 6U; ++i) sum += frame[i];
    frame[length + 6U] = corrupt ? (uint8_t)(sum ^ 0xffU) : sum;
    for (unsigned i = 0; i < length + 7U; ++i) uart_receive_input(frame[i]);
    wifi_uart_service();
}

static void protocol_cases(void)
{
    bridge_init();
    const uint8_t online = 4U;
    send_frame(3U, &online, 1, false);
    assert(wifi_work_state == 4U);
    unsigned before = bridge_rx_frames;
    send_frame(8U, NULL, 0, false);
    assert(!Product_Busy() && bridge_rx_frames > before);
    /* DP101 initial state is 50, without starting a calibration. */
    bool found_position = false;
    for (unsigned i = 0; i + 14U < tx_length; ++i) {
        if (transmitted[i] == 0x55 && transmitted[i + 1] == 0xaa && transmitted[i + 3] == 7U &&
            transmitted[i + 6] == 101U && transmitted[i + 7] == 2U) {
            assert(transmitted[i + 13] == 50U); found_position = true;
        }
    }
    assert(found_position);
    const uint8_t unlearned_cal[] = {102U, 1U, 0U, 1U, 1U};
    unsigned cal_writes = ee_writes;
    send_frame(6U, unlearned_cal, sizeof(unlearned_cal), false);
    assert(!Product_Busy() && ee_writes == cal_writes);
    assert(bridge_calibration_result == PRODUCT_CAL_UNAVAILABLE);
    const uint8_t bad_dp[] = {2, 2, 0, 4, 0, 0, 0, 101};
    send_frame(6, bad_dp, sizeof(bad_dp), false);
    assert(!Product_Busy());
    const uint8_t open_dp[] = {1, 4, 0, 1, 0};
    send_frame(6, open_dp, sizeof(open_dp), true);
    assert(!Product_Busy());
    send_frame(6, open_dp, 3, false); /* truncated DP header */
    assert(!Product_Busy());
    const uint8_t unknown_dp[] = {255, 2, 0, 4, 0, 0, 0, 0};
    send_frame(6, unknown_dp, sizeof(unknown_dp), false);
    assert(!Product_Busy());
    const uint8_t percent_dp[] = {2, 2, 0, 4, 0, 0, 0, 37};
    send_frame(6, percent_dp, sizeof(percent_dp), false);
    assert(!Product_Busy()); /* Direction and range are not yet taught by the local switch. */
    const uint8_t stop_dp[] = {1, 4, 0, 1, 1};
    send_frame(6, stop_dp, sizeof(stop_dp), false);
    uint32_t stopped = count(); tick(100, true);
    assert(!Product_Busy() && count() == stopped);
    send_frame(6, open_dp, sizeof(open_dp), false);
    assert(!Product_Busy()); /* A remote command cannot assign the first local UP contact. */
    const uint8_t offline = 2U;
    send_frame(3, &offline, 1, false); bridge_poll();
    stopped = count(); tick(100, true);
    assert(!Product_Busy() && count() == stopped);
    /* Disabled bench console inputs must never request motion. */
    const char *disabled = "FBTH";
    while (*disabled) { console = (uint8_t)*disabled++; bridge_poll(); assert(!Product_Busy()); }
    puts("PASS real Tuya SDK: initial 50, queries, malformed DP/checksum, STOP; remote movement rejected until local direction learning");
}

static void input_and_irq_cases(void)
{
    uint32_t before_writes = ee_writes, before_reads = ee_reads;
    /* Stable center arms the switch, a hold starts, first release stops in ISR. */
    tick(40, true);
    fake_c.IDR &= ~KEY_UP_Pin;
    tick(29, true); assert(!Motor_Active());
    tick(1, true); assert(Product_Busy());
    tick(400, true); assert(count() > 0U);
    fake_c.IDR |= KEY_UP_Pin;
    tick(1, false); assert(!Motor_Active());
    uint32_t stopped = count(); tick(10, true);
    assert(!Product_Busy() && count() == stopped);
    /* Direct reversal without a center dwell must not restart. */
    fake_c.IDR &= ~KEY_DOWN_Pin;
    tick(40, true); assert(!Motor_Active());
    fake_c.IDR |= KEY_DOWN_Pin;
    tick(40, true);
    assert(Product_Request(100U, PRODUCT_REMOTE)); tick(400, true);
    fake_c.IDR &= ~KEY_DOWN_Pin;
    tick(30, true); assert(Product_Busy() && Product_Target() == 100U); /* PC7 was learned as UP. */
    assert(!Product_Request(100U, PRODUCT_REMOTE)); /* local priority */
    fake_c.IDR &= ~KEY_UP_Pin;
    tick(1, false); assert(!Motor_Active());
    tick(10, true); assert(!Product_Busy());
    fake_c.IDR |= KEY_UP_Pin | KEY_DOWN_Pin;
    tick(40, true);
    assert(Product_Request(100U, PRODUCT_REMOTE)); tick(400, true);
    tick(251, false); /* KeepAlive absent: actual motor SysTick guard */
    LiftFeedback f; ProductMotor_Feedback(&f);
    assert(f.fault && f.error == LIFT_MAIN_STALL && !Motor_Active());
    Product_Poll(); assert(!Product_Busy());
    assert(Product_Request(100U, PRODUCT_REMOTE)); tick(20, true);
    fake_b.IDR &= ~DRV_NFAULT_Pin; tick(1, false);
    ProductMotor_Feedback(&f); assert(f.fault && f.error == LIFT_DRIVER_FAULT);
    fake_b.IDR |= DRV_NFAULT_Pin; Product_Poll(); tick(10, true);
    assert(Product_Request(100U, PRODUCT_REMOTE));
    adc_error = true; tick(10, true);
    assert(!Motor_Active() && !Product_Busy()); adc_error = false;
    assert(ee_reads == before_reads && ee_writes == before_writes);
    puts("PASS real product runtime: debounce, release ISR, conflict, local priority, main watchdog, nFAULT, ADC failure, idle EEPROM access");
}

static void motor_limits(void)
{
    Product_Stop(LIFT_USER_STOP);
    LiftDrive drive = {.generation = 1000U, .run = true, .hz = LIFT_DOWN_HZ,
                       .steps = 317U, .timeout_ms = 2000U};
    assert(ProductMotor_Start(&drive));
    for (unsigned i = 0; i < 1000U && Motor_Active(); ++i) { Motor_KeepAlive(); tick(1, false); }
    LiftFeedback f; ProductMotor_Feedback(&f);
    assert(f.done && f.count == 317U && !(fake_timer.CR1 & TIM_CR1_CEN));
    drive.steps = 0; drive.timeout_ms = 20U;
    assert(ProductMotor_Start(&drive));
    for (unsigned i = 0; i < 20U; ++i) { Motor_KeepAlive(); tick(1, false); }
    ProductMotor_Feedback(&f);
    assert(f.fault && f.error == LIFT_TIMEOUT && !(fake_timer.DIER & TIM_DIER_UIE));
    Motor_Stop();
    /* Run long enough to reach the steady 360-rpm timer configuration in the
     * real motor implementation, then verify STOP blocks subsequent edges. */
    drive.hz = LIFT_PROBE_HZ;
    drive.low_current = true;
    drive.timeout_ms = 2000U;
    assert(ProductMotor_Start(&drive));
    for (unsigned i = 0; i < 1000U; ++i) { Motor_KeepAlive(); tick(1, false); }
    ProductMotor_Feedback(&f);
    assert(f.running_ms < LIFT_RAMP_MS && !(fake_a.ODR & LOW_CURRENT_SEL_Pin));
    for (unsigned i = 0; i < 200U; ++i) { Motor_KeepAlive(); tick(1, false); }
    ProductMotor_Feedback(&f);
    /* 1 s ramp plus ~0.2 s at 360 rpm yields ~6700 rising edges. */
    assert(f.active && !f.fault && fake_timer.ARR == 51U &&
           f.count > 6300U && f.count < 7100U);
    assert(fake_a.ODR & LOW_CURRENT_SEL_Pin);
    Motor_Stop();
    uint32_t stopped = count(); tick(20, false);
    assert(!Motor_Active() && count() == stopped);
    puts("PASS actual motor driver: exact pulse limit and independent segment timeout");
    puts("PASS 360 rpm steady timer half-period 52 us; STOP blocks further steps");
    puts("PASS sensing-current profile: working current through full ramp, low current only at steady speed before ADC window");
}

static uint32_t record_crc(const uint8_t *bytes)
{
    uint32_t crc = 0xffffffffU;
    for (unsigned i = 0; i < 12U; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

static void storage_and_boot_held(void)
{
    uint32_t before_reads = ee_reads, before_writes = ee_writes;
    /* Independently construct a valid older slot and a torn newer slot. */
    uint8_t record[16] = {'L', 'W', LIFT_CALIBRATION_VERSION, 31U, 7, 0, 0, 0, 0, 125, 0, 0};
    uint32_t crc = record_crc(record);
    for (unsigned i = 0; i < 4; ++i) record[12 + i] = (uint8_t)(crc >> (8 * i));
    memcpy(ee + 0x200, record, 16);
    memcpy(ee + 0x210, record, 16);
    ee[0x214] = 8; /* sequence changed without a complete CRC commit */
    Motor_Init();
    fake_c.IDR = KEY_DOWN_Pin | NETCONTROL_BTN_Pin; /* UP held during boot */
    Product_Init();
    tick(100, true);
    assert(!Motor_Active() && !Product_Busy() && Product_Percent() == 50U);
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "range=32000 known=1/0 dir=1/1"));
    fake_c.IDR = KEY_UP_Pin | KEY_DOWN_Pin | NETCONTROL_BTN_Pin;
    tick(40, true);
    fake_c.IDR &= ~KEY_DOWN_Pin;
    tick(30, true); assert(Product_Busy());
    Product_Report(report, sizeof(report));
    assert(strstr(report, "phase=3 ") && strstr(report, "map=2 mode=1"));
    fake_c.IDR |= KEY_DOWN_Pin; tick(40, true);
    assert(!Product_Busy() && ee_writes == before_writes && ee_reads == before_reads + 2U);
    puts("PASS persistence: torn-newer CRC fallback, boot-held interlock, saved switch/motor mapping, no re-probe or idle writes");
}

static void provision_score(unsigned address, const char *text, uint32_t sequence)
{
    uint8_t record[208] = {'M', 'S', 'C', '1', 1U};
    record[5] = (uint8_t)strlen(text);
    memcpy(record + 16, text, strlen(text) + 1U);
    for (unsigned i = 0; i < 4U; ++i) record[8 + i] = (uint8_t)(sequence >> (8U * i));
    uint32_t crc = 0xffffffffU;
    for (unsigned i = 4; i < 208U; ++i) {
        if (i >= 12U && i < 16U) continue;
        crc ^= record[i];
        for (unsigned b = 0; b < 8U; ++b)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4U; ++i) record[12 + i] = (uint8_t)(crc >> (8U * i));
    memcpy(ee + address, record, sizeof(record));
}

static void startup_music(void)
{
    assert(MotorTune_SelfTest());
    uint32_t started = now;
    observe_music = true;
    Product_PlayStartup();
    assert(Motor_Active() && ProductMotor_MusicActive() && !Product_Busy());
    assert(music_reads == 2U && ee_reads == 2U && !ee_writes);
    assert(fake_a.BRR & LOW_CURRENT_SEL_Pin);
    while (Motor_Active() && (uint32_t)(now - started) < 1400U) tick(1U, true);
    ProductMusicFeedback music; ProductMotor_MusicFeedback(&music);
    assert(music.state == PRODUCT_MUSIC_DONE && music.offset == 0);
    assert(music.pulses == observed_music_pulses && observed_music_offset == 0);
    assert(music.pulses > 3100U && music.pulses < 3700U);
    assert((uint32_t)(now - started) >= 1177U && (uint32_t)(now - started) <= 1190U);
    assert(!Motor_Active() && !Product_Busy() && count() == 0U && Product_Percent() == 50U);
    assert(!(fake_a.ODR & (DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin)));
    assert(!(fake_b.ODR & (DRV_ENABLE_Pin | DRV_NSLEEP_Pin)));
    observe_music = false;
    Product_PlayStartup(); tick(5000U, true);
    assert(!Motor_Active() && music_reads == 2U && !ee_writes);
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "boot=1 music=2/") && strstr(report, "map=0 mode=0"));
    puts("PASS louder B1 once: same 1177 ms score, working-current setting, +/-5 microsteps, balanced edges, no lift steps or EEPROM writes/retries");
}

static void music_preemption(void)
{
    uint32_t before_writes = ee_writes;
    ProductMusicFeedback music;
    Product_Stop(LIFT_USER_STOP); tick(40U, true);
    assert(!Motor_PlayText(MUSIC_STARTUP_B1)); tick(50U, true);
    Product_Stop(LIFT_USER_STOP);
    ProductMotor_MusicFeedback(&music);
    uint32_t pulses = music.pulses; tick(40U, true);
    ProductMotor_MusicFeedback(&music);
    assert(music.state == PRODUCT_MUSIC_STOPPED && music.pulses == pulses && !Motor_Active());

    assert(!Motor_PlayText(MUSIC_STARTUP_B1)); tick(50U, true);
    assert(Product_Request(0U, PRODUCT_REMOTE)); tick(10U, true);
    assert(Product_Busy() && Motor_Active() && !ProductMotor_MusicActive());
    assert(!(fake_a.ODR & LOW_CURRENT_SEL_Pin)); /* DOWN starts at working current. */
    Product_Stop(LIFT_USER_STOP); tick(40U, true);

    assert(!Motor_PlayText(MUSIC_STARTUP_B1)); tick(50U, true);
    fake_c.IDR &= ~KEY_UP_Pin;
    tick(1U, false); assert(!Motor_Active()); /* Raw key cancels music in SysTick. */
    tick(29U, true); assert(Product_Busy() && !ProductMotor_MusicActive());
    fake_c.IDR |= KEY_UP_Pin; tick(1U, false); assert(!Motor_Active());
    tick(40U, true); assert(!Product_Busy());

    assert(!Motor_PlayText(MUSIC_STARTUP_B1)); tick(10U, true);
    fake_b.IDR &= ~DRV_NFAULT_Pin; tick(1U, false);
    ProductMotor_MusicFeedback(&music);
    assert(music.state == PRODUCT_MUSIC_FAULT && !Motor_Active());
    fake_b.IDR |= DRV_NFAULT_Pin;
    Product_Stop(LIFT_USER_STOP); tick(40U, true);

    assert(!Motor_PlayText(MUSIC_STARTUP_B1)); tick(251U, false);
    ProductMotor_MusicFeedback(&music);
    assert(music.state == PRODUCT_MUSIC_FAULT && !Motor_Active());
    Product_Stop(LIFT_USER_STOP); tick(40U, true);
    assert(music_reads == 2U && ee_writes == before_writes);
    puts("PASS startup music: STOP, local key, motion preemption, normal-current restore, nFAULT and main-loop timeout");
}

static void release_key(void)
{
    fake_c.IDR = KEY_UP_Pin | KEY_DOWN_Pin | NETCONTROL_BTN_Pin;
    tick(40U, true);
}

static void learn_switch_and_endpoints(void)
{
    char report[384];
    release_key();
    probe_stall = true;
    fake_c.IDR &= ~KEY_DOWN_Pin; /* First actual toggle is PC7, therefore UP. */
    tick(430U, true); assert(Product_Target() == 100U && Product_Busy());
    assert(!(fake_a.ODR & LOW_CURRENT_SEL_Pin)); /* Full current during breakaway/ramp. */
    release_key();
    assert(!Product_Busy() && !ee_writes);
    Product_Report(report, sizeof(report)); assert(strstr(report, "map=2 mode=0"));

    fake_c.IDR &= ~KEY_UP_Pin; /* Opposite key retries detection, retaining the first UP mapping. */
    for (unsigned i = 0; i < 4000U && !ee_writes; ++i) tick(1U, true);
    assert(ee_writes == 1U && ee[0x210] == 'L' && ee[0x211] == 'W');
    assert(ee[0x212] == LIFT_CALIBRATION_VERSION && ee[0x213] == 2U); /* Direction only; PC7 up. */
    Product_Report(report, sizeof(report));
    assert(strstr(report, "dir=1/0") && strstr(report, "map=2 mode=1 ends=0/0 saved=1"));
    const ProductProbeTrace *a = Product_ProbeTrace(0U), *b = Product_ProbeTrace(1U);
    assert(a && b && !Product_ProbeTrace(3U));
    assert(a->result == PRODUCT_TRACE_STALL && b->result == PRODUCT_TRACE_FREE);
    assert(a->minimum == 115U && a->maximum == 600U && a->used > 100U);
    assert(b->minimum == 600U && b->maximum == 600U && b->used > 100U);
    for (unsigned i = 1; i < a->used; ++i)
        assert((a->samples[i] >> 12) - (a->samples[i - 1U] >> 12) >= 10U);
    assert(a->used <= PRODUCT_PROBE_TRACE_SAMPLES && b->used <= PRODUCT_PROBE_TRACE_SAMPLES);
    probe_stall = false;
    tick(1450U, true);
    assert(Product_Busy() && Product_Target() == 0U && fake_timer.ARR == 51U);
    assert(fake_a.ODR & DRV_DIR_Pin);
    assert(fake_a.ODR & LOW_CURRENT_SEL_Pin);
    const ProductProbeTrace *working = Product_ProbeTrace(2U);
    assert(working->result == PRODUCT_TRACE_RUNNING && working->used > 100U);
    release_key(); assert(!Product_Busy() && ee_writes == 1U);

    Motor_Init(); Product_Init(); release_key(); /* Reload direction-only record, without range. */
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1230U, true);
    assert(Product_Busy() && Product_Target() == 100U && fake_timer.ARR == 51U);
    assert(!(fake_a.ODR & DRV_DIR_Pin));
    assert(!(fake_a.ODR & LOW_CURRENT_SEL_Pin)); /* UP retains 1.50 A. */
    Product_Report(report, sizeof(report)); assert(strstr(report, "phase=3 "));
    release_key(); assert(ee_writes == 1U);

    adc_stall = true;
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1330U, true);
    assert(!Product_Busy() && Product_Percent() == 100U && ee_writes == 2U);
    Product_Report(report, sizeof(report)); assert(strstr(report, "ends=1/0 saved=1"));
    release_key(); adc_stall = false;
    fake_c.IDR &= ~KEY_UP_Pin; tick(2030U, true);
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && Product_Percent() == 0U && ee_writes == 3U);
    working = Product_ProbeTrace(2U);
    assert(working->result == PRODUCT_TRACE_STALL && working->used == PRODUCT_PROBE_TRACE_SAMPLES);
    for (unsigned i = 1; i < working->used; ++i) {
        unsigned previous = (working->next + i - 1U) % PRODUCT_PROBE_TRACE_SAMPLES;
        unsigned current = (working->next + i) % PRODUCT_PROBE_TRACE_SAMPLES;
        assert((working->samples[current] >> 12) - (working->samples[previous] >> 12) >= 10U);
    }
    Product_Report(report, sizeof(report)); assert(strstr(report, "known=1/1") && strstr(report, "ends=1/1 saved=1"));
    release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(100U, true);
    assert(!Motor_Active() && ee_writes == 3U); /* Repeated known endpoint is RAM-only. */
    release_key();
    puts("PASS first PC7 toggle=UP; release cancels; opposite-key retry retains mapping; immediate direction-only EEPROM save/reload; UP/DOWN360 with separate currents; separate endpoint saves; no repeated writes");
}

static void working_protocol(void)
{
    const uint8_t online = 4U;
    send_frame(3U, &online, 1U, false);
    const uint8_t percent_dp[] = {2, 2, 0, 4, 0, 0, 0, 37};
    send_frame(6U, percent_dp, sizeof(percent_dp), false);
    assert(Product_Busy() && Product_Target() == 37U);
    for (unsigned i = 0; i < 5000U && Product_Busy(); ++i) tick(1U, true);
    assert(!Product_Busy() && Product_Percent() == 37U);
    const uint8_t open_dp[] = {1, 4, 0, 1, 0};
    send_frame(6U, open_dp, sizeof(open_dp), false); tick(100U, true);
    const uint8_t stop_dp[] = {1, 4, 0, 1, 1};
    send_frame(6U, stop_dp, sizeof(stop_dp), false);
    uint32_t stopped = count(); tick(100U, true);
    assert(!Product_Busy() && count() == stopped);
    send_frame(6U, open_dp, sizeof(open_dp), false); tick(100U, true);
    const uint8_t offline = 2U;
    send_frame(3U, &offline, 1U, false); bridge_poll(); tick(10U, true);
    assert(!Product_Busy() && !Motor_Active());
    puts("PASS working real Tuya SDK: learned 37% target, STOP and offline stopping");
}

static void direction_save_failure(void)
{
    Motor_Init(); Product_Init(); release_key();
    probe_stall = ee_write_error = true;
    fake_c.IDR &= ~KEY_UP_Pin;
    for (unsigned i = 0; i < 4000U && !ee_writes; ++i) tick(1U, true);
    assert(ee_writes == 1U && !Motor_Active() && !Product_Busy());
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "error=10") && strstr(report, "map=1 mode=1") && strstr(report, "saved=0"));
    tick(2000U, true); assert(ee_writes == 1U); /* No periodic retry. */
    release_key(); ee_write_error = probe_stall = false;
    fake_c.IDR &= ~KEY_UP_Pin; tick(31U, true);
    assert(ee_writes == 2U);
    Product_Report(report, sizeof(report));
    assert(strstr(report, "phase=3 ") && strstr(report, "saved=1"));
    release_key();
    puts("PASS failed direction write stops; no automatic retry; next explicit key retries save without re-probing");
}

static void constant_current_capture(void)
{
    uint8_t record[16] = {'L', 'W', LIFT_CALIBRATION_VERSION, 31U, 7, 0, 0, 0, 0, 125, 0, 0};
    uint32_t crc = record_crc(record);
    for (unsigned i = 0; i < 4U; ++i) record[12U + i] = (uint8_t)(crc >> (8U * i));
    memcpy(ee + 0x200U, record, sizeof(record));
    provision_score(0U, MUSIC_STARTUP_B1, 7U);
    uint8_t before[sizeof(ee)]; memcpy(before, ee, sizeof(before));
    Motor_Init(); Product_Init(); Product_PlayStartup(); tick(1400U, true); release_key();
    assert(!Motor_Active() && !Product_Busy() && ee_reads == 2U && music_reads == 2U && !ee_writes);
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "range=0 known=0/0 dir=1/0") && strstr(report, "map=0 mode=1"));
    assert(strstr(report, "capture=1") && strstr(report, "saved=0"));
    assert(!Product_Request(100U, PRODUCT_REMOTE));
    assert(Product_Calibrate(true) == PRODUCT_CAL_UNAVAILABLE && !ee_writes);
    adc_free_raw = 180U; adc_stall_raw = 16U;
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1830U, true);
    assert(Product_Busy() && fake_timer.ARR == 51U);
    assert(!(fake_a.ODR & (DRV_DIR_Pin | LOW_CURRENT_SEL_Pin)));
    Product_Report(report, sizeof(report));
    assert(strstr(report, "phase=3 ") && strstr(report, "map=2 mode=1"));
    assert(!Product_Request(0U, PRODUCT_REMOTE));
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && !Motor_Active() && Product_Percent() == 100U && !ee_writes);
    assert(Product_ProbeTrace(2U)->result == PRODUCT_TRACE_STALL);
    ProductProbeTrace top_trace = *Product_ProbeTrace(0U);
    assert(top_trace.result == PRODUCT_TRACE_STALL && top_trace.generation == Product_ProbeTrace(2U)->generation);
    release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(100U, true); release_key();
    assert(Product_ProbeTrace(2U)->result == PRODUCT_TRACE_CANCELED);
    assert(!memcmp(Product_ProbeTrace(0U), &top_trace, sizeof(top_trace)));
    fake_c.IDR &= ~KEY_UP_Pin; tick(1830U, true);
    assert(Product_Busy() && (fake_a.ODR & DRV_DIR_Pin) && !(fake_a.ODR & LOW_CURRENT_SEL_Pin));
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && Product_Percent() == 0U && !ee_writes);
    assert(Product_ProbeTrace(1U)->result == PRODUCT_TRACE_STALL);
    assert(!memcmp(Product_ProbeTrace(0U), &top_trace, sizeof(top_trace)));
    Product_Report(report, sizeof(report)); assert(strstr(report, "known=1/1"));
    release_key(); tick(2000U, true);
    assert(!ee_writes && ee_reads == 2U && music_reads == 2U && !memcmp(ee, before, sizeof(ee)));
    puts("PASS constant-current capture: confirmed local direction, first toggle=UP, full-current 360 both ways, synthetic 180 normal / 16 stalled input, endpoint stopping; automatic UP/DOWN traces survive manual taps; remote rejected and all EEPROM bytes untouched");
}

static void confirmed_direction_product(void)
{
    /* Neither the old experimental format nor a conflicting orientation may
     * redefine this user's confirmed assembly or its first UP key. */
    for (unsigned slot = 0U; slot < 2U; ++slot) {
        uint8_t old[16] = {'L', 'W', slot ? LIFT_CALIBRATION_VERSION : 6U, 1U, 1U};
        uint32_t crc = record_crc(old);
        for (unsigned i = 0U; i < 4U; ++i) old[12U + i] = (uint8_t)(crc >> (8U * i));
        memcpy(ee + 0x200U + slot * 16U, old, sizeof(old));
    }
    provision_score(0U, MUSIC_STARTUP_B1, 7U);
    Motor_Init(); Product_Init(); Product_PlayStartup(); tick(1400U, true); release_key();
    assert(!Motor_Active() && !Product_Busy() && !ee_writes && ee_reads == 2U && music_reads == 2U);
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "dir=1/0") && strstr(report, "map=0 mode=1"));
    assert(strstr(report, "capture=0") && strstr(report, "saved=0"));
    assert(!Product_Request(100U, PRODUCT_REMOTE));
    bridge_init();

    adc_free_raw = 180U; adc_stall_raw = 16U;
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(31U, true);
    assert(ee_writes == 1U && !Motor_Active()); /* Direction/key verified before starting. */
    Product_Report(report, sizeof(report));
    assert(strstr(report, "phase=3 ") && strstr(report, "map=2 mode=1") && strstr(report, "saved=1"));
    tick(2200U, true);
    assert(Product_Busy() && fake_timer.ARR == 51U);
    assert(!(fake_a.ODR & (DRV_DIR_Pin | LOW_CURRENT_SEL_Pin)));
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && !Motor_Active() && Product_Percent() == 100U && ee_writes == 2U);
    Product_Report(report, sizeof(report)); assert(strstr(report, "ends=1/0 saved=1"));
    release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(2030U, true);
    assert(Product_Busy() && (fake_a.ODR & DRV_DIR_Pin) && !(fake_a.ODR & LOW_CURRENT_SEL_Pin));
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && Product_Percent() == 0U && ee_writes == 3U);
    Product_Report(report, sizeof(report)); assert(strstr(report, "known=1/1") && strstr(report, "ends=1/1 saved=1"));
    assert(ee[0x212U] == LIFT_CALIBRATION_VERSION && ee[0x213U] == 30U);
    release_key(); tick(2000U, true);
    assert(ee_reads == 2U && music_reads == 2U && ee_writes == 3U);

    Motor_Init(); Product_Init(); release_key();
    Product_Report(report, sizeof(report));
    assert(strstr(report, "known=1/0 dir=1/0") && strstr(report, "map=2 mode=1") && strstr(report, "saved=1"));
    assert(Product_Percent() == 50U && ee_writes == 3U);
    assert(Product_Request(37U, PRODUCT_REMOTE)); /* v11 references TOP before the saved-range target. */
    Product_Stop(LIFT_USER_STOP); release_key();
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1830U, true);
    assert(Product_Busy() && !(fake_a.ODR & (DRV_DIR_Pin | LOW_CURRENT_SEL_Pin)));
    adc_stall = true; tick(100U, true); adc_stall = false;
    assert(!Product_Busy() && Product_Percent() == 100U && ee_writes == 3U);
    release_key();
    working_protocol();
    assert(ee_writes == 3U);

    fake_c.IDR &= ~KEY_DOWN_Pin;
    Motor_Init(); Product_Init(); tick(100U, true);
    assert(!Motor_Active()); /* Boot-held remains interlocked with saved mapping. */
    release_key(); fake_c.IDR &= ~KEY_DOWN_Pin; tick(100U, true);
    assert(Motor_Active());
    fake_c.IDR = KEY_UP_Pin | KEY_DOWN_Pin | NETCONTROL_BTN_Pin; Product_TickISR(); apply_outputs();
    assert(!Motor_Active());
    uint32_t stopped = count(); tick(100U, true);
    assert(!Product_Busy() && count() == stopped && ee_writes == 3U);
    puts("PASS confirmed Product: no probes, first key=UP saved before motion, full-current360 both ways, filtered endpoints and range saved, v6/conflicting records ignored, reboot mapping/range restored, no repeated writes, boot-held/release protected");
}

static void confirmed_direction_write_failure(void)
{
    Motor_Init(); Product_Init(); release_key(); ee_write_error = true;
    fake_c.IDR &= ~KEY_UP_Pin; tick(31U, true);
    char report[384]; Product_Report(report, sizeof(report));
    assert(!Motor_Active() && !Product_Busy() && ee_writes == 1U);
    assert(strstr(report, "error=10") && strstr(report, "map=1 mode=1") && strstr(report, "saved=0"));
    tick(2000U, true); assert(ee_writes == 1U && !Motor_Active());
    release_key(); ee_write_error = false;
    fake_c.IDR &= ~KEY_UP_Pin; tick(31U, true);
    Product_Report(report, sizeof(report));
    assert(ee_writes == 2U && strstr(report, "phase=3 ") && strstr(report, "saved=1"));
    puts("PASS confirmed Product save failure: no motor start or automatic retry; next explicit toggle retries direction/key save without probes");
}

static void calibration_tap(void)
{
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; tick(80U, true);
    fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);
}

static void manual_product_calibration(void)
{
    uint8_t old[16] = {'L', 'W', 7U, 30U, 7U, 0U, 0U, 0U, 0x80U, 0x38U, 1U, 0U}; /* 80000 */
    uint32_t crc = record_crc(old);
    for (unsigned i = 0U; i < 4U; ++i) old[12U + i] = (uint8_t)(crc >> (8U * i));
    memcpy(ee + 0x200U, old, sizeof(old));
    Motor_Init(); Product_Init(); release_key();
    char report[384]; Product_Report(report, sizeof(report));
    assert(strstr(report, "range=80000 known=1/0") && strstr(report, "saved=1") && !ee_writes);
    calibration_tap(); Product_Report(report, sizeof(report));
    assert(strstr(report, "cal=3 last=0") && !ee_writes); /* No preceding switch input. */
    adc_free_raw = 180U; adc_stall_raw = 16U;
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(100U, true); release_key();
    calibration_tap(); Product_Report(report, sizeof(report));
    assert(Product_Percent() == 100U && strstr(report, "manual=1 cal=1 last=1") && ee_writes == 1U);

    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(2030U, true);
    assert(Motor_Active());
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; tick(1U, false);
    assert(!Motor_Active()); /* Stops at raw press, not after debounce/release. */
    uint32_t at_press = count(); tick(79U, true);
    assert(!Product_Request(100U, PRODUCT_REMOTE) && count() == at_press);
    fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);
    Product_Report(report, sizeof(report));
    assert(!Motor_Active() && Product_Percent() == 0U && strstr(report, "manual=3 cal=2 last=2"));
    assert(ee_writes == 2U && ee[0x202U] == LIFT_CALIBRATION_VERSION && ee[0x203U] == 126U);
    tick(300U, true); assert(!Motor_Active() && count() == at_press); /* Held switch cannot restart. */

    release_key(); fake_c.IDR &= ~KEY_DOWN_Pin;
    for (unsigned i = 0U; i < 10000U && (i < 31U || Product_Busy()); ++i) tick(1U, true);
    assert(!Product_Busy() && Product_Percent() == 100U && ee_writes == 2U);
    assert(Product_ProbeTrace(0U)->result == PRODUCT_TRACE_LIMIT);
    release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(30U, true);
    assert(Product_Busy()); /* Relative local limits work before absolute reference learning. */
    for (unsigned i = 0U; i < 10000U && Product_Busy(); ++i) tick(1U, true);
    assert(!Product_Busy() && Product_Percent() == 0U && ee_writes == 2U);
    assert(Product_ProbeTrace(1U)->result == PRODUCT_TRACE_LIMIT);
    /* ADC never stalled: both stops used the actual driver's pulse limit. */
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(1830U, true);
    assert(Product_Busy() && Motor_Active() && count() > 4799U && ee_writes == 2U);
    release_key(); calibration_tap();
    assert(ee_writes == 3U && Product_Percent() == 0U);
    Product_Report(report, sizeof(report)); assert(strstr(report, "manual=3 cal=2 last=2"));
    unsigned writes_before = ee_writes;
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; tick(5040U, true);
    assert(Product_PairingRequested() && !Product_PairingRequested());
    fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);
    assert(ee_writes == writes_before && !Product_PairingRequested());
    Motor_Init(); Product_Init(); release_key();
    Product_Report(report, sizeof(report));
    assert(strstr(report, "known=1/0") && strstr(report, "manual=3 cal=0 last=0") && strstr(report, "saved=1"));
    calibration_tap(); assert(ee_writes == writes_before);
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; Motor_Init(); Product_Init(); tick(6000U, true);
    fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);
    assert(!Product_PairingRequested() && ee_writes == writes_before);
    puts("PASS real calibration input: v7 migration without writes, no-input rejection, immediate press stop, UP/DOWN EEPROM8 teaching, held-switch interlock,pulse-limited endpoints with no stall,continuous held adjustment,long/AP excludes short,manual flags restored and boot-held button suppressed");
}

static void load_manual_product(void)
{
    const uint32_t travel = 196823U;
    uint8_t record[16] = {'L', 'W', 9U, 126U, 6U}; /* Known physical TOP offset0. */
    for (unsigned i = 0U; i < 4U; ++i) record[8U + i] = (uint8_t)(travel >> (8U * i));
    uint32_t crc = record_crc(record);
    for (unsigned i = 0U; i < 4U; ++i) record[12U + i] = (uint8_t)(crc >> (8U * i));
    memcpy(ee + 0x200U, record, sizeof(record));
    adc_free_raw = 180U; adc_stall_raw = 16U; adc_stall = false;
    Motor_Init(); Product_Init(); release_key();
    assert(!ee_writes && Product_Percent() == 50U);
}

static void confirm_current_top(void)
{
    assert(Product_Request(100U, PRODUCT_REMOTE));
    tick(1500U, true); adc_stall = true;
    for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
    adc_stall = false;
    tick(250U, true);
    assert(!Product_Busy() && Product_Percent() == 100U && !ee_writes);
}

static bool report_has(const char *text)
{
    char report[512]; Product_Report(report, sizeof(report));
    return strstr(report, text) != NULL;
}

static int32_t reported_position(void)
{
    char report[512]; Product_Report(report, sizeof(report));
    const char *field = strstr(report, " pos=");
    long position;
    assert(field && sscanf(field, " pos=%ld", &position) == 1);
    return (int32_t)position;
}

static void finish_motion(void)
{
    for (unsigned i = 0U; i < 40000U && Product_Busy(); ++i) tick(1U, true);
    assert(!Product_Busy() && !Motor_Active());
}

static void latched_toggle(void)
{
    load_manual_product(); confirm_current_top();
    fake_c.IDR &= ~KEY_UP_Pin; tick(30U, true); tick(999U, true);
    assert(Motor_Active() && report_has("auto=0"));
    fake_c.IDR |= KEY_UP_Pin; tick(1U, false);
    assert(!Motor_Active()); tick(40U, true);
    assert(!Product_Busy()); /* 999 ms after movement starts is still momentary. */

    fake_c.IDR &= ~KEY_UP_Pin; tick(30U, true); tick(1000U, true);
    assert(report_has("auto=1"));
    uint32_t before = count(); release_key(); tick(1500U, true);
    assert(Product_Busy() && Motor_Active() && count() > before);
    assert(!Product_Opening());
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1U, false);
    assert(!Motor_Active()); before = count();
    tick(999U, true); assert(!Motor_Active() && count() == before);
    tick(1U, true); assert(Motor_Active() && Product_Opening());
    tick(999U, true); assert(report_has("auto=0"));
    fake_c.IDR |= KEY_DOWN_Pin; tick(1U, false);
    assert(!Motor_Active()); tick(40U, true);
    assert(!Product_Busy()); /* Reversed movement gets its own 1 s latch timer. */

    fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true); release_key(); tick(1500U, true);
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1001U, true);
    assert(Motor_Active() && report_has("auto=0"));
    tick(1000U, true); assert(report_has("auto=1"));
    release_key(); assert(Motor_Active()); finish_motion();
    assert(Product_Percent() == 100U && Product_ProbeTrace(0U)->result == PRODUCT_TRACE_LIMIT);
    assert(!ee_writes);

    /* A sub-second opposite input only stops; release cancels the reversal. */
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true); release_key();
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(400U, true);
    assert(!Motor_Active()); release_key(); tick(1500U, true);
    assert(!Product_Busy() && !Motor_Active());

    /* Opposite contact interrupts even between ADC sampling and its result. */
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true); release_key();
    fake_c.IDR &= ~(KEY_UP_Pin | KEY_DOWN_Pin); tick(1U, false);
    assert(!Motor_Active()); tick(100U, true); assert(!Product_Busy());
    tick(1500U, true); assert(!Motor_Active()); release_key();

    /* Holding through an endpoint cannot start the explicit adjustment mode. */
    fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true); finish_motion();
    assert(Product_Percent() == 0U); before = count(); tick(1500U, true);
    assert(!Product_Busy() && !Motor_Active() && count() == before);
    release_key();
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1030U, true); release_key();
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; tick(1U, false);
    assert(!Motor_Active());
    tick(79U, true); fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);
    assert(!Product_Busy() && !Motor_Active() && report_has("auto=0"));
    tick(1000U, true); assert(!Motor_Active()); release_key();

    /* Unsigned hold/reversal timing across HAL tick wrap. */
    now = 0xfffffe00U;
    fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true);
    assert(report_has("auto=1")); release_key();
    assert(Product_Busy());
    fake_b.IDR &= ~DRV_NFAULT_Pin; tick(1U, true);
    fake_b.IDR |= DRV_NFAULT_Pin; tick(1500U, true);
    assert(!Product_Busy() && !Motor_Active()); release_key();
    puts("PASS latched toggle:999/1000ms, release continues, raw opposite stops within1ms,999/1000ms reversal wait, fresh1s latch, canceled reversal, held endpoint cannot jog,button cancels auto,conflict/fault stop and tick wrap");
}

static void continuous_adjustment(void)
{
    load_manual_product(); confirm_current_top(); release_key();
    /* At upper, explicit second movement is continuous manual UP even at the
     * previously learned physical coordinate; no silent direction reversal. */
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(3530U, true);
    assert(Product_Busy() && Motor_Active() && count() > 15000U);
    assert(Product_Opening() && report_has("auto=0") && reported_position() < -4799);
    uint32_t stopped = count(); fake_c.IDR |= KEY_DOWN_Pin; tick(1U, false);
    assert(!Motor_Active()); tick(100U, true);
    assert(!Product_Busy() && count() == stopped && !ee_writes);

    /* Moving back into range remains hold-to-run throughout the same press. */
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(6030U, true);
    assert(Product_Busy() && Motor_Active() && reported_position() > 0);
    assert(!Product_Opening() && report_has("auto=0"));
    stopped = count(); fake_c.IDR |= KEY_UP_Pin; tick(1U, false);
    assert(!Motor_Active()); tick(100U, true);
    assert(!Product_Busy() && count() == stopped && !ee_writes);
    assert(Product_Request(0U, PRODUCT_REMOTE)); finish_motion(); release_key();
    assert(Product_Percent() == 0U);

    /* Lower adjustment also exceeds the old quota; another press while already
     * outside resumes immediately and never arms the1s automatic latch. */
    fake_c.IDR &= ~KEY_UP_Pin; tick(6030U, true);
    assert(Motor_Active() && count() > 40000U && reported_position() > 196823 + 4799);
    assert(report_has("auto=0") && !ee_writes);
    release_key(); int32_t beyond = reported_position();
    fake_c.IDR &= ~KEY_UP_Pin; tick(630U, true);
    assert(Motor_Active() && reported_position() > beyond && report_has("auto=0"));

    /* A raw opposite input stops within1ms, keeps the existing1s reversal wait,
     * then remains manual after crossing back through the saved lower limit. */
    fake_c.IDR |= KEY_UP_Pin; fake_c.IDR &= ~KEY_DOWN_Pin;
    tick(1U, false); assert(!Motor_Active()); stopped = count();
    tick(999U, true); assert(!Motor_Active() && count() == stopped);
    tick(1U, true); assert(Motor_Active() && Product_Opening());
    tick(7000U, true);
    assert(Product_Busy() && Motor_Active() && reported_position() < 196823);
    assert(report_has("auto=0"));
    stopped = count(); fake_c.IDR |= KEY_DOWN_Pin; tick(1U, false);
    assert(!Motor_Active()); tick(100U, true);
    assert(!Product_Busy() && count() == stopped && !ee_writes);

    /* App soft endpoints still stop on their pulse limit, and actual TRQ/
     * driver faults can still stop a held adjustment without saving endpoints. */
    assert(Product_Request(100U, PRODUCT_REMOTE)); finish_motion(); release_key();
    assert(Product_Percent() == 100U && !ee_writes);
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(2030U, true);
    adc_stall = true; tick(120U, true); adc_stall = false;
    assert(!Motor_Active() && !Product_Busy() && !ee_writes);
    stopped = count(); tick(1200U, true); assert(count() == stopped && !Motor_Active());
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(100U, true);
    assert(Motor_Active()); fake_b.IDR &= ~DRV_NFAULT_Pin; tick(1U, true);
    assert(!Motor_Active()); fake_b.IDR |= DRV_NFAULT_Pin;
    tick(1200U, true); assert(!Motor_Active() && !ee_writes); release_key();
    puts("PASS continuous boundary adjustment: held UP/DOWN exceeds4799/15000/40000 pulses,no1s latch,release stops within1ms,repeat while outside,opposite stops+1s reversal,return into range stays manual; App limits/TRQ/nFAULT retained,no automatic EEPROM writes");
}

static uint32_t dp_report(uint8_t id)
{
    uint32_t result = UINT32_MAX;
    unsigned at = 0U;
    while (at + 7U <= tx_length) {
        assert(transmitted[at] == 0x55U && transmitted[at + 1U] == 0xaaU);
        unsigned length = ((unsigned)transmitted[at + 4U] << 8) | transmitted[at + 5U];
        assert(at + length + 7U <= tx_length);
        uint8_t sum = 0U;
        for (unsigned i = 0U; i < length + 6U; ++i) sum += transmitted[at + i];
        assert(sum == transmitted[at + length + 6U]);
        if (transmitted[at + 3U] == 7U && length >= 5U && transmitted[at + 6U] == id) {
            unsigned bytes = ((unsigned)transmitted[at + 8U] << 8) | transmitted[at + 9U];
            assert(bytes <= 4U && bytes + 4U == length);
            result = 0U;
            for (unsigned i = 0U; i < bytes; ++i) result = (result << 8) | transmitted[at + 10U + i];
        }
        at += length + 7U;
    }
    return result;
}

static void cloud_control(uint8_t value)
{
    tx_length = 0U;
    uint8_t dp[] = {1U, 4U, 0U, 1U, value};
    send_frame(6U, dp, sizeof(dp), false);
}

static void cloud_percent(uint32_t value)
{
    tx_length = 0U;
    uint8_t dp[] = {2U, 2U, 0U, 4U, (uint8_t)(value >> 24), (uint8_t)(value >> 16),
                    (uint8_t)(value >> 8), (uint8_t)value};
    send_frame(6U, dp, sizeof(dp), false);
}

static void cloud_product(void)
{
    load_manual_product(); bridge_init();
    const uint8_t online = 4U, offline = 2U;
    send_frame(3U, &online, 1U, false);
    tx_length = 0U; send_frame(8U, NULL, 0U, false);
    assert(dp_report(1U) == 1U && dp_report(2U) == 50U && dp_report(101U) == 50U);
    cloud_percent(101U); assert(!Product_Busy());
    cloud_percent(37U);
    assert(Product_Busy() && Product_Target() == 37U && Product_Opening());
    assert(dp_report(2U) == 37U && dp_report(101U) == 50U);
    tick(200U, true); uint32_t generation = Product_ProbeTrace(2U)->generation;
    uint32_t before = count(); cloud_percent(62U); tick(100U, true);
    assert(Product_Target() == 62U && count() > before && Product_ProbeTrace(2U)->generation == generation);
    tick(1500U, true); adc_stall = true;
    for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
    adc_stall = false;
    assert(Product_Busy() && !Motor_Active() && Product_Percent() == 100U);
    assert(report_has("home=2"));
    tx_length = 0U; send_frame(8U, NULL, 0U, false);
    assert(Product_Target() == 62U && dp_report(2U) == 62U && dp_report(101U) == 100U);
    tick(199U, true); assert(!Motor_Active());
    tick(1U, true); assert(Motor_Active() && !Product_Opening());
    finish_motion(); assert(Product_Percent() == 62U && !ee_writes);

    cloud_control(0U); tick(400U, true);
    assert(Product_Busy() && Product_Opening());
    cloud_control(1U); before = count(); tick(300U, true);
    assert(!Product_Busy() && count() == before && dp_report(1U) == 1U);
    assert(dp_report(2U) == 100U && dp_report(101U) == Product_Percent());
    cloud_control(2U); finish_motion(); tx_length = 0U; bridge_poll();
    assert(Product_Percent() == 0U && dp_report(2U) == 0U && dp_report(101U) == 0U);
    cloud_control(0U); finish_motion(); tx_length = 0U; bridge_poll();
    assert(Product_Percent() == 100U && dp_report(2U) == 100U && dp_report(101U) == 100U);
    cloud_percent(37U); finish_motion();
    assert(Product_Percent() == 37U && !ee_writes);

    /* A remote goal may replace latched local motion with the toggle centered. */
    release_key(); fake_c.IDR &= ~KEY_UP_Pin; tick(1030U, true); release_key();
    cloud_percent(62U); assert(Product_RemoteActive()); finish_motion();
    assert(Product_Percent() == 62U);
    cloud_control(2U); tick(1500U, true);
    fake_c.IDR &= ~KEY_DOWN_Pin; tick(1U, false); assert(!Motor_Active());
    tick(999U, true); assert(!Motor_Active());
    tick(1U, true); assert(Motor_Active() && Product_Opening());
    release_key(); assert(!Product_Busy());

    cloud_control(0U); tick(100U, true);
    send_frame(3U, &offline, 1U, false); bridge_poll(); tick(300U, true);
    assert(!Product_Busy() && !Motor_Active());
    send_frame(3U, &online, 1U, false);

    /* Cancel between reference search and target: never execute a stale goal. */
    Product_Stop(LIFT_USER_STOP); Motor_Init(); Product_Init(); release_key();
    cloud_percent(37U); tick(1500U, true); adc_stall = true;
    for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
    adc_stall = false; assert(report_has("home=2"));
    cloud_control(1U); tick(1000U, true);
    assert(!Product_Busy() && !Motor_Active() && Product_Percent() == 100U);

    /* A stall while seeking an intermediate percentage is an obstruction. */
    cloud_percent(37U); tick(1800U, true); adc_stall = true; tick(120U, true); adc_stall = false;
    assert(!Product_Busy() && !Motor_Active() && report_has("error=6"));
    assert(report_has("known=1/0") && !ee_writes);
    puts("PASS real Tuya frames: DP1 open/stop/close, DP2 accepted target and DP101 position, reboot TOP reference then62%,retarget without restart,200ms dwell,canceled home,37% pulse positioning,0/100 soft limits,local/remote takeover,link-loss stop,partial obstruction and unchanged EEPROM");
}

static void finish_with_progress(uint8_t target, bool up)
{
    unsigned previous = Product_Percent(), intermediate = 0U;
    for (unsigned i = 0U; i < 400U && Product_Busy(); ++i) {
        tick(100U, true); tx_length = 0U; bridge_poll();
        assert(Product_Target() == target);
        uint32_t position = dp_report(101U);
        if (position == UINT32_MAX) continue;
        assert(dp_report(2U) == target && position == Product_Percent());
        assert(up ? position >= previous : position <= previous);
        if (position != target) ++intermediate;
        previous = position;
    }
    assert(!Product_Busy() && Product_Percent() == target && intermediate >= 3U);
    assert(dp_report(2U) == target && dp_report(101U) == target);
}

static void slider_progress(void)
{
    load_manual_product(); confirm_current_top(); bridge_init();
    const uint8_t online = 4U;
    send_frame(3U, &online, 1U, false);
    bridge_poll();
    cloud_percent(61U);
    assert(Product_Target() == 61U && dp_report(2U) == 61U);
    assert(dp_report(101U) == Product_Percent() && dp_report(101U) != 61U);
    tick(1800U, true); tx_length = 0U; bridge_poll();
    unsigned at = Product_Percent();
    assert(at < 99U && at > 61U && dp_report(2U) == 61U && dp_report(101U) == at);
    tx_length = 0U; tick(499U, true); bridge_poll();
    assert(Product_Percent() < at && !tx_length); /* Same state/goal: wait500ms. */
    tick(1U, true); bridge_poll();
    assert(dp_report(2U) == 61U && dp_report(101U) == Product_Percent());

    uint32_t received = bridge_percent_rx_count, accepted = bridge_percent_accepted;
    LiftFeedback before, after; ProductMotor_Feedback(&before);
    for (unsigned i = 0U; i < 10U; ++i) bridge_all_data_update();
    ProductMotor_Feedback(&after);
    assert(Product_Target() == 61U && before.generation == after.generation && before.count == after.count);
    assert(bridge_percent_rx_count == received && bridge_percent_accepted == accepted);
    finish_with_progress(61U, false);

    cloud_percent(27U); assert(Product_Target() == 27U && dp_report(2U) == 27U && dp_report(101U) == 61U);
    finish_with_progress(27U, false);
    cloud_percent(61U); assert(Product_Target() == 61U && dp_report(2U) == 61U && dp_report(101U) == 27U);
    finish_with_progress(61U, true);

    cloud_control(0U); bridge_poll(); tick(1800U, true); tx_length = 0U; bridge_poll();
    assert(Product_Busy() && dp_report(101U) > 61U && dp_report(101U) < 100U);
    assert(dp_report(2U) == 100U);
    cloud_control(1U);
    assert(!Product_Busy() && dp_report(2U) == 100U && dp_report(101U) == Product_Percent());
    cloud_control(2U); bridge_poll(); tick(1800U, true); tx_length = 0U; bridge_poll();
    assert(Product_Busy() && dp_report(101U) > 0U && dp_report(101U) < 100U);
    assert(dp_report(2U) == 0U);
    assert(bridge_percent_rx_count == received + 2U && bridge_percent_accepted == accepted + 2U && !ee_writes);
    puts("PASS separate App controls: DP2 retains accepted target even on STOP,DP101 monotonic actual progress,500ms boundary,immediate final/STOP,61->27->61 and open/close; reports never re-enter downloads or restart motor; no EEPROM writes");
}

static void cloud_calibrate(uint8_t id, uint8_t value)
{
    tx_length = 0U;
    uint8_t dp[] = {id, 1U, 0U, 1U, value};
    send_frame(6U, dp, sizeof(dp), false);
}

static void cloud_calibration(void)
{
    load_manual_product(); confirm_current_top();
    bridge_init();
    const uint8_t online = 4U, offline = 2U;
    send_frame(3U, &online, 1U, false);
    unsigned reads = ee_reads, writes = ee_writes;
    tx_length = 0U; send_frame(8U, NULL, 0U, false);
    assert(dp_report(102U) == 0U && dp_report(103U) == 0U);
    cloud_calibrate(102U, 0U); cloud_calibrate(103U, 0U);
    assert(Product_Percent() == 100U && !Product_Busy() && reads == ee_reads && writes == ee_writes);
    const uint8_t wrong_type[] = {102U, 2U, 0U, 4U, 0U, 0U, 0U, 1U};
    const uint8_t wrong_length[] = {103U, 1U, 0U, 2U, 1U, 0U};
    const uint8_t read_only[] = {101U, 2U, 0U, 4U, 0U, 0U, 0U, 100U};
    send_frame(6U, wrong_type, sizeof(wrong_type), false);
    send_frame(6U, wrong_length, sizeof(wrong_length), false);
    send_frame(6U, read_only, sizeof(read_only), false);
    cloud_calibrate(102U, 2U);
    assert(!Product_Busy() && Product_Percent() == 100U && writes == ee_writes);

    /* No previous physical toggle is needed: explicit UP establishes the
     * post-reset position, retaining the verified range without a write. */
    cloud_calibrate(102U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_UPPER && bridge_calibration_saved == 1U);
    assert(Product_Percent() == 100U && Product_Target() == 100U && !ee_writes);
    assert(report_has("last=0") && dp_report(102U) == 1U);
    cloud_percent(0U);
    while (count() < 2500U) tick(1U, true);
    unsigned final_count = count();
    cloud_calibrate(103U, 0U); assert(Motor_Active());
    cloud_calibrate(103U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_LOWER && Product_Percent() == 0U);
    assert(!Product_Busy() && !Motor_Active() && ee_writes == 1U && count() == final_count);
    char range_text[40]; snprintf(range_text, sizeof(range_text), "range=%u ", final_count);
    assert(report_has(range_text) && report_has("saved=1"));
    assert(dp_report(2U) == 0U && dp_report(101U) == 0U && dp_report(103U) == 1U);
    /* Explicit lower remains lower within three turns of the old upper. */
    assert(final_count >= 1600U && final_count < 4800U);
    reads = ee_reads; writes = ee_writes;
    cloud_calibrate(103U, 1U); tick(1500U, true);
    assert(bridge_calibration_result == PRODUCT_CAL_LOWER && !Motor_Active());
    assert(reads == ee_reads && writes == ee_writes); /* Unchanged is RAM-only. */
    cloud_calibrate(102U, 1U); /* Same place as bottom: zero span, never flip. */
    assert(bridge_calibration_result == PRODUCT_CAL_INVALID && report_has(range_text));
    assert(ee_writes == writes);

    /* Stops and consumes a held local latch; center is required to re-arm. */
    cloud_control(0U); finish_motion(); release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(30U, true);
    while (count() < 1800U) tick(1U, true);
    cloud_calibrate(103U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_LOWER && !Motor_Active());
    final_count = count(); tick(2000U, true);
    assert(!Product_Busy() && count() == final_count && report_has("auto=0"));
    release_key(); writes = ee_writes;
    send_frame(3U, &offline, 1U, false);
    cloud_calibrate(102U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_UNAVAILABLE && writes == ee_writes);
    send_frame(3U, &online, 1U, false);
    fake_c.IDR &= ~NETCONTROL_BTN_Pin; tick(1U, true);
    cloud_calibrate(102U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_UNAVAILABLE && writes == ee_writes);
    fake_c.IDR |= NETCONTROL_BTN_Pin; tick(40U, true);

    /* Reboot reloads the journal without writes; calibration cancels the
     * entire remote reference search and its pending percentage target. */
    Motor_Init(); Product_Init(); release_key();
    assert(report_has("known=1/0") && report_has("manual=3") && report_has("saved=1"));
    assert(ee_writes == writes);
    cloud_percent(61U); tick(500U, true); assert(report_has("home=1"));
    cloud_calibrate(102U, 1U); final_count = count(); tick(2000U, true);
    assert(bridge_calibration_result == PRODUCT_CAL_UPPER && !Product_Busy() && !Motor_Active());
    assert(count() == final_count && report_has("home=0") && !Product_RemoteActive());

    /* The deliberate unknown-position teaching invalidated the absolute TOP
     * reference. Re-establish it before the following movement tests. */
    cloud_percent(100U); tick(1500U, true); adc_stall = true;
    for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
    adc_stall = false; finish_motion();

    /* An explicit jog extends the range, then a failed EEPROM commit must
     * report failure and never periodically retry or restart the motor. */
    cloud_control(2U); finish_motion(); release_key();
    fake_c.IDR &= ~KEY_UP_Pin; tick(700U, true); release_key();
    ee_write_error = true; writes = ee_writes;
    cloud_calibrate(103U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_STORAGE_ERROR && report_has("saved=0"));
    assert(report_has("error=10") && ee_writes == writes + 1U);
    assert(dp_report(103U) == 1U && !Product_Busy() && !Motor_Active());
    tick(2000U, true); assert(ee_writes == writes + 1U);
    ee_write_error = false;
    cloud_calibrate(103U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_LOWER && report_has("saved=1"));
    assert(ee_writes == writes + 2U && !Motor_Active());
    Motor_Init(); Product_Init(); release_key();
    assert(report_has("manual=3") && report_has("saved=1") && ee_writes == writes + 2U);
    puts("PASS real Bool DP102/103: query/false no-op,malformed type/length/value rejected,explicit upper/lower no local history or near-opposite flip,final-pulse stop and EEPROM,unchanged RAM-only,held toggle consumed,offline/button interlocks,reboot restore,homing canceled,write failure/reset/no auto retry and explicit retry");
}

static void calibration_reset(void)
{
    load_manual_product(); confirm_current_top(); bridge_init();
    const uint8_t online = 4U, offline = 2U;
    send_frame(3U, &online, 1U, false);
    now = 0xffffff00U; /* Timers must also release across tick wrap. */
    unsigned resets = bridge_calibration_reset_tx, writes = ee_writes;
    cloud_calibrate(102U, 1U);
    assert(dp_report(102U) == 1U && bridge_calibration_pending == 1U);
    tick(299U, true); tx_length = 0U; bridge_poll();
    assert(bridge_calibration_reset_tx == resets);
    tick(1U, true); tx_length = 0U; bridge_poll();
    assert(dp_report(102U) == 0U && bridge_calibration_reset_tx == resets + 1U);
    tick(1199U, true); tx_length = 0U; bridge_poll();
    assert(bridge_calibration_reset_tx == resets + 1U);
    tick(1U, true); tx_length = 0U; bridge_poll();
    assert(dp_report(102U) == 0U && bridge_calibration_reset_tx == resets + 2U && !bridge_calibration_pending);
    tick(5000U, true); bridge_poll();
    assert(bridge_calibration_reset_tx == resets + 2U && ee_writes == writes);
    /* Second press is a fresh command. Offline postpones only release reports,
     * never the calibration itself, and reconnect cannot re-execute it. */
    send_frame(3U, &offline, 1U, false);
    cloud_calibrate(103U, 1U);
    assert(bridge_calibration_result == PRODUCT_CAL_UNAVAILABLE && dp_report(103U) == 1U);
    unsigned received = bridge_calibration_rx_count;
    tick(2000U, true); bridge_poll();
    assert(bridge_calibration_reset_tx == resets + 2U && ee_writes == writes);
    send_frame(3U, &online, 1U, false); tx_length = 0U; bridge_poll();
    assert(dp_report(103U) == 0U);
    tick(1200U, true); tx_length = 0U; bridge_poll();
    assert(dp_report(103U) == 0U && !bridge_calibration_pending);
    assert(bridge_calibration_rx_count == received && ee_writes == writes && !Motor_Active());
    tx_length = 0U; send_frame(8U, NULL, 0U, false);
    assert(dp_report(102U) == 0U && dp_report(103U) == 0U);
    puts("PASS momentary calibration reports: press=true,release=false at300ms plus one1200ms follow-up; tick wrap,independent DP state,reconnect,query and repeated click; no periodic spam,EEPROM retry or motion");
}

static void reference_positioning(bool migration)
{
    load_manual_product();
    if (migration) {
        ee[0x202U] = LIFT_CALIBRATION_VERSION;
        uint32_t crc = record_crc(ee + 0x200U);
        for (unsigned i = 0U; i < 4U; ++i) ee[0x20cU + i] = (uint8_t)(crc >> (8U * i));
        Motor_Init(); Product_Init(); release_key();
    } else confirm_current_top();
    bridge_init(); const uint8_t online = 4U;
    send_frame(3U, &online, 1U, false);
    uint32_t span = 196823U;
    char ref_text[40];
    if (migration) {
        assert(report_has("ref=0/0"));
        cloud_percent(37U); assert(!Product_Busy() && !ee_writes);
        cloud_calibrate(102U, 1U); /* Still at the user's old taught upper. */
        assert(Product_Percent() == 100U && !ee_writes);
        cloud_percent(37U); assert(report_has("home=1"));
        tick(1500U, true); adc_stall = true;
        for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
        adc_stall = false;
        assert(report_has("home=2") && ee_writes == 1U && Product_Percent() == 99U);
        /* Packed high offset bits and unchanged span survive reload. */
        uint8_t *record = ee + 0x210U;
        uint32_t lo = 0U, hi = 0U;
        for (unsigned i = 0U; i < 4U; ++i) { lo |= (uint32_t)record[8U+i] << (8U*i); hi |= (uint32_t)record[4U+i] << (8U*i); }
        unsigned offset = (lo >> 19) | ((hi >> 26) << 13);
        assert(record[2] == 9U && (lo & 0x7ffffU) == span && offset > 0U);
        snprintf(ref_text, sizeof(ref_text), "ref=1/%u", offset);
        assert(report_has(ref_text)); finish_motion();
    } else {
        cloud_percent(76U); finish_motion();
        cloud_calibrate(102U, 1U);
        span -= 196823U * 24U / 100U;
        assert(ee_writes == 1U && report_has("ref=1/47237"));
        strcpy(ref_text, "ref=1/47237");
    }
    char span_text[40]; snprintf(span_text, sizeof(span_text), "range=%lu ", (unsigned long)span);
    assert(report_has(span_text));
    unsigned writes = ee_writes;
    Product_Stop(LIFT_USER_STOP); Motor_Init(); Product_Init(); release_key();
    assert(report_has("known=1/0") && report_has(ref_text) && report_has(span_text));
    assert(ee_writes == writes);
    cloud_percent(37U); assert(Product_Busy() && report_has("home=1"));
    tick(1500U, true); adc_stall = true;
    for (unsigned i = 0U; i < 150U && Motor_Active(); ++i) tick(1U, true);
    adc_stall = false;
    assert(report_has("home=2") && Product_Percent() == 99U && !Motor_Active());
    finish_motion(); assert(Product_Percent() == 37U && report_has(ref_text));
    cloud_control(2U); finish_motion();
    cloud_control(0U); finish_motion();
    assert(Product_Percent() == 100U && count() == span && ee_writes == writes);
    assert(report_has(ref_text) && report_has(span_text) && report_has("saved=1"));
    puts(migration ? "PASS LW8 manual-point migration: no assumed physical zero; explicit old upper + one homing learns offset,LW9 preserves span and offset across reset,37% plus both taught endpoints stop correctly" :
        "PASS taught intermediate upper47237: compact LW9 offset/span persistence,reboot physical TOP lies before taught TOP,return downward to37%,both taught soft limits preserved,no ordinary-motion EEPROM writes");
}

int main(int argc, char **argv)
{
    memset(ee, 0xff, sizeof(ee));
    fake_b.IDR = DRV_NFAULT_Pin;
    fake_c.IDR = KEY_UP_Pin | KEY_DOWN_Pin | NETCONTROL_BTN_Pin;
    if (LIFT_TRQ_CAPTURE) { constant_current_capture(); return 0; }
    if (LIFT_CONFIRMED_DIRECTION) {
        if (argc > 1 && !strcmp(argv[1], "manual-calibration")) manual_product_calibration();
        else if (argc > 1 && !strcmp(argv[1], "latched-toggle")) latched_toggle();
        else if (argc > 1 && !strcmp(argv[1], "continuous-adjustment")) continuous_adjustment();
        else if (argc > 1 && !strcmp(argv[1], "cloud-product")) cloud_product();
        else if (argc > 1 && !strcmp(argv[1], "slider-progress")) slider_progress();
        else if (argc > 1 && !strcmp(argv[1], "cloud-calibration")) cloud_calibration();
        else if (argc > 1 && !strcmp(argv[1], "calibration-reset")) calibration_reset();
        else if (argc > 1 && !strcmp(argv[1], "reference-positioning")) reference_positioning(false);
        else if (argc > 1 && !strcmp(argv[1], "reference-migration")) reference_positioning(true);
        else if (argc > 1 && !strcmp(argv[1], "confirmed-write-error")) confirmed_direction_write_failure();
        else { assert(argc == 1); confirmed_direction_product(); }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "torque-plateau")) {
        Motor_Init(); Product_Init(); release_key(); torque_plateau = true;
        fake_c.IDR &= ~KEY_DOWN_Pin; tick(3500U, true);
        char report[384]; Product_Report(report, sizeof(report));
        assert(!Product_Busy() && !Motor_Active() && !ee_writes);
        assert(strstr(report, "error=11") && strstr(report, "map=2 mode=0"));
        const ProductProbeTrace *trace = Product_ProbeTrace(0U);
        assert(trace->result == PRODUCT_TRACE_FREE);
        assert(trace->minimum == 2880U && trace->maximum == 2880U && trace->used > 100U);
        assert((trace->samples[trace->used - 1U] >> 12) < LIFT_PROBE_MS);
        assert(trace->last == 2880U && trace->total >= trace->used);
        assert(Product_ProbeTrace(1U)->result == PRODUCT_TRACE_FAULT &&
               Product_ProbeTrace(1U)->error == LIFT_TRQ_UNUSABLE);
        tick(1000U, true); assert(!ee_writes && !Motor_Active());
        puts("PASS indistinguishable 2880 plateaus: explicit unusable-signal error after both probes, retained bounded RAM samples, no false direction/EEPROM commit");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "release-in-adc")) {
        Motor_Init(); Product_Init(); release_key();
        probe_stall = probe_up_reverse = release_on_confirmation = true;
        fake_c.IDR &= ~KEY_UP_Pin;
        for (unsigned i = 0; i < 4000U && low_probe_samples < 17U; ++i) tick(1U, true);
        assert(low_probe_samples == 17U); tick(10U, true);
        char report[384]; Product_Report(report, sizeof(report));
        assert(!Product_Busy() && !Motor_Active() && !ee_writes && strstr(report, "map=1 mode=0"));
        release_key(); release_on_confirmation = false;
        fake_c.IDR &= ~KEY_UP_Pin; tick(30U, true);
        Product_Report(report, sizeof(report)); assert(strstr(report, "phase=1 "));
        puts("PASS release inside final direction ADC sample prevents stale success/save; next toggle restarts probe A");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "direction-write-error")) {
        direction_save_failure(); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "startup-held")) {
        fake_c.IDR &= ~KEY_UP_Pin;
        Motor_Init(); Product_Init(); Product_PlayStartup(); tick(100U, true);
        assert(!Motor_Active() && !music_reads && !ee_writes);
        fake_c.IDR |= KEY_UP_Pin; tick(40U, true);
        Product_PlayStartup(); tick(1500U, true);
        assert(!Motor_Active() && !music_reads && !ee_writes);
        puts("PASS boot-held key skips music once; release never replays it");
        return 0;
    }
    if (argc > 1) {
        music_read_error = !strcmp(argv[1], "startup-read-error");
        assert(music_read_error || !strcmp(argv[1], "startup-blank"));
        Motor_Init(); Product_Init(); Product_PlayStartup();
        uint32_t reads = music_reads;
        assert(reads == (music_read_error ? 1U : 2U));
        Product_PlayStartup(); tick(5000U, true);
        assert(!Motor_Active() && !Product_Busy() && music_reads == reads && !ee_writes);
        char report[384]; Product_Report(report, sizeof(report));
        assert(strstr(report, "boot=3 music=0/0/0"));
        puts("PASS missing/corrupt score or EEPROM read failure: no playback, writes or automatic retries");
        return 0;
    }
    provision_score(0U, MUSIC_STARTUP_B1, 7U);
    provision_score(0x100U, "c4f4g4", 8U);
    ee[0x105U] ^= 1U; /* A newer torn music slot must not replace the valid B1. */
    Motor_Init(); Product_Init();
    assert(!Product_Busy() && !Motor_Active() && Product_Percent() == 50U && ee_reads == 2U);
    startup_music();
    protocol_cases();
    learn_switch_and_endpoints();
    working_protocol();
    music_preemption();
    input_and_irq_cases();
    motor_limits();
    storage_and_boot_held();
    return 0;
}

