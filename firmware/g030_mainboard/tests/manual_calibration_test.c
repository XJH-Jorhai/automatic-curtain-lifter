#include "motion.h"
#include "motion_config.h"
#include "product_button.h"
#include <assert.h>
#include <stdio.h>

static LiftMotion known(void)
{
    LiftMotion m; Lift_Init(&m);
    m.direction_known = m.position_known = m.range_known = m.top_known = m.bottom_known = true;
    m.anchor = LIFT_ANCHOR_TOP; m.travel = 80000U; m.position = 40000;
    return m;
}

static void manual_geometry(void)
{
    LiftMotion m; Lift_Init(&m); m.direction_known = true;
    assert(Lift_ManualEndpoint(&m, true) == LIFT_ANCHOR_TOP && m.manual_mask == 1U);
    assert(m.position_known && !m.range_known && m.position == 0);
    m.save_pending = 0U; m.position = 80000;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.manual_mask == 3U);
    assert(m.travel == 80000U && m.range_known);
    m = known(); m.position = 1000;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_TOP && m.travel == 79000U);
    m = known(); m.position = 79000;
    assert(Lift_ManualEndpoint(&m, true) == LIFT_ANCHOR_BOTTOM && m.travel == 79000U);
    m = known(); m.position = 4799;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_TOP && m.travel == 75201U);
    m = known(); m.position = 4800;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 4800U);
    m = known(); m.position = -4799;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_TOP && m.travel == 84799U);
    m = known(); m.position = 80000 - 4799;
    assert(Lift_ManualEndpoint(&m, true) == LIFT_ANCHOR_BOTTOM && m.travel == 75201U);
    m = known(); m.position = 80000 - 4800;
    assert(Lift_ManualEndpoint(&m, true) == LIFT_ANCHOR_TOP && m.travel == 4800U);
    m = known(); m.position_known = false; m.anchor = LIFT_ANCHOR_NONE; m.position = 0;
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 80000U);
    m = known(); m.position = LIFT_MAX_TRAVEL_STEPS + 1U;
    assert(!Lift_ManualEndpoint(&m, false) && m.travel == 80000U && !m.manual_mask);
    puts("PASS manual geometry: UP/DOWN teaching, opposite-end fine adjustment, strict4799/4800 boundary both ways, unknown-position fallback and invalid-span rejection");
}

static void explicit_geometry(void)
{
    LiftMotion m = known(); m.position = 2000;
    assert(Lift_CalibrateEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 2000U);
    m = known(); m.position = 78000;
    assert(Lift_CalibrateEndpoint(&m, true) == LIFT_ANCHOR_TOP && m.travel == 2000U);
    m = known(); m.position = 1000;
    assert(!Lift_CalibrateEndpoint(&m, false) && m.travel == 80000U && !m.manual_mask);
    m = known(); m.position_known = false; m.anchor = LIFT_ANCHOR_NONE;
    assert(Lift_CalibrateEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 80000U);
    puts("PASS explicit App endpoint labels never flip near the opposite endpoint; invalid span rejected and unknown-position anchor supported");
}

static void soft_limits_and_jog(void)
{
    LiftMotion m = known(); m.manual_mask = 3U;
    assert(Lift_Request(&m, 0U, 0U) && m.drive.steps == 40000U);
    LiftFeedback f = {.generation = m.drive.generation, .count = 40000U, .running_ms = 5000U,
        .done = true, .adc_valid = true, .trq = 240U};
    Lift_Tick(&m, 5000U, &f);
    assert(!Lift_Busy(&m) && m.endpoint == LIFT_ANCHOR_BOTTOM && m.position == 80000 && !m.save_pending);
    assert(Lift_RequestAdjustment(&m, 0U, 6000U) && !m.drive.steps);
    f.generation = m.drive.generation; f.count = 15000U; f.running_ms = 2000U;
    f.done = false; f.active = true;
    Lift_Tick(&m, 8000U, &f);
    assert(Lift_Busy(&m) && m.jogging && !m.endpoint && m.position == 95000 && !m.save_pending);
    Lift_Stop(&m, LIFT_USER_STOP);
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 95000U);
    assert(m.manual_mask == 3U && (m.save_pending & LIFT_SAVE_RANGE));
    m = known(); m.position = 0; m.endpoint = LIFT_ANCHOR_TOP;
    assert(Lift_RequestAdjustment(&m, 100U, 0U));
    f.generation = m.drive.generation; f.count = 15000U;
    Lift_Tick(&m, 2000U, &f);
    assert(Lift_Busy(&m) && !m.endpoint && m.position == -15000 && !m.drive.steps);
    Lift_Stop(&m, LIFT_USER_STOP);
    assert(Lift_RequestAdjustment(&m, 0U, 3000U) && m.jogging && !m.drive.steps && !m.moving_up);
    f.generation = m.drive.generation; f.count = 20000U;
    Lift_Tick(&m, 5000U, &f);
    assert(Lift_Busy(&m) && m.position == 5000 && m.jogging); /* Crossing back into range remains manual. */
    Lift_Stop(&m, LIFT_USER_STOP);
    m = known(); assert(Lift_Request(&m, 0U, 0U));
    f = (LiftFeedback){.generation = m.drive.generation, .count = 1000U,
        .running_ms = 2000U, .active = true, .adc_valid = true, .trq = 0U};
    for (unsigned at = 2000U; at <= 2100U && Lift_Busy(&m); at += 5U) Lift_Tick(&m, at, &f);
    assert(m.error == LIFT_OBSTRUCTION && !m.endpoint && m.travel == 80000U);
    assert(!m.position_known);
    Lift_Stop(&m, LIFT_USER_STOP); /* Product button stops before applying teaching. */
    assert(Lift_ManualEndpoint(&m, false) == LIFT_ANCHOR_BOTTOM && m.travel == 41000U);
    puts("PASS saved soft limits still exact; hold-to-run adjustment exceeds15000 pulses in both directions,reverse crosses back into range without soft stop; explicit save required; early obstruction cannot replace an endpoint");
}

