#include "motion.h"
#include "motion_config.h"

void Lift_Init(LiftMotion *m) { *m = (LiftMotion){.estimate = 50U}; }

bool Lift_Busy(const LiftMotion *m)
{
    return m->phase != LIFT_IDLE && m->phase != LIFT_FAULT;
}

uint8_t Lift_Percent(const LiftMotion *m)
{
    if (!Lift_Busy(m) && m->position_known && m->endpoint)
        return m->endpoint == LIFT_ANCHOR_TOP ? 100U : 0U;
    unsigned percent = m->estimate;
    if (m->position_known && m->range_known && m->travel) {
        uint32_t p = m->position < 0 ? 0U : (uint32_t)m->position;
        if (p > m->travel) p = m->travel;
        percent = (100U * (m->travel - p) + m->travel / 2U) / m->travel;
    }
    /* Only the confirmed-endpoint branch above may publish 0 or 100. */
    if (percent == 0U) percent = 1U;
    if (percent == 100U) percent = 99U;
    return (uint8_t)percent;
}

void Lift_Stop(LiftMotion *m, LiftError error)
{
    m->estimate = Lift_Percent(m);
    m->drive.run = false;
    ++m->drive.generation;
    m->paused = false;
    m->jogging = false;
    m->error = error;
    m->phase = error == LIFT_OK || error == LIFT_USER_STOP ? LIFT_IDLE : LIFT_FAULT;
    if (error != LIFT_OK && error != LIFT_USER_STOP) {
        m->position_known = false;
        m->anchor = m->endpoint = LIFT_ANCHOR_NONE;
    }
    /* Completed events survive a following release; partial probes have none. */
    if (!m->position_known) {
        if (m->estimate == 0U) m->estimate = 1U;
        if (m->estimate == 100U) m->estimate = 99U;
    }
}

static bool reference_coordinate(const LiftMotion *m, bool up, bool manual, int32_t *at)
{
    if ((!m->position_known && !(manual && m->anchor != LIFT_ANCHOR_NONE)) ||
        !(up ? m->top_known : m->bottom_known)) return false;
    if (m->range_known) { *at = up ? 0 : (int32_t)m->travel; return true; }
    if (m->anchor == (up ? LIFT_ANCHOR_TOP : LIFT_ANCHOR_BOTTOM)) { *at = 0; return true; }
    return false;
}

static bool endpoint_coordinate(const LiftMotion *m, bool up, int32_t *at)
{
    return reference_coordinate(m, up, false, at);
}

static void reached_limit(LiftMotion *m, bool up, int32_t at)
{
    m->position = at;
    Lift_Stop(m, LIFT_OK);
    m->endpoint = up ? LIFT_ANCHOR_TOP : LIFT_ANCHOR_BOTTOM;
    m->estimate = up ? 100U : 0U;
}

static void segment(LiftMotion *m, LiftPhase phase, uint32_t now)
{
    m->phase = phase;
    m->paused = false;
    m->segment_at = now;
    m->last_count = m->event_count = m->baseline = 0U;
    m->healthy = m->low = m->rail = false;
    m->healthy_samples = 0U;
    m->trq_samples = m->trq_next = 0U;
    m->trq_filtered = 0U;
    m->origin = m->position;
    if (!m->position_known) m->anchor = LIFT_ANCHOR_NONE;
    LiftDrive drive = {.generation = m->drive.generation + 1U,
                       .run = true, .timeout_ms = LIFT_SEARCH_MS};
    if (phase == LIFT_PROBE_A || phase == LIFT_PROBE_B) {
        drive.hz = LIFT_PROBE_HZ;
        drive.reverse = phase == LIFT_PROBE_B;
        /* Motor layer ramps at working current, then lowers VREF for sensing. */
        drive.low_current = !LIFT_FULL_CURRENT_PROFILE;
        drive.timeout_ms = LIFT_PROBE_MS + 100U;
    } else {
        if (!m->jogging && ((m->target == 100U && m->endpoint == LIFT_ANCHOR_TOP) ||
            (m->target == 0U && m->endpoint == LIFT_ANCHOR_BOTTOM))) {
            Lift_Stop(m, LIFT_OK); return;
        }
        int32_t goal = m->range_known ? (int32_t)(m->travel * (100U - m->target) / 100U) : 0;
        m->moving_up = !m->jogging && m->position_known && m->range_known ?
            goal < m->position : m->target == 100U || (m->target != 0U && goal < m->position);
        int32_t boundary;
        /* Adjustment is hold-to-run: no short pulse quota. The key ISR stops
         * on release; ordinary timeout/coordinate/TRQ/fault guards still apply. */
        if (!m->jogging && (m->target == 0U || m->target == 100U) &&
                 endpoint_coordinate(m, m->target == 100U, &boundary)) {
            int32_t distance = m->moving_up ? m->position - boundary : boundary - m->position;
            if (distance <= 0) {
                if (!distance) reached_limit(m, m->moving_up, boundary);
                else Lift_Stop(m, LIFT_USER_STOP); /* Keep an intentional adjustment coordinate. */
                return;
            }
            drive.steps = (uint32_t)distance;
        } else if (!m->jogging && m->target != 0U && m->target != 100U) {
            if (goal == m->position) { Lift_Stop(m, LIFT_OK); return; }
            drive.steps = (uint32_t)(m->moving_up ? m->position - goal : goal - m->position);
        }
        m->endpoint = LIFT_ANCHOR_NONE;
        drive.hz = m->moving_up ? LIFT_UP_HZ : LIFT_DOWN_HZ;
        drive.low_current = !m->moving_up && !LIFT_FULL_CURRENT_PROFILE;
        drive.reverse = m->moving_up ? m->up_reverse : !m->up_reverse;
    }
    m->drive = drive;
}

