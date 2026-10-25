#include "motion.h"
#include "motion_config.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    LiftMotion m;
    uint32_t now, generation, segment_at, count, fraction, saves;
    unsigned saved;
    int32_t physical, bottom;
    uint16_t free_trq, stall_trq;
    bool up_reverse, both_free, both_blocked, bad_adc, rail, driver_fault, save_blocked;
} Simulation;

static Simulation fresh(bool reverse, uint32_t now)
{
    Simulation s = {.up_reverse = reverse, .now = now, .physical = 5000, .bottom = 48000,
                    .free_trq = 600U, .stall_trq = 115U};
    Lift_Init(&s.m);
    return s;
}

/* Synthetic mechanics only: low-current up stalls, working-current up rotates; down
 * rotates until the reverse-winding boundary. This is not measured motor data. */
static void advance(Simulation *s)
{
    s->now += 5U;
    if (s->generation != s->m.drive.generation) {
        s->generation = s->m.drive.generation;
        s->segment_at = s->now;
        s->count = s->fraction = 0U;
    }
    LiftFeedback f = {.generation = s->generation, .adc_valid = !s->bad_adc,
                      .active = s->m.drive.run, .trq = 600U};
    uint32_t elapsed = s->now - s->segment_at;
    bool up = s->m.drive.reverse == s->up_reverse;
    bool blocked = (up && (s->m.drive.low_current || s->physical <= 0)) ||
                   (!up && s->physical >= s->bottom) || s->both_blocked;
    if (s->both_free) blocked = false;
    if (s->m.drive.run && elapsed >= 3U) {
        f.running_ms = elapsed - 3U;
        uint32_t hz = f.running_ms >= LIFT_RAMP_MS ? s->m.drive.hz :
            50U + (s->m.drive.hz - 50U) * f.running_ms / LIFT_RAMP_MS;
        s->fraction += hz * 5U;
        uint32_t pulses = s->fraction / 1000U;
        s->fraction %= 1000U;
        if (s->m.drive.steps && pulses > s->m.drive.steps - s->count)
            pulses = s->m.drive.steps - s->count;
        s->count += pulses;
        if (!blocked) {
            s->physical += up ? -(int32_t)pulses : (int32_t)pulses;
            if (s->physical < 0) s->physical = 0;
            if (s->physical > s->bottom) s->physical = s->bottom;
        }
        if (s->m.drive.steps && s->count >= s->m.drive.steps) {
            f.done = true; f.active = false;
        }
        if (elapsed >= s->m.drive.timeout_ms) { f.fault = true; f.error = LIFT_TIMEOUT; }
    }
    f.count = s->count;
    if (s->driver_fault) { f.fault = true; f.error = LIFT_DRIVER_FAULT; }
    f.trq = s->rail ? 4095U : (blocked ? s->stall_trq : s->free_trq);
    Lift_Tick(&s->m, s->now, &f);
    if (s->m.save_pending && !s->save_blocked) {
        ++s->saves; s->saved |= s->m.save_pending; s->m.save_pending = 0U;
    }
    if (Lift_Busy(&s->m) && (!s->m.range_known || !s->m.position_known))
        assert(Lift_Percent(&s->m) >= 1U && Lift_Percent(&s->m) <= 99U);
}

static void finish(Simulation *s)
{
    unsigned ticks = 0U;
    while (Lift_Busy(&s->m) && ticks++ < 80000U) advance(s);
    assert(!Lift_Busy(&s->m));
}

static void wait_phase(Simulation *s, LiftPhase phase)
{
    unsigned ticks = 0U;
    while ((s->m.phase != phase || s->m.paused) && Lift_Busy(&s->m) && ticks++ < 2000U)
        advance(s);
    assert(s->m.phase == phase && !s->m.paused);
}

