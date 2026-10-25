#include "product.h"
#include "product_motor.h"
#include "product_button.h"
#include "motion_config.h"
#include "main.h"
#include "motor.h"
#include "music.h"
#include "adc.h"
#include "eeprom.h"
#include <stdio.h>
#include <string.h>

/* These are operational status, not writable diagnostic / motion-test hooks. */
static LiftMotion lift;
static volatile ProductOwner owner;
static volatile bool initialized, abort_pending, key_owned;
static volatile LiftError abort_error;
static volatile uint8_t pending_key;
static uint8_t candidate, stable_ticks, key_direction;
static bool key_armed, adc_ready;
static volatile bool key_auto;
static volatile uint8_t reverse_key;
static uint32_t key_started, reverse_started;
enum { HOME_NONE, HOME_SEARCH, HOME_WAIT };
static uint8_t remote_home, remote_goal;
static uint32_t home_stopped_at;
static int32_t home_origin;
static bool home_learn_reference;
static bool requested_open;
static uint8_t accepted_target;
static uint32_t applied_generation, sampled_at, saved_sequence;
static uint32_t storage_loads, storage_saves, storage_errors;
static unsigned saved_slot;
static uint16_t last_trq;
/* One startup event per MCU boot: 0 not attempted, 1 started, 2 held key,
 * 3 score/load/start failure. No automatic retry or EEPROM provisioning. */
static uint32_t startup_music_result;
static volatile uint8_t up_key;
static uint8_t saved_flags;
static uint32_t saved_travel;
static uint32_t saved_top_offset;
static bool saved_reference_known;
static bool saved_valid;
static ProductProbeTrace trq_trace[3];
static ProductButton control_button;
static volatile bool button_interlock, pairing_pending;
static volatile uint8_t last_local_key, calibration_key, calibration_pending;
static uint8_t last_calibration;
enum { RECORD_UP_REVERSE = 1U, RECORD_KEY2_UP = 2U,
       RECORD_TOP = 4U, RECORD_BOTTOM = 8U, RECORD_RANGE = 16U,
       RECORD_MANUAL_TOP = 32U, RECORD_MANUAL_BOTTOM = 64U };

const ProductProbeTrace *Product_ProbeTrace(unsigned index)
{
    return index < 3U ? &trq_trace[index] : NULL;
}

static void trace_sample(const LiftFeedback *f)
{
    if (lift.paused || !f->active || !f->adc_valid ||
        (lift.phase != LIFT_PROBE_A && lift.phase != LIFT_PROBE_B && lift.phase != LIFT_MOVE)) return;
    unsigned index = lift.phase == LIFT_MOVE ? 2U : (lift.phase == LIFT_PROBE_B ? 1U : 0U);
    ProductProbeTrace *trace = &trq_trace[index];
    if (trace->generation != f->generation) {
        if (!index) trq_trace[1] = (ProductProbeTrace){0};
        *trace = (ProductProbeTrace){.generation = f->generation,
            .minimum = 4095U, .result = PRODUCT_TRACE_RUNNING};
    }
    trace->count = f->count;
    trace->last = f->trq;
    if (f->trq < trace->minimum) trace->minimum = f->trq;
    if (f->trq > trace->maximum) trace->maximum = f->trq;
    if (trace->total < UINT16_MAX) ++trace->total;
    unsigned previous = trace->next ? trace->next - 1U : PRODUCT_PROBE_TRACE_SAMPLES - 1U;
    if (!trace->used || f->running_ms - (trace->samples[previous] >> 12) >= 10U) {
        trace->samples[trace->next] = (f->running_ms << 12) | f->trq;
        trace->next = (uint16_t)((trace->next + 1U) % PRODUCT_PROBE_TRACE_SAMPLES);
        if (trace->used < PRODUCT_PROBE_TRACE_SAMPLES) ++trace->used;
    }
}