static void pause_before(LiftMotion *m, LiftPhase next, uint32_t now)
{
    m->drive.run = false;
    ++m->drive.generation;
    m->paused = true;
    m->pause_at = now;
    m->next_phase = next;
}

static bool request(LiftMotion *m, uint8_t percent, uint32_t now, bool jog)
{
    if (percent > 100U || (percent != 0U && percent != 100U &&
        (!m->range_known || !m->position_known))) return false;
    if (Lift_Busy(m) && m->target == percent) return true;
    bool was_busy = Lift_Busy(m);
    if (was_busy) Lift_Stop(m, LIFT_USER_STOP);
    m->jogging = jog;
    m->target = percent;
    m->error = LIFT_OK;
    m->started = m->estimate_at = now;
    LiftPhase first = m->direction_known ? LIFT_MOVE : LIFT_PROBE_A;
    if (was_busy || m->save_pending) {
        m->phase = first;
        pause_before(m, first, now);
    } else segment(m, first, now);
    return true;
}

bool Lift_Request(LiftMotion *m, uint8_t percent, uint32_t now)
{
    return request(m, percent, now, false);
}

bool Lift_RequestAdjustment(LiftMotion *m, uint8_t percent, uint32_t now)
{
    int32_t at;
    bool outside = m->range_known &&
        (m->position < 0 || m->position > (int32_t)m->travel);
    if (Lift_Busy(m) || (percent != 0U && percent != 100U) ||
        !endpoint_coordinate(m, percent == 100U, &at) ||
        (!outside && (percent == 100U ? m->position > at : m->position < at))) return false;
    return request(m, percent, now, true);
}

static uint8_t calibrate_endpoint(LiftMotion *m, bool up, bool correct_near)
{
    if (!m->direction_known || Lift_Busy(m)) return LIFT_ANCHOR_NONE;
    int32_t other;
    if (correct_near && reference_coordinate(m, !up, true, &other)) {
        int32_t difference = m->position - other;
        if (difference < 0) difference = -difference;
        if ((uint32_t)difference < LIFT_MANUAL_NEAR_STEPS) up = !up;
    }
    uint32_t distance = 0U;
    bool other_known = reference_coordinate(m, !up, true, &other);
    if (other_known) {
        int32_t measured = up ? other - m->position : m->position - other;
        if (measured < (int32_t)LIFT_MIN_TRAVEL_STEPS || measured > (int32_t)LIFT_MAX_TRAVEL_STEPS)
            return LIFT_ANCHOR_NONE;
        distance = (uint32_t)measured;
    }
    bool reference = m->physical_top_known &&
        (m->position_known || m->anchor != LIFT_ANCHOR_NONE);
    int32_t offset = (int32_t)m->physical_top_offset + (up ? m->position : 0);
    uint32_t span = distance ? distance : m->travel;
    if (reference && (offset < 0 || (uint32_t)offset > LIFT_MAX_TRAVEL_STEPS - span))
        return LIFT_ANCHOR_NONE;
    if (m->physical_top_known != reference || (reference && m->physical_top_offset != (uint32_t)offset))
        m->save_pending |= LIFT_SAVE_REFERENCE;
    m->physical_top_known = reference;
    m->physical_top_offset = reference ? (uint32_t)offset : 0U;
    if (distance) {
        if (!m->range_known || m->travel != distance) m->save_pending |= LIFT_SAVE_RANGE;
        m->travel = distance;
        m->range_known = m->top_known = m->bottom_known = true;
    }
    uint8_t mask = up ? 1U : 2U;
    if (!(m->manual_mask & mask) || !(up ? m->top_known : m->bottom_known))
        m->save_pending |= up ? LIFT_SAVE_TOP : LIFT_SAVE_BOTTOM;
    m->manual_mask |= mask;
    if (up) m->top_known = true; else m->bottom_known = true;
    m->position = up || !m->range_known ? 0 : (int32_t)m->travel;
    m->anchor = up || m->range_known ? LIFT_ANCHOR_TOP : LIFT_ANCHOR_BOTTOM;
    m->position_known = true;
    reached_limit(m, up, m->position);
    return m->endpoint;
}