static void physical_reference(void)
{
    LiftMotion m = known(); m.position = 0; m.manual_mask = 3U;
    assert(Lift_ReferenceTop(&m, 0, 20000U));
    assert(m.physical_top_known && m.physical_top_offset == 20000U && m.position == -20000);
    assert(m.travel == 80000U && Lift_Percent(&m) == 99U && !m.endpoint);
    assert(Lift_Request(&m, 100U, 0U));
    LiftFeedback f = {.adc_valid = true, .active = true, .trq = 200U};
    Lift_Tick(&m, 200U, &f); /* Save event / reverse dwell precedes the segment. */
    m.save_pending = 0U; Lift_Tick(&m, 400U, &f);
    assert(m.drive.run && !m.moving_up && m.drive.steps == 20000U);
    f.generation = m.drive.generation; f.count = 20000U; f.done = true;
    Lift_Tick(&m, 2000U, &f);
    assert(!Lift_Busy(&m) && m.position == 0 && Lift_Percent(&m) == 100U);
    m.position = 10000;
    assert(Lift_CalibrateEndpoint(&m, true) == LIFT_ANCHOR_TOP);
    assert(m.physical_top_offset == 30000U && m.travel == 70000U);
    m = known(); assert(!Lift_ReferenceTop(&m, 5000, 1000U));
    assert(!m.physical_top_known && m.travel == 80000U);
    assert(!Lift_ReferenceTop(&m, 0, LIFT_MAX_TRAVEL_STEPS));
    assert(!Lift_ReferenceTop(&m, -(int32_t)LIFT_MAX_TRAVEL_STEPS, 10000U));
    puts("PASS physical reference: out-of-range TOP remains99%,returns DOWN20000 pulses to taught100%,upper re-teaching shifts offset without moving bottom,negative/oversized offsets rejected");
}

static void button_timing(void)
{
    ProductButton b; ProductButton_Init(&b, false, 0U);
    assert(!ProductButton_Tick(&b, true, 10U));
    assert(!ProductButton_Tick(&b, true, 39U));
    assert(!ProductButton_Tick(&b, true, 40U));
    assert(!ProductButton_Tick(&b, false, 100U));
    assert(ProductButton_Tick(&b, false, 130U) == PRODUCT_BUTTON_SHORT);
    assert(!ProductButton_Tick(&b, false, 131U));
    assert(!ProductButton_Tick(&b, true, 200U));
    assert(!ProductButton_Tick(&b, true, 230U));
    assert(ProductButton_Tick(&b, true, 5230U) == PRODUCT_BUTTON_LONG);
    assert(!ProductButton_Tick(&b, true, 6000U));
    assert(!ProductButton_Tick(&b, false, 6100U));
    assert(!ProductButton_Tick(&b, false, 6130U));
    ProductButton_Init(&b, true, 0U);
    assert(!ProductButton_Tick(&b, true, 6000U));
    assert(!ProductButton_Tick(&b, false, 6100U));
    assert(!ProductButton_Tick(&b, false, 6130U));
    ProductButton_Init(&b, false, 0xfffffff0U);
    assert(!ProductButton_Tick(&b, true, 0xfffffff5U));
    assert(!ProductButton_Tick(&b, true, 25U));
    assert(!ProductButton_Tick(&b, false, 100U));
    assert(ProductButton_Tick(&b, false, 130U) == PRODUCT_BUTTON_SHORT);
    puts("PASS button debounce, one short on release, one5s long without trailing short, boot-held suppression and tick wrap");
}

int main(void) { manual_geometry(); explicit_geometry(); soft_limits_and_jog(); physical_reference(); button_timing(); return 0; }