static void trace_stop(LiftError error)
{
    bool working = trq_trace[2].result == PRODUCT_TRACE_RUNNING;
    for (unsigned i = 0; i < 3U; ++i) {
        if (trq_trace[i].result != PRODUCT_TRACE_RUNNING) continue;
        trq_trace[i].result = error == LIFT_OK || error == LIFT_USER_STOP ?
            PRODUCT_TRACE_CANCELED : PRODUCT_TRACE_FAULT;
        trq_trace[i].error = (uint8_t)error;
    }
    if (LIFT_FULL_CURRENT_PROFILE && working && trq_trace[2].result == PRODUCT_TRACE_FAULT)
        trq_trace[lift.moving_up ? 0U : 1U] = trq_trace[2];
}

static void trace_finish(LiftPhase phase, bool done)
{
    if (phase != LIFT_PROBE_A && phase != LIFT_PROBE_B && phase != LIFT_MOVE) return;
    if (lift.phase == LIFT_FAULT) { trace_stop(lift.error); return; }
    if (phase == LIFT_MOVE) {
        if (!Lift_Busy(&lift)) {
            trq_trace[2].result = lift.endpoint ?
                (done ? PRODUCT_TRACE_LIMIT : PRODUCT_TRACE_STALL) : PRODUCT_TRACE_FREE;
            /* Confirmed Product/capture do not probe: reuse slots for the last automatic
             * UP/DOWN stop, so subsequent short manual taps cannot erase it. */
            if (LIFT_FULL_CURRENT_PROFILE) trq_trace[lift.moving_up ? 0U : 1U] = trq_trace[2];
        }
        return;
    }
    if (!lift.paused) return;
    bool blocked = phase == LIFT_PROBE_A ? lift.probe_a_stalled : lift.up_reverse;
    trq_trace[phase == LIFT_PROBE_B ? 1U : 0U].result = blocked ?
        PRODUCT_TRACE_STALL : PRODUCT_TRACE_FREE;
}

static unsigned keys(void)
{
    uint32_t pins = GPIOC->IDR;
    return (!(pins & KEY_UP_Pin) ? 1U : 0U) | (!(pins & KEY_DOWN_Pin) ? 2U : 0U);
}