uint8_t Lift_ManualEndpoint(LiftMotion *m, bool last_up)
{
    return calibrate_endpoint(m, last_up, true);
}

uint8_t Lift_CalibrateEndpoint(LiftMotion *m, bool up)
{
    return calibrate_endpoint(m, up, false);
}

bool Lift_ReferenceTop(LiftMotion *m, int32_t start_position, uint32_t steps)
{
    int32_t offset = (int32_t)steps - start_position;
    if (Lift_Busy(m) || !m->range_known || offset < 0 ||
        (uint32_t)offset > LIFT_MAX_TRAVEL_STEPS - m->travel) return false;
    m->physical_top_known = true;
    m->physical_top_offset = (uint32_t)offset;
    m->position = -offset;
    m->position_known = true;
    m->anchor = LIFT_ANCHOR_TOP;
    m->endpoint = offset ? LIFT_ANCHOR_NONE : LIFT_ANCHOR_TOP;
    m->estimate = offset ? 99U : 100U;
    m->save_pending |= LIFT_SAVE_REFERENCE;
    return true;
}

static uint16_t torque_value(LiftMotion *m, uint16_t raw)
{
    if (!LIFT_FULL_CURRENT_PROFILE) return m->trq_filtered = raw;
    m->trq_window[m->trq_next] = raw;
    m->trq_next = (uint8_t)((m->trq_next + 1U) % LIFT_TRQ_MEDIAN_SAMPLES);
    if (m->trq_samples < LIFT_TRQ_MEDIAN_SAMPLES) ++m->trq_samples;
    uint16_t ordered[LIFT_TRQ_MEDIAN_SAMPLES];
    for (unsigned i = 0; i < m->trq_samples; ++i) {
        uint16_t value = m->trq_window[i];
        unsigned at = i;
        while (at && ordered[at - 1U] > value) {
            ordered[at] = ordered[at - 1U]; --at;
        }
        ordered[at] = value;
    }
    return m->trq_filtered = ordered[m->trq_samples / 2U];
}

static bool stalled(LiftMotion *m, uint32_t now, const LiftFeedback *f)
{
    if (f->running_ms < LIFT_RAMP_MS + LIFT_SETTLE_MS) {
        m->low = m->rail = false;
        return false;
    }
    if (f->trq >= LIFT_ADC_RAIL_RAW) {
        if (!m->rail) { m->rail = true; m->rail_at = now; }
        if ((uint32_t)(now - m->rail_at) >= LIFT_ADC_RAIL_MS) {
            Lift_Stop(m, LIFT_ADC_FAULT);
        }
        /* An ADC rail is never evidence that the rotor is moving. */
        m->low = false;
        return false;
    } else m->rail = false;
    /* ADC-rail protection above always uses the raw signal. The measured
     * full-current profile filters both torque and baseline at the same scale. */
    uint16_t trq = torque_value(m, f->trq);
    uint32_t relative = m->baseline * LIFT_STALL_PERCENT / 100U;
    uint32_t threshold = relative > LIFT_STALL_RAW ? relative : LIFT_STALL_RAW;
    if (trq <= threshold) {
        if (!m->low) { m->low = true; m->low_at = now; m->low_count = f->count; }
        if ((uint32_t)(now - m->low_at) >= LIFT_STALL_CONFIRM_MS) {
            m->event_count = m->low_count;
            return true;
        }
    } else {
        m->low = false;
        if (trq >= LIFT_HEALTHY_RAW && trq < LIFT_ADC_RAIL_RAW) {
            if (m->healthy_samples < 16U) ++m->healthy_samples;
            m->healthy = m->healthy_samples >= 16U;
            /* Do not follow a gradual loss of torque count down into a stall. */
            if (!m->baseline) m->baseline = trq;
            else if (trq > m->baseline) m->baseline = (m->baseline * 7U + trq) / 8U;
        }
    }
    return false;
}

