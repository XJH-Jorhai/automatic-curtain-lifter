#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LIFT_IDLE, LIFT_PROBE_A, LIFT_PROBE_B, LIFT_MOVE, LIFT_FAULT
} LiftPhase;
typedef enum {
    LIFT_OK, LIFT_USER_STOP, LIFT_DIRECTION_AMBIGUOUS, LIFT_TIMEOUT,
    LIFT_DRIVER_FAULT, LIFT_ADC_FAULT, LIFT_OBSTRUCTION, LIFT_RANGE_MISMATCH,
    LIFT_MAIN_STALL, LIFT_LINK_LOST, LIFT_STORAGE_FAULT, LIFT_TRQ_UNUSABLE
} LiftError;

enum { LIFT_ANCHOR_NONE, LIFT_ANCHOR_TOP, LIFT_ANCHOR_BOTTOM };
enum { LIFT_SAVE_DIRECTION = 1U, LIFT_SAVE_TOP = 2U,
       LIFT_SAVE_BOTTOM = 4U, LIFT_SAVE_RANGE = 8U, LIFT_SAVE_REFERENCE = 16U };

/* Generation binds feedback to exactly one motor segment, including pauses. */
typedef struct {
    uint32_t generation, hz, steps, timeout_ms;
    bool run, reverse, low_current;
} LiftDrive;
typedef struct {
    uint32_t generation, count, running_ms;
    bool active, done, fault, adc_valid;
    uint16_t trq;
    LiftError error;
} LiftFeedback;
typedef struct {
    LiftPhase phase, next_phase;
    LiftError error;
    LiftDrive drive;
    uint32_t started, segment_at, pause_at, estimate_at;
    uint32_t travel, last_count, event_count;
    int32_t position, origin;
    uint32_t baseline, low_at, low_count, rail_at;
    uint8_t target, estimate, healthy_samples, anchor, endpoint, save_pending;
    bool direction_known, up_reverse, range_known, position_known;
    bool top_known, bottom_known, moving_up;
    bool paused, probe_a_stalled, healthy, low, rail, probe_a_high;
    /* Full-current median state; every segment starts with an empty window. */
    uint16_t trq_window[5];
    uint8_t trq_samples, trq_next;
    uint16_t trq_filtered;
    uint8_t manual_mask; /* bit0 TOP, bit1 BOTTOM explicitly taught by button */
    bool jogging;
    uint32_t physical_top_offset; /* Physical TOP is at -offset in the taught frame. */
    bool physical_top_known;
} LiftMotion;

void Lift_Init(LiftMotion *m);
bool Lift_Busy(const LiftMotion *m);
bool Lift_Request(LiftMotion *m, uint8_t percent, uint32_t now);
bool Lift_RequestAdjustment(LiftMotion *m, uint8_t percent, uint32_t now);
uint8_t Lift_ManualEndpoint(LiftMotion *m, bool last_up);
/* Explicit App labels do not use the physical button's near-opposite correction. */
uint8_t Lift_CalibrateEndpoint(LiftMotion *m, bool up);
bool Lift_ReferenceTop(LiftMotion *m, int32_t start_position, uint32_t steps);
void Lift_Stop(LiftMotion *m, LiftError error);
void Lift_Track(LiftMotion *m, const LiftFeedback *feedback);
void Lift_Tick(LiftMotion *m, uint32_t now, const LiftFeedback *feedback);
uint8_t Lift_Percent(const LiftMotion *m);

