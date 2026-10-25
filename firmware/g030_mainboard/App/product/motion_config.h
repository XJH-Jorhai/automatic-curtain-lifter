#pragma once

/* Separate local-only capture build; Product remains the default. */
#ifndef LIFT_TRQ_CAPTURE
#define LIFT_TRQ_CAPTURE           0
#endif
#ifndef LIFT_CAPTURE_UP_REVERSE
#define LIFT_CAPTURE_UP_REVERSE    0
#endif
/* This assembly was physically confirmed by the user: DIR0 winds UP.
 * Legacy host regressions explicitly set this to 0; deployed Product does not probe. */
#ifndef LIFT_CONFIRMED_DIRECTION
#define LIFT_CONFIRMED_DIRECTION   1
#endif
#ifndef LIFT_CONFIRMED_UP_REVERSE
#define LIFT_CONFIRMED_UP_REVERSE  0
#endif
#define LIFT_FULL_CURRENT_PROFILE  (LIFT_TRQ_CAPTURE || LIFT_CONFIRMED_DIRECTION)
#define LIFT_LOCAL_LATCH           (LIFT_CONFIRMED_DIRECTION && !LIFT_TRQ_CAPTURE)
#define LIFT_LOCAL_HOLD_MS         1000U

/* 1.8 degree motor, fixed 1/8 microsteps. Initial mechanical-trial settings.
 * TRQ thresholds and bottom event offset MUST be measured on the final mechanism. */
#define LIFT_UP_HZ                 9600U /* 360 rpm */
#define LIFT_CALIBRATION_VERSION   (LIFT_CONFIRMED_DIRECTION ? 8U : 6U)
#define LIFT_MANUAL_NEAR_STEPS     4800U /* Strictly less than three 1600-microstep turns. */
#define LIFT_PROBE_HZ              9600U /* 360 rpm: lower-speed current/signal comparison */
#define LIFT_BOTTOM_HZ             9600U /* 360 rpm: provisional reverse-winding detection */
#define LIFT_DOWN_HZ               9600U /* 360 rpm: current/signal trial */
#define LIFT_RAMP_MS               1000U
#define LIFT_SETTLE_MS             150U
#define LIFT_REVERSE_PAUSE_MS      200U
#define LIFT_PROBE_MS              1500U /* Includes ramp and settling; stop early on stall. */
#define LIFT_SEARCH_MS             120000U
#define LIFT_CALIBRATION_MS        360000U
#define LIFT_MIN_TRAVEL_STEPS      1600U
#define LIFT_MAX_TRAVEL_STEPS      300000U
#define LIFT_SAMPLE_MS             5U
#if LIFT_FULL_CURRENT_PROFILE
#define LIFT_STALL_RAW             64U   /* Full-current 360-rpm endpoint contrast trial. */
#define LIFT_HEALTHY_RAW           128U  /* Normal UP ramp previously measured ~165..200. */
#define LIFT_STALL_PERCENT         50U   /* Actual steady UP ~190..260; stalled waveform ~4..130. */
#else
#define LIFT_STALL_RAW             160U  /* Measured 800-rpm low-current stall: ADC 102..124. */
#define LIFT_HEALTHY_RAW           224U  /* Keep a gap above the measured stall range. */
#define LIFT_STALL_PERCENT         25U
#endif
#define LIFT_TRQ_MEDIAN_SAMPLES     5U    /* 5 ms samples; full-current profile rejects <=2 outliers. */
#define LIFT_STALL_CONFIRM_MS      80U
#define LIFT_ADC_RAIL_RAW          4080U
#define LIFT_ADC_RAIL_MS           50U
#define LIFT_TRQ_HIGH_RAW          2600U /* Diagnose two indistinguishable high probe plateaus. */
#define LIFT_ENDPOINT_WINDOW_MIN   1600U
#define LIFT_ESTIMATE_TICK_MS      400U
/* Zero means the detected reverse-winding stall is the provisional bottom.
 * A measured offset can move the software boundary before that event. */
#define LIFT_BOTTOM_OFFSET_STEPS   0U