static void learn_direction(Simulation *s, uint8_t target)
{
    assert(Lift_Request(&s->m, target, s->now));
    assert(s->m.drive.hz == LIFT_PROBE_HZ);
    assert(s->m.drive.low_current);
    unsigned ticks = 0U;
    while (!s->m.direction_known && Lift_Busy(&s->m) && ticks++ < 1000U) advance(s);
    assert(s->m.direction_known && s->m.up_reverse == s->up_reverse);
    assert(s->m.paused && !s->m.drive.run && !s->m.top_known && !s->m.bottom_known);
    assert(s->saves == 1U && s->saved == LIFT_SAVE_DIRECTION && !s->m.range_known);
    wait_phase(s, LIFT_MOVE);
    assert(s->m.drive.hz == (target == 100U ? LIFT_UP_HZ : LIFT_DOWN_HZ));
    assert(s->m.drive.low_current == (target == 0U));
}

static void direction_and_manual_endpoints(void)
{
    for (unsigned reverse = 0; reverse < 2U; ++reverse) {
        for (unsigned down_first = 0; down_first < 2U; ++down_first) {
            Simulation s = fresh(reverse != 0U, 0xfffff000U);
            uint8_t first = down_first ? 0U : 100U;
            learn_direction(&s, first);
            finish(&s);
            assert(s.m.error == LIFT_OK && Lift_Percent(&s.m) == first);
            assert(s.m.position_known && !s.m.range_known && s.saves == 2U);
            assert(s.m.top_known != s.m.bottom_known); /* No autonomous other-end search. */
            assert(Lift_Request(&s.m, first == 0U ? 100U : 0U, s.now));
            finish(&s);
            assert(s.m.range_known && s.m.top_known && s.m.bottom_known && s.saves == 3U);
            assert(s.m.travel >= (uint32_t)s.bottom - 180U && s.m.travel <= (uint32_t)s.bottom + 180U);
            assert(Lift_Request(&s.m, 37U, s.now)); finish(&s);
            assert(s.m.error == LIFT_OK && Lift_Percent(&s.m) == 37U);
            assert(Lift_Request(&s.m, 100U, s.now)); finish(&s);
            assert(s.m.position == 0 && s.physical == 0 && Lift_Percent(&s.m) == 100U);
            assert(Lift_Request(&s.m, 0U, s.now)); finish(&s);
            assert(s.physical == s.bottom && Lift_Percent(&s.m) == 0U && s.saves == 3U);
            assert(Lift_Request(&s.m, 0U, s.now) && !Lift_Busy(&s.m)); /* No repeated push/write. */
        }
    }
    puts("PASS both motor directions: save direction before endpoints; working UP and sensing DOWN current profiles; either endpoint order; learned range and 37%; unchanged endpoints not resaved");
}

static void interrupted_detection(void)
{
    for (unsigned phase = 0; phase < 2U; ++phase) {
        Simulation s = fresh(false, 0);
        assert(Lift_Request(&s.m, 100U, s.now));
        if (phase) wait_phase(&s, LIFT_PROBE_B);
        for (unsigned i = 0; i < 100U; ++i) advance(&s);
        Lift_Stop(&s.m, LIFT_USER_STOP);
        assert(!s.m.direction_known && !s.m.save_pending && !s.saves && !s.m.drive.run);
        assert(Lift_Request(&s.m, 100U, s.now));
        assert(s.m.phase == LIFT_PROBE_A && !s.m.paused);
    }
    Simulation s = fresh(false, 0);
    s.save_blocked = true;
    assert(Lift_Request(&s.m, 100U, s.now));
    while (!s.m.direction_known && Lift_Busy(&s.m)) advance(&s);
    assert(s.m.save_pending == LIFT_SAVE_DIRECTION);
    Lift_Stop(&s.m, LIFT_USER_STOP);
    assert(s.m.direction_known && s.m.save_pending == LIFT_SAVE_DIRECTION);
    assert(Lift_Request(&s.m, 0U, s.now) && s.m.phase == LIFT_MOVE && s.m.paused);
    s.save_blocked = false; advance(&s);
    wait_phase(&s, LIFT_MOVE);
    assert(s.m.drive.hz == LIFT_DOWN_HZ && s.saves == 1U);
    puts("PASS release during either probe restarts A; release after completion preserves direction/save and never re-enters probing");
}