static uint32_t crc32(const uint8_t *bytes, unsigned size)
{
    uint32_t crc = 0xffffffffU;
    for (unsigned i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

/* LW9 retains the same verified 16-byte page and CRC. Sequence uses 26 bits;
 * travel and physical-TOP offset use 19 bits each (both bounded to300000). */
#define REFERENCE_RECORD_VERSION 9U
#define SEQUENCE_MASK 0x03ffffffU
static uint32_t record_sequence(const uint8_t *r)
{ return get32(r + 4) & SEQUENCE_MASK; }
static uint32_t record_travel(const uint8_t *r)
{ return r[2] == REFERENCE_RECORD_VERSION ? get32(r + 8) & 0x7ffffU : get32(r + 8); }
static uint32_t record_offset(const uint8_t *r)
{ return (get32(r + 8) >> 19) | ((get32(r + 4) >> 26) << 13); }
static bool sequence_after(uint32_t a, uint32_t b)
{ uint32_t d = (a - b) & SEQUENCE_MASK; return d && d < 0x02000000U; }

static bool valid_record(const uint8_t *record)
{
    uint32_t range = record_travel(record);
    unsigned flags = record[3];
    bool has_range = (flags & RECORD_RANGE) != 0U;
    bool old_v7 = LIFT_CONFIRMED_DIRECTION && record[2] == 7U;
    bool reference = LIFT_CONFIRMED_DIRECTION && record[2] == REFERENCE_RECORD_VERSION;
    return record[0] == 'L' && record[1] == 'W' &&
        (record[2] == LIFT_CALIBRATION_VERSION || old_v7 || reference) && !(flags & ~(old_v7 ? 31U : 127U)) &&
        (!reference || ((flags & RECORD_TOP) && record_offset(record) <= LIFT_MAX_TRAVEL_STEPS - range)) &&
        (!(flags & RECORD_MANUAL_TOP) || (flags & RECORD_TOP)) &&
        (!(flags & RECORD_MANUAL_BOTTOM) || (flags & RECORD_BOTTOM)) &&
        (!LIFT_CONFIRMED_DIRECTION || LIFT_TRQ_CAPTURE ||
            !!(flags & RECORD_UP_REVERSE) == !!LIFT_CONFIRMED_UP_REVERSE) &&
        (has_range ? ((flags & (RECORD_TOP | RECORD_BOTTOM)) == (RECORD_TOP | RECORD_BOTTOM) &&
            range >= LIFT_MIN_TRAVEL_STEPS && range <= LIFT_MAX_TRAVEL_STEPS) : range == 0U) &&
        crc32(record, 12) == get32(record + 12);
}

/* Calibration boot event reads two reserved pages, separate from music loading.
 * Position starts untrusted even when a valid direction/range was recovered. */
static void load_range(void)
{
    uint8_t record[2][16];
    bool valid[2] = {false, false};
    for (unsigned slot = 0; slot < 2; ++slot) {
        ++storage_loads;
        if (Eeprom_Read((uint16_t)(0x200U + slot * 16U), record[slot], 16U))
            valid[slot] = valid_record(record[slot]);
        else ++storage_errors;
    }
    if (!valid[0] && !valid[1]) return;
    saved_slot = valid[1] && (!valid[0] ||
        sequence_after(record_sequence(record[1]), record_sequence(record[0]))) ? 1U : 0U;
    saved_sequence = record_sequence(record[saved_slot]);
    saved_flags = record[saved_slot][3];
    saved_travel = record_travel(record[saved_slot]);
    saved_reference_known = record[saved_slot][2] == REFERENCE_RECORD_VERSION ||
        ((saved_flags & RECORD_TOP) && !(saved_flags & RECORD_MANUAL_TOP));
    saved_top_offset = record[saved_slot][2] == REFERENCE_RECORD_VERSION ? record_offset(record[saved_slot]) : 0U;
    lift.physical_top_known = saved_reference_known;
    lift.physical_top_offset = saved_top_offset;
    saved_valid = true;
    up_key = saved_flags & RECORD_KEY2_UP ? 2U : 1U;
    lift.travel = saved_travel;
    lift.up_reverse = (saved_flags & RECORD_UP_REVERSE) != 0U;
    lift.top_known = (saved_flags & RECORD_TOP) != 0U;
    lift.bottom_known = (saved_flags & RECORD_BOTTOM) != 0U;
    lift.range_known = (saved_flags & RECORD_RANGE) != 0U;
    lift.manual_mask = (uint8_t)(((saved_flags & RECORD_MANUAL_TOP) ? 1U : 0U) |
                              ((saved_flags & RECORD_MANUAL_BOTTOM) ? 2U : 0U));
    lift.direction_known = true;
}

static uint8_t record_flags(void)
{
    return (lift.up_reverse ? RECORD_UP_REVERSE : 0U) |
        (up_key == 2U ? RECORD_KEY2_UP : 0U) |
        (lift.top_known ? RECORD_TOP : 0U) | (lift.bottom_known ? RECORD_BOTTOM : 0U) |
        (lift.range_known ? RECORD_RANGE : 0U) |
        (lift.manual_mask & 1U ? RECORD_MANUAL_TOP : 0U) |
        (lift.manual_mask & 2U ? RECORD_MANUAL_BOTTOM : 0U);
}

static bool state_saved(void)
{
    return saved_valid && saved_flags == record_flags() &&
        saved_travel == (lift.range_known ? lift.travel : 0U) &&
        saved_reference_known == lift.physical_top_known &&
        saved_top_offset == lift.physical_top_offset;
}

/* One attempt per completed event, motor disabled; unchanged state is RAM-only. */
static void save_range(void)
{
    lift.save_pending = 0U;
    if (LIFT_TRQ_CAPTURE) return; /* Measurement build never commits direction/endpoints. */
    if (!lift.direction_known || !up_key || state_saved()) return;
    uint8_t flags = record_flags();
    uint32_t travel = lift.range_known ? lift.travel : 0U;
    uint8_t record[16] = {'L', 'W', LIFT_CALIBRATION_VERSION, flags};
    uint32_t next = (saved_sequence + 1U) & SEQUENCE_MASK;
    put32(record + 4, next);
    put32(record + 8, travel);
    if (LIFT_CONFIRMED_DIRECTION && lift.physical_top_known && (lift.manual_mask & 1U)) {
        record[2] = REFERENCE_RECORD_VERSION;
        put32(record + 4, next | ((lift.physical_top_offset >> 13) << 26));
        put32(record + 8, travel | ((lift.physical_top_offset & 0x1fffU) << 19));
    }
    put32(record + 12, crc32(record, 12));
    unsigned slot = saved_slot ^ 1U;
    ++storage_saves;
    if (Eeprom_UpdateLiftRecord((uint16_t)(0x200U + slot * 16U), record)) {
        saved_slot = slot;
        saved_sequence = next;
        saved_flags = flags; saved_travel = travel; saved_valid = true;
        saved_reference_known = lift.physical_top_known;
        saved_top_offset = lift.physical_top_offset;
    } else {
        ++storage_errors;
        Lift_Stop(&lift, LIFT_STORAGE_FAULT);
    }
}

static void synchronize(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    /* Release/conflict/communication IRQ cannot be followed by a stale start. */
    if (!abort_pending && lift.drive.generation != applied_generation) {
        Motor_Stop();
        applied_generation = lift.drive.generation;
        if (lift.drive.run && !ProductMotor_Start(&lift.drive))
            Lift_Stop(&lift, LIFT_DRIVER_FAULT);
    }
    __set_PRIMASK(mask);
}

static void track_final_edges(void)
{
    LiftFeedback feedback;
    ProductMotor_Feedback(&feedback);
    Lift_Track(&lift, &feedback);
}

/* SysTick stops pulses before the main loop handles the planner/position. */
static void stop_local_input(void)
{
    if (Product_Busy() || pending_key || Motor_Active()) {
        Motor_Stop();
        abort_error = LIFT_USER_STOP;
        abort_pending = true;
    }
    pending_key = reverse_key = 0U;
    key_owned = key_auto = key_armed = false;
}

static void start_local_input(uint8_t key, uint32_t now)
{
    Motor_Stop();
    if (owner == PRODUCT_REMOTE) {
        abort_error = LIFT_USER_STOP;
        abort_pending = true;
    }
    key_armed = key_auto = false;
    key_owned = true;
    key_direction = last_local_key = key;
    key_started = now;
    if (!up_key) up_key = key;
    pending_key = key;
}

void Product_Init(void)
{
    initialized = false;
    Lift_Init(&lift);
    owner = PRODUCT_NONE;
    abort_pending = key_owned = false;
    accepted_target = 50U;
    abort_error = LIFT_OK;
    pending_key = key_direction = 0U;
    reverse_key = remote_home = remote_goal = 0U;
    home_origin = 0; home_learn_reference = false;
    key_auto = false;
    key_started = reverse_started = home_stopped_at = 0U;
    applied_generation = saved_sequence = 0U;
    sampled_at = HAL_GetTick();
    storage_loads = storage_saves = storage_errors = 0U;
    saved_slot = 0U;
    up_key = saved_flags = 0U;
    last_local_key = calibration_key = calibration_pending = last_calibration = 0U;
    pairing_pending = false;
    button_interlock = !(GPIOC->IDR & NETCONTROL_BTN_Pin);
    ProductButton_Init(&control_button, button_interlock, HAL_GetTick());
    saved_travel = saved_top_offset = 0U; saved_valid = saved_reference_known = false;
    memset(trq_trace, 0, sizeof(trq_trace));
    adc_ready = HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK;
    load_range();
    if (LIFT_TRQ_CAPTURE) {
        /* Use only the physically confirmed motor orientation for this trial.
         * Existing EEPROM is read for diagnostics and otherwise left untouched. */
        Lift_Init(&lift);
        lift.direction_known = true;
        lift.up_reverse = LIFT_CAPTURE_UP_REVERSE != 0;
        up_key = 0U;
        saved_valid = false;
    } else if (LIFT_CONFIRMED_DIRECTION && !saved_valid) {
        /* User-approved assembly orientation; first local toggle still supplies
         * the UP key. Save only from that explicit event, never during boot. */
        lift.direction_known = true;
        lift.up_reverse = LIFT_CONFIRMED_UP_REVERSE != 0;
    }
    candidate = (uint8_t)keys();
    stable_ticks = 0U;
    key_armed = false; /* Boot held: require a stable center before motion. */
    initialized = true;
}

void Product_TickISR(void)
{
    if (!initialized) return;
    uint32_t now = HAL_GetTick();
    bool button_down = !(GPIOC->IDR & NETCONTROL_BTN_Pin);
    if (button_down && !button_interlock) {
        calibration_key = last_local_key; /* Input preceding button press, not release. */
        button_interlock = true;
        stop_local_input();
    }
    unsigned event = ProductButton_Tick(&control_button, button_down, HAL_GetTick());
    if (event == PRODUCT_BUTTON_SHORT) calibration_pending = calibration_key ? calibration_key : 3U;
    if (event == PRODUCT_BUTTON_LONG) pairing_pending = true;
    if (!button_down && !control_button.stable &&
        (uint32_t)(HAL_GetTick() - control_button.changed_at) >= 30U) button_interlock = false;
    if (button_interlock) {
        pending_key = reverse_key = 0U;
        key_owned = key_auto = key_armed = false;
        return;
    }
    unsigned raw = keys();
    if (raw && ProductMotor_MusicActive()) Motor_Stop();
    if (raw != candidate) { candidate = (uint8_t)raw; stable_ticks = 1U; }
    else if (stable_ticks < 30U) ++stable_ticks;

    if (raw == 3U) stop_local_input();
    else if (LIFT_LOCAL_LATCH) {
        bool opposite = !reverse_key && raw && ((key_owned && raw != key_direction) ||
            (owner == PRODUCT_REMOTE && Product_Busy() && up_key &&
             raw != (requested_open ? up_key : (up_key ^ 3U))));
        if (opposite) {
            stop_local_input();
            reverse_key = (uint8_t)raw;
            reverse_started = now;
        } else if (reverse_key) {
            if (raw != reverse_key) {
                reverse_key = (uint8_t)raw;
                reverse_started = now;
                key_armed = false;
            } else if (stable_ticks >= 30U) {
                last_local_key = (uint8_t)raw;
                if ((uint32_t)(now - reverse_started) >= LIFT_LOCAL_HOLD_MS) {
                    reverse_key = 0U;
                    start_local_input((uint8_t)raw, now);
                }
            }
        } else if (key_owned) {
            if (!key_auto && raw != key_direction) stop_local_input();
            else if (!Product_Busy() && !pending_key) {
                /* Endpoints/faults consume the held contact; no automatic jog. */
                key_owned = key_auto = false;
            } else if (!key_auto && (uint32_t)(now - key_started) >= LIFT_LOCAL_HOLD_MS &&
                       !lift.jogging && lift.direction_known) key_auto = true;
        }
    } else if (key_owned && raw != key_direction) stop_local_input();

    if (raw == 0U && stable_ticks >= 30U && !key_owned && !reverse_key) key_armed = true;
    if ((raw == 1U || raw == 2U) && stable_ticks >= 30U && key_armed && !reverse_key)
        start_local_input((uint8_t)raw, now);
}

void Product_PlayStartup(void)
{
    if (startup_music_result) return;
    startup_music_result = 2U;
    if (keys() || button_interlock || Product_Busy() || Motor_Active()) return;
    startup_music_result = Music_PlayStored() ? 3U : 1U;
}

void Product_RemoteLostISR(void)
{
    if (owner == PRODUCT_REMOTE) {
        Motor_Stop();
        abort_error = LIFT_LINK_LOST;
        abort_pending = true;
    }
}

bool Product_PairingRequested(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool pending = pairing_pending;
    pairing_pending = false;
    __set_PRIMASK(mask);
    return pending;
}

void Product_Stop(LiftError error)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    track_final_edges();
    Motor_Stop();
    trace_stop(error);
    Lift_Stop(&lift, error);
    applied_generation = lift.drive.generation;
    owner = PRODUCT_NONE;
    pending_key = 0U;
    reverse_key = remote_home = 0U;
    key_owned = key_auto = key_armed = false;
    stable_ticks = 0U;
    abort_pending = false;
    __set_PRIMASK(mask);
}

bool Product_Request(uint8_t percent, ProductOwner requested_owner)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool accepted = !abort_pending && !button_interlock && !calibration_pending && adc_ready && percent <= 100U &&
        (!LIFT_TRQ_CAPTURE || requested_owner == PRODUCT_LOCAL) &&
        (requested_owner != PRODUCT_REMOTE || (up_key && lift.direction_known)) &&
        !(requested_owner == PRODUCT_REMOTE && !remote_home && lift.range_known &&
          !lift.physical_top_known && !lift.position_known) &&
        !(requested_owner == PRODUCT_REMOTE &&
          ((key_owned && !key_auto) || pending_key || reverse_key || keys() != 0U));
    if (accepted) {
        track_final_edges();
        /* A fresh user request may retry an unsuccessful event save. */
        if (!LIFT_TRQ_CAPTURE && lift.direction_known && !state_saved()) lift.save_pending |= LIFT_SAVE_DIRECTION;
        bool home = LIFT_LOCAL_LATCH && requested_owner == PRODUCT_REMOTE &&
                    lift.range_known && (!lift.position_known || !lift.physical_top_known);
        if (requested_owner == PRODUCT_REMOTE && remote_home) {
            remote_goal = percent; /* Retarget without restarting the reference search. */
        } else {
            bool opening = requested_open;
            if (home || !Product_Busy() || lift.target != percent)
                opening = home || percent == 100U ||
                    (percent != 0U && percent >= Lift_Percent(&lift));
            accepted = requested_owner == PRODUCT_LOCAL && LIFT_CONFIRMED_DIRECTION &&
                       Lift_RequestAdjustment(&lift, percent, HAL_GetTick());
            if (home) {
                home_learn_reference = !lift.physical_top_known;
                home_origin = lift.position;
                lift.position_known = false;
                lift.endpoint = lift.anchor = LIFT_ANCHOR_NONE;
            }
            if (!accepted) accepted = Lift_Request(&lift, home ? 100U : percent, HAL_GetTick());
            if (accepted) requested_open = opening;
            if (accepted && home) { remote_home = HOME_SEARCH; remote_goal = percent; }
        }
        if (accepted && requested_owner == PRODUCT_REMOTE) {
            key_owned = key_auto = false;
            key_armed = stable_ticks >= 30U && !keys();
        }
        if (accepted) { owner = requested_owner; accepted_target = percent; }
    }
    __set_PRIMASK(mask);
    synchronize();
    return accepted;
}

