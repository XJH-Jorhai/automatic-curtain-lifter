#include "motion.h"
#include "motion_config.h"
#include <assert.h>
#include <stdio.h>
#include "data/trq_top_2026_10_25.h"

_Static_assert(LIFT_FULL_CURRENT_PROFILE, "Build this test with the measured full-current profile");

static LiftMotion start(void)
{
    LiftMotion m;
    Lift_Init(&m);
    m.direction_known = true;
    assert(Lift_Request(&m, 100U, 0U));
    return m;
}

static void feed(LiftMotion *m, uint32_t ms, uint16_t raw)
{
    LiftFeedback f = {.generation = m->drive.generation, .active = true, .adc_valid = true,
        .running_ms = ms, .count = ms < 1000U ? ms * 4U : 4800U + (ms - 1000U) * 96U / 10U,
        .trq = raw};
    Lift_Tick(m, ms + 3U, &f);
}

static void recorded_top_replay(void)
{
    const unsigned baselines[] = {0U, 180U, 245U, 265U};
    for (unsigned run = 0U; run < sizeof(baselines) / sizeof(baselines[0]); ++run) {
        LiftMotion m = start();
        m.baseline = baselines[run];
        uint32_t stopped = 0U;
        for (unsigned i = 0U; i < sizeof(recorded_top) / sizeof(recorded_top[0]); ++i) {
            feed(&m, recorded_top[i].ms, recorded_top[i].raw);
            if (!Lift_Busy(&m)) { stopped = recorded_top[i].ms; break; }
            if (recorded_top[i].ms < 15662U) assert(!m.top_known);
        }
        assert(m.error == LIFT_OK && m.top_known && m.endpoint == LIFT_ANCHOR_TOP);
        assert(stopped >= 15662U + LIFT_STALL_CONFIRM_MS && stopped <= 15662U + 200U);
        assert(m.event_count < m.last_count && m.position == 0);
        printf("PASS retained real 10 ms top waveform: prior baseline %u, stop %lu ms, %lu ms after first drop (5 ms ADC timing not reconstructed)\n",
            baselines[run], (unsigned long)stopped, (unsigned long)(stopped - 15662U));
    }
}

static void outliers_and_reset(void)
{
    LiftMotion m = start();
    for (unsigned at = 0U; at <= 2000U; at += 5U) feed(&m, at, 200U);
    const uint16_t glitches[] = {4U, 4U, 2880U, 3000U, 200U, 200U, 200U};
    for (unsigned i = 0U; i < sizeof(glitches) / sizeof(glitches[0]); ++i)
        feed(&m, 2005U + i * 5U, glitches[i]);
    assert(Lift_Busy(&m) && m.baseline == 200U && !m.low && !m.top_known);
    for (unsigned at = 2100U; at < 2115U; at += 5U) feed(&m, at, 4U);
    for (unsigned at = 2115U; at <= 2400U; at += 5U) feed(&m, at, 200U);
    assert(Lift_Busy(&m) && !m.low && !m.top_known);
    Lift_Stop(&m, LIFT_USER_STOP);
    assert(Lift_Request(&m, 0U, 2500U));
    assert(!m.trq_samples && !m.trq_next && !m.trq_filtered && !m.baseline && !m.low);
    puts("PASS capture transient rejection: short low bursts and two high outliers do not create endpoints or inflate baseline; new segment clears filter");
}

static void raw_rail_and_startup(void)
{
    LiftMotion m = start();
    for (unsigned at = 0U; at <= 2000U; at += 5U) feed(&m, at, 200U);
    for (unsigned at = 2010U; at <= 2060U; at += 5U) feed(&m, at, 4095U);
    assert(m.phase == LIFT_FAULT && m.error == LIFT_ADC_FAULT && !m.top_known);
    m = start();
    for (unsigned at = 0U; at < 1400U && Lift_Busy(&m); at += 5U) {
        feed(&m, at, 0U);
        if (at < LIFT_RAMP_MS + LIFT_SETTLE_MS + LIFT_STALL_CONFIRM_MS) assert(Lift_Busy(&m));
    }
    assert(m.error == LIFT_OK && m.top_known && m.healthy_samples == 0U);
    puts("PASS capture raw ADC-rail fault bypasses filter; startup blanking remains intact even when starting blocked");
}

int main(void)
{
    recorded_top_replay();
    outliers_and_reset();
    raw_rail_and_startup();
    return 0;
}