static void partial_work_and_reboot(void)
{
    Simulation s = fresh(false, 0);
    learn_direction(&s, 100U); finish(&s);
    assert(Lift_Request(&s.m, 0U, s.now));
    for (unsigned i = 0; i < 400U; ++i) advance(&s);
    Lift_Stop(&s.m, LIFT_USER_STOP);
    int32_t partial = s.m.position;
    assert(partial > 0 && s.m.position_known && !s.m.range_known && !s.m.endpoint);
    assert(Lift_Request(&s.m, 0U, s.now) && s.m.phase == LIFT_MOVE); finish(&s);
    assert(s.m.range_known && s.m.travel >= (uint32_t)s.bottom - 180U &&
           s.m.travel <= (uint32_t)s.bottom + 180U);
    uint32_t travel = s.m.travel;
    Lift_Init(&s.m);
    s.m.direction_known = s.m.top_known = s.m.bottom_known = s.m.range_known = true;
    s.m.travel = travel;
    assert(Lift_Percent(&s.m) == 50U && !s.m.position_known);
    assert(!Lift_Request(&s.m, 37U, s.now)); /* No invented percentage location. */
    assert(Lift_Request(&s.m, 0U, s.now));
    assert(s.m.phase == LIFT_MOVE && !s.m.moving_up && s.m.drive.hz == LIFT_DOWN_HZ);
    finish(&s);
    assert(s.m.position_known && s.m.position == (int32_t)travel && Lift_Percent(&s.m) == 0U);
    puts("PASS interrupted working stroke retains coordinate; reboot keeps direction/range but waits for a real endpoint to trust position");
}

static void failures_and_timing(void)
{
    for (unsigned kind = 0; kind < 5U; ++kind) {
        Simulation s = fresh(false, 0);
        s.both_blocked = kind == 0; s.both_free = kind == 1;
        s.bad_adc = kind == 2; s.rail = kind == 3; s.driver_fault = kind == 4;
        assert(Lift_Request(&s.m, 100U, s.now)); finish(&s);
        assert(s.m.phase == LIFT_FAULT && !s.m.direction_known && !s.saves);
        assert(s.m.error == (kind < 2 ? LIFT_DIRECTION_AMBIGUOUS :
                            kind < 4 ? LIFT_ADC_FAULT : LIFT_DRIVER_FAULT));
    }
    Simulation s = fresh(false, 0xffffff00U);
    s.both_free = true;
    assert(Lift_Request(&s.m, 100U, s.now));
    for (unsigned elapsed = 5U; elapsed < 1500U; elapsed += 5U) {
        advance(&s); assert(s.m.phase == LIFT_PROBE_A && !s.m.paused);
    }
    advance(&s); assert(s.m.paused && !s.m.drive.run);
    finish(&s); assert(s.m.error == LIFT_DIRECTION_AMBIGUOUS);
    s = fresh(false, 0);
    assert(Lift_Request(&s.m, 100U, s.now));
    while (!s.m.paused && s.now < 1500U) advance(&s);
    assert(s.m.paused && s.now >= 1230U && s.now <= 1250U);
    s = fresh(false, 0xfffff000U); s.m.direction_known = true; s.both_free = true;
    assert(Lift_Request(&s.m, 100U, s.now)); finish(&s);
    assert(s.m.error == LIFT_TIMEOUT && !s.m.top_known && !s.saves);
    puts("PASS ambiguous direction, ADC/rail/nFAULT, 1.5 s probe cap, early stall, timeout/tick wrap and no false persistence");
}