ProductCalibration Product_Calibrate(bool up)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool available = !LIFT_TRQ_CAPTURE && adc_ready && up_key && lift.direction_known &&
        !abort_pending && !button_interlock && !calibration_pending && !pairing_pending &&
        keys() != 3U && (GPIOB->IDR & DRV_NFAULT_Pin);
    uint8_t endpoint = LIFT_ANCHOR_NONE;
    if (available) {
        /* Consumes local latch, held contact and pending homing goal as STOP does.
         * Freeze final pulses before changing the coordinate system. */
        Product_Stop(LIFT_USER_STOP);
        endpoint = Lift_CalibrateEndpoint(&lift, up);
        if (endpoint) accepted_target = up ? 100U : 0U;
    }
    __set_PRIMASK(mask);
    if (!available) return PRODUCT_CAL_UNAVAILABLE;
    if (!endpoint) return PRODUCT_CAL_INVALID;
    /* IRQs stay enabled during the bounded EEPROM write/readback. No motion
     * request is executed here; unchanged records require no EEPROM traffic. */
    save_range();
    if (lift.error == LIFT_STORAGE_FAULT || !state_saved()) return PRODUCT_CAL_STORAGE_ERROR;
    return endpoint == LIFT_ANCHOR_TOP ? PRODUCT_CAL_UPPER : PRODUCT_CAL_LOWER;
}

