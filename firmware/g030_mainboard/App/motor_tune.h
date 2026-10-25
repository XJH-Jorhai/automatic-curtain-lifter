#pragma once
#include <stdbool.h>
#include <stdint.h>
#define TUNE_MAX_NOTES 32U
#define TUNE_MAX_MS 10000U
#define TUNE_GAP_MS 30U
#define TUNE_MAX_PULSES 20000U
/* Louder startup listening trial: five eighth-steps out and back, same pitch.
 * The requested 7/10 is a listening target, not a measured acoustic level. */
#define TUNE_SWING_STEPS 5U
typedef struct { uint16_t hz, duration_ms; } MotorNote;
typedef struct {
    MotorNote notes[TUNE_MAX_NOTES];
    uint32_t total_ms;
    uint16_t count;
} MotorScore;
typedef struct { bool high, reverse; int8_t offset; } MotorOscillation;
bool MotorTune_OscEdge(MotorOscillation *state, bool ending);
/* C4F4G4 or T120 C4/8 D#4/8 R/4 Bb3/4.
 * Default 120 BPM, quarter notes. C3..B5, /1 /2 /4 /8 /16 /32, optional dot. */
const char *MotorTune_Parse(const char *text, MotorScore *score);
bool MotorTune_SelfTest(void);