static void filtering_and_reversal(void)
{
    LiftMotion m; Lift_Init(&m);
    assert(!Lift_Request(&m, 37U, 0U) && !Lift_Busy(&m));
    assert(Lift_Request(&m, 100U, 0U));
    LiftFeedback f = {.generation = m.drive.generation, .active = true, .adc_valid = true};
    for (uint32_t now = 0; now < 1150U; now += 5U) {
        f.running_ms = now; Lift_Tick(&m, now, &f); assert(!m.paused);
    }
    for (uint32_t now = 1200; now <= 1300; now += 5U) {
        f.running_ms = now; f.trq = 600U; Lift_Tick(&m, now, &f);
    }
    f.trq = 0; Lift_Tick(&m, 1305U, &f);
    f.trq = 600U; Lift_Tick(&m, 1310U, &f); assert(!m.low && !m.paused);
    f.trq = 100U;
    for (uint32_t now = 1320; now <= 1400; now += 5U) Lift_Tick(&m, now, &f);
    assert(m.paused && m.next_phase == LIFT_PROBE_B);
    f.generation = m.drive.generation - 1U;
    Lift_Tick(&m, 1600U, &f); assert(m.phase == LIFT_PROBE_B && !m.paused);
    f.done = true; f.active = false;
    Lift_Tick(&m, 1605U, &f); assert(m.phase == LIFT_PROBE_B);
    Lift_Init(&m);
    m.direction_known = m.range_known = m.position_known = true;
    m.anchor = LIFT_ANCHOR_TOP; m.travel = 32000U; m.position = 16000;
    m.position = 1; assert(Lift_Percent(&m) == 99U);
    m.position = 31999; assert(Lift_Percent(&m) == 1U);
    m.position = 16000;
    assert(Lift_Request(&m, 100U, 0U));
    uint32_t started = m.started, generation = m.drive.generation;
    assert(Lift_Request(&m, 100U, 50U) && m.started == started && m.drive.generation == generation);
    assert(Lift_Request(&m, 0U, 100U));
    assert(Lift_Request(&m, 37U, 150U));
    Lift_Tick(&m, 349U, &f); assert(m.paused && !m.drive.run);
    Lift_Tick(&m, 350U, &f); assert(!m.paused && m.drive.run);
    puts("PASS startup blanking, relative TRQ, glitch rejection, stale feedback, duplicate requests and 200 ms reversal dwell");
}

static void measured_signal_regressions(void)
{
    /* Real low-current stalled plateau is 102..124, not an ideal zero. A high
     * FREE count alone must not abort the other direction before comparison. */
    for (unsigned reverse = 0; reverse < 2U; ++reverse) {
        Simulation s = fresh(reverse != 0U, 0U);
        s.stall_trq = 124U; s.free_trq = 2800U;
        learn_direction(&s, 100U);
        assert(s.saves == 1U && s.m.direction_known);
    }
    Simulation s = fresh(false, 0U);
    s.both_free = true; s.free_trq = 2880U;
    assert(Lift_Request(&s.m, 100U, 0U)); finish(&s);
    assert(s.m.error == LIFT_TRQ_UNUSABLE && !s.saves && !s.m.direction_known);
    s = fresh(false, 0U); s.stall_trq = 200U;
    assert(Lift_Request(&s.m, 100U, 0U)); finish(&s);
    assert(s.m.error == LIFT_DIRECTION_AMBIGUOUS && !s.saves);

    LiftMotion m; Lift_Init(&m); m.direction_known = true;
    assert(Lift_Request(&m, 100U, 0U));
    LiftFeedback f = {.generation = m.drive.generation, .active = true, .adc_valid = true};
    for (uint32_t now = 1150U; now < 3000U && Lift_Busy(&m); now += 5U) {
        f.running_ms = now;
        f.trq = now < 1250U ? 2000U : (now < 2950U ? (uint16_t)(3250U - now) : 300U);
        Lift_Tick(&m, now, &f);
    }
    assert(!Lift_Busy(&m) && m.error == LIFT_OK && m.top_known && m.baseline == 2000U);
    puts("PASS measured nonzero 124 stall, either probe order with high FREE count, two high plateaus rejected, indeterminate gap rejected, gradual TRQ collapse cannot lower its own baseline");
}

int main(void)
{
    direction_and_manual_endpoints();
    interrupted_detection();
    partial_work_and_reboot();
    failures_and_timing();
    filtering_and_reversal();
    measured_signal_regressions();
    return 0;
}