static bool sample_trq(uint16_t *value)
{
    if (!adc_ready || HAL_ADC_Start(&hadc1) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1); return false;
    }
    *value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    return HAL_ADC_Stop(&hadc1) == HAL_OK;
}

void Product_Poll(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool abort = abort_pending;
    uint8_t request = pending_key;
    pending_key = 0U;
    if (abort) {
        track_final_edges();
        Motor_Stop();
        trace_stop(abort_error);
        Lift_Stop(&lift, abort_error);
        applied_generation = lift.drive.generation;
        owner = PRODUCT_NONE;
        remote_home = HOME_NONE;
        abort_pending = false;
    }
    if (calibration_pending) {
        uint8_t input = calibration_pending;
        calibration_pending = 0U;
        track_final_edges();
        Motor_Stop();
        trace_stop(LIFT_USER_STOP);
        Lift_Stop(&lift, LIFT_USER_STOP);
        last_calibration = input == 3U || !up_key ? 3U :
            Lift_ManualEndpoint(&lift, input == up_key);
        if (!last_calibration) last_calibration = 4U;
        if (last_calibration == LIFT_ANCHOR_TOP) accepted_target = 100U;
        if (last_calibration == LIFT_ANCHOR_BOTTOM) accepted_target = 0U;
        applied_generation = lift.drive.generation;
        owner = PRODUCT_NONE;
        remote_home = HOME_NONE;
        request = pending_key = 0U;
        reverse_key = 0U;
        key_owned = key_auto = key_armed = false;
    }
    /* Start processing remains atomic with the raw release check in SysTick. */
    if (request && key_owned && keys() == request) {
        if (!up_key) up_key = request; /* First actual toggle means UP, even if interrupted. */
        (void)Product_Request(request == up_key ? 100U : 0U, PRODUCT_LOCAL);
    }
    __set_PRIMASK(mask);
    synchronize();
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - sampled_at) >= LIFT_SAMPLE_MS) {
        sampled_at = now;
        if (Lift_Busy(&lift)) {
            bool valid = sample_trq(&last_trq);
            uint32_t sample_mask = __get_PRIMASK();
            __disable_irq();
            /* A release/fault during ADC acquisition cannot complete a probe
             * from stale feedback after the ISR already stopped the motor. */
            if (!abort_pending && (owner != PRODUCT_LOCAL ||
                (key_owned && (key_auto || keys() == key_direction)))) {
                LiftFeedback feedback;
                ProductMotor_Feedback(&feedback);
                feedback.adc_valid = valid;
                feedback.trq = last_trq;
                LiftPhase phase = lift.phase;
                trace_sample(&feedback);
                Lift_Tick(&lift, HAL_GetTick(), &feedback);
                trace_finish(phase, feedback.done);
            }
            __set_PRIMASK(sample_mask);
        }
        synchronize();
    }
    mask = __get_PRIMASK();
    __disable_irq();
    if (remote_home == HOME_SEARCH && !Lift_Busy(&lift)) {
        bool at_physical_top = home_learn_reference ? lift.endpoint == LIFT_ANCHOR_TOP :
            lift.position_known && lift.physical_top_known &&
            lift.position == -(int32_t)lift.physical_top_offset;
        if (!abort_pending && owner == PRODUCT_REMOTE && lift.error == LIFT_OK && at_physical_top) {
            if (home_learn_reference && !Lift_ReferenceTop(&lift, home_origin, lift.event_count))
                Lift_Stop(&lift, LIFT_RANGE_MISMATCH);
            remote_home = HOME_WAIT;
            home_stopped_at = now;
        } else remote_home = HOME_NONE;
    }
    if (remote_home == HOME_WAIT && !abort_pending && !button_interlock && !calibration_pending &&
        !keys() && !lift.save_pending && lift.error == LIFT_OK && lift.position_known &&
        (uint32_t)(now - home_stopped_at) >= LIFT_REVERSE_PAUSE_MS) {
        uint8_t goal = remote_goal;
        remote_home = HOME_NONE;
        requested_open = goal == 100U;
        (void)Lift_Request(&lift, goal, now);
    }
    __set_PRIMASK(mask);
    synchronize();
    if (lift.save_pending && !Motor_Active()) save_range();
    if (lift.error != LIFT_OK && remote_home == HOME_WAIT) remote_home = HOME_NONE;
    if (!Product_Busy()) owner = PRODUCT_NONE;
}