static void finish_probe(LiftMotion *m, bool blocked, uint32_t now)
{
    if (!blocked && !m->healthy) { Lift_Stop(m, LIFT_DIRECTION_AMBIGUOUS); return; }
    if (m->phase == LIFT_PROBE_A) {
        m->probe_a_stalled = blocked;
        m->probe_a_high = m->baseline >= LIFT_TRQ_HIGH_RAW;
        pause_before(m, LIFT_PROBE_B, now);
    } else if (blocked == m->probe_a_stalled) {
        Lift_Stop(m, !blocked && m->probe_a_high && m->baseline >= LIFT_TRQ_HIGH_RAW ?
            LIFT_TRQ_UNUSABLE : LIFT_DIRECTION_AMBIGUOUS);
    } else {
        m->direction_known = true;
        m->up_reverse = blocked;
        m->position_known = false;
        m->anchor = m->endpoint = LIFT_ANCHOR_NONE;
        m->save_pending |= LIFT_SAVE_DIRECTION;
        pause_before(m, LIFT_MOVE, now);
    }
}

static void endpoint(LiftMotion *m, uint32_t now)
{
    if (m->phase == LIFT_PROBE_A || m->phase == LIFT_PROBE_B) {
        finish_probe(m, true, now); return;
    }
    if (m->jogging) { Lift_Stop(m, LIFT_USER_STOP); return; }
    if (m->target != 0U && m->target != 100U) {
        Lift_Stop(m, LIFT_OBSTRUCTION); return;
    }
    if (m->drive.steps && (m->target == 0U || m->target == 100U)) {
        uint32_t remaining = m->drive.steps > m->event_count ? m->drive.steps - m->event_count : 0U;
        if (remaining > LIFT_ENDPOINT_WINDOW_MIN) {
            /* Keep only a manual-teaching reference at first low count. Normal
             * positioning stays untrusted; moving again discards this reference. */
            int32_t at = m->origin + (m->moving_up ? -(int32_t)m->event_count : (int32_t)m->event_count);
            uint8_t anchor = m->anchor;
            Lift_Stop(m, LIFT_OBSTRUCTION);
            m->position = at; m->anchor = anchor;
            return;
        }
    }
    /* Exclude command pulses sent during the 80 ms stall confirmation. */
    int32_t at = m->origin + (m->moving_up ? -(int32_t)m->event_count : (int32_t)m->event_count);
    uint32_t distance = 0U;
    if (m->position_known && m->healthy && !m->range_known) {
        if (m->moving_up && m->anchor == LIFT_ANCHOR_BOTTOM && at < 0)
            distance = (uint32_t)-at;
        else if (!m->moving_up && m->anchor == LIFT_ANCHOR_TOP && at > 0)
            distance = (uint32_t)at > LIFT_BOTTOM_OFFSET_STEPS ?
                (uint32_t)at - LIFT_BOTTOM_OFFSET_STEPS : 0U;
    }
    if (distance >= LIFT_MIN_TRAVEL_STEPS && distance <= LIFT_MAX_TRAVEL_STEPS) {
        if (m->physical_top_known && m->physical_top_offset > LIFT_MAX_TRAVEL_STEPS - distance) {
            Lift_Stop(m, LIFT_RANGE_MISMATCH); return;
        }
        m->travel = distance;
        m->range_known = m->top_known = m->bottom_known = true;
        m->save_pending |= LIFT_SAVE_RANGE;
    }
    if (m->moving_up) {
        if (!m->top_known) m->save_pending |= LIFT_SAVE_TOP;
        m->top_known = true;
        if (!(m->manual_mask & 1U)) {
            m->physical_top_known = true;
            m->physical_top_offset = 0U;
        }
        m->position = -(int32_t)m->physical_top_offset;
        m->anchor = LIFT_ANCHOR_TOP;
    } else {
        if (!m->bottom_known) m->save_pending |= LIFT_SAVE_BOTTOM;
        m->bottom_known = true;
        m->position = m->range_known ? (int32_t)m->travel : 0;
        m->anchor = m->range_known ? LIFT_ANCHOR_TOP : LIFT_ANCHOR_BOTTOM;
    }
    m->position_known = m->position_known || !m->manual_mask || m->physical_top_known;
    bool up = m->moving_up;
    Lift_Stop(m, LIFT_OK);
    m->endpoint = up ? (m->physical_top_offset ? LIFT_ANCHOR_NONE : LIFT_ANCHOR_TOP) : LIFT_ANCHOR_BOTTOM;
    m->estimate = up ? 100U : 0U;
}