bool Product_Busy(void) { return Lift_Busy(&lift) || remote_home != HOME_NONE; }
bool Product_RemoteActive(void) { return owner == PRODUCT_REMOTE && Product_Busy(); }
uint8_t Product_Percent(void) { return Lift_Percent(&lift); }
uint8_t Product_Target(void) { return accepted_target; }
bool Product_Opening(void) { return requested_open; }

void Product_Report(char *buffer, unsigned capacity)
{
    LiftFeedback feedback;
    ProductMotor_Feedback(&feedback);
    ProductMusicFeedback music;
    ProductMotor_MusicFeedback(&music);
    snprintf(buffer, capacity,
        "PRODUCT phase=%u busy=%u owner=%u percent=%u target=%u range=%lu known=%u/%u dir=%u/%u pos=%ld count=%lu trq=%u trqf=%u error=%u EN=%u SLEEP=%u key=%u ee=%lu/%lu/%lu boot=%lu music=%u/%lu/%ld map=%u mode=%u ends=%u/%u saved=%u low=%u probe=%u/%u work=%u capture=%u manual=%u cal=%u last=%u auto=%u reverse=%u home=%u ref=%u/%lu\r\n",
        (unsigned)lift.phase, Product_Busy(), (unsigned)owner, Product_Percent(), Product_Target(),
        (unsigned long)lift.travel, lift.range_known, lift.position_known,
        lift.direction_known, lift.up_reverse, (long)lift.position,
        (unsigned long)feedback.count, last_trq, lift.trq_filtered, (unsigned)lift.error,
        !!(GPIOB->IDR & DRV_ENABLE_Pin), !!(GPIOB->IDR & DRV_NSLEEP_Pin), keys(),
        (unsigned long)storage_loads, (unsigned long)storage_saves, (unsigned long)storage_errors,
        (unsigned long)startup_music_result, (unsigned)music.state,
        (unsigned long)music.pulses, (long)music.offset, up_key, lift.direction_known,
        lift.top_known, lift.bottom_known, state_saved(),
        !!(GPIOA->IDR & LOW_CURRENT_SEL_Pin), trq_trace[0].result, trq_trace[1].result, trq_trace[2].result,
        (unsigned)LIFT_TRQ_CAPTURE, lift.manual_mask, last_calibration,
        last_local_key ? (last_local_key == up_key ? 1U : 2U) : 0U,
        key_auto, reverse_key, remote_home, lift.physical_top_known, (unsigned long)lift.physical_top_offset);
}