void Lift_Track(LiftMotion *m, const LiftFeedback *f)
{
    if (!Lift_Busy(m) || m->paused || f->generation != m->drive.generation) return;
    if (f->count < m->last_count) { Lift_Stop(m, LIFT_DRIVER_FAULT); return; }
    if (f->count > LIFT_MAX_TRAVEL_STEPS) { Lift_Stop(m, LIFT_TIMEOUT); return; }
    uint32_t delta = f->count - m->last_count;
    m->last_count = f->count;
    if (m->phase == LIFT_MOVE && m->position_known) {
        m->position += m->moving_up ? -(int32_t)delta : (int32_t)delta;
        if (m->position < -(int32_t)LIFT_MAX_TRAVEL_STEPS ||
            m->position > (int32_t)LIFT_MAX_TRAVEL_STEPS)
            Lift_Stop(m, LIFT_TIMEOUT);
    }
}

void Lift_Tick(LiftMotion *m, uint32_t now, const LiftFeedback *f)
{
    if (!Lift_Busy(m)) return;
    if ((uint32_t)(now - m->started) >= LIFT_CALIBRATION_MS) {
        Lift_Stop(m, LIFT_TIMEOUT); return;
    }
    if ((!m->position_known || !m->range_known) &&
        (uint32_t)(now - m->estimate_at) >= LIFT_ESTIMATE_TICK_MS) {
        m->estimate_at = now;
        uint8_t goal = m->target == 0U ? 1U : (m->target == 100U ? 99U : m->target);
        if (m->estimate < goal) ++m->estimate;
        if (m->estimate > goal) --m->estimate;
    }
    if (m->paused) {
        if (!m->save_pending && (uint32_t)(now - m->pause_at) >= LIFT_REVERSE_PAUSE_MS)
            segment(m, m->next_phase, now);
        return;
    }
    if (f->generation != m->drive.generation) return;
    Lift_Track(m, f);
    if (!Lift_Busy(m)) return;
    if (f->fault) { Lift_Stop(m, f->error); return; }
    if ((uint32_t)(now - m->segment_at) >= LIFT_SEARCH_MS) {
        Lift_Stop(m, LIFT_TIMEOUT); return;
    }
    if (f->done) {
        if (m->phase == LIFT_MOVE && m->drive.steps && f->count == m->drive.steps) {
            if (m->jogging) Lift_Stop(m, LIFT_USER_STOP);
            else if (m->target == 0U || m->target == 100U) {
                int32_t at;
                if (endpoint_coordinate(m, m->target == 100U, &at)) reached_limit(m, m->target == 100U, at);
                else Lift_Stop(m, LIFT_DRIVER_FAULT);
            } else {
                m->position = (int32_t)(m->travel * (100U - m->target) / 100U);
                Lift_Stop(m, LIFT_OK);
            }
        } else Lift_Stop(m, LIFT_TIMEOUT);
        return;
    }
    if (!f->active) { Lift_Stop(m, LIFT_DRIVER_FAULT); return; }
    if (!f->adc_valid) { Lift_Stop(m, LIFT_ADC_FAULT); return; }
    if (stalled(m, now, f)) { endpoint(m, now); return; }
    if (!Lift_Busy(m)) return;
    if ((m->phase == LIFT_PROBE_A || m->phase == LIFT_PROBE_B) &&
        (uint32_t)(now - m->segment_at) >= LIFT_PROBE_MS) finish_probe(m, false, now);
}

