#include "motor_tune.h"
#include "music_scores.h"
#include <string.h>

static bool separator(char c) { return c == ' ' || c == ',' || c == '\t'; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
bool MotorTune_OscEdge(MotorOscillation *state, bool ending)
{
    state->high = !state->high;
    if (state->high) state->offset += state->reverse ? -1 : 1;
    else if (state->offset == TUNE_SWING_STEPS) state->reverse = true;
    else if (state->offset == 0) state->reverse = false;
    return ending && !state->high && state->offset == 0;
}
static bool number(const char **text, unsigned *value)
{
    const char *p = *text;
    *value = 0;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        *value = *value * 10U + (unsigned)(*p++ - '0');
        if (*value > 9999U) return false;
    }
    *text = p; return true;
}

const char *MotorTune_Parse(const char *text, MotorScore *score)
{
    static const uint16_t hz[] = {
        131,139,147,156,165,175,185,196,208,220,233,247,
        262,277,294,311,330,349,370,392,415,440,466,494,
        523,554,587,622,659,698,740,784,831,880,932,988
    };
    if (!text || !score) return "argument";
    memset(score, 0, sizeof(*score));
    unsigned bpm = 120, pulse_budget = 0;
    while (separator(*text)) ++text;
    if (upper(*text) == 'T') {
        ++text;
        if (!number(&text, &bpm) || bpm < 40 || bpm > 300 || !separator(*text))
            return "tempo_40_300";
    }
    while (*text) {
        while (separator(*text)) ++text;
        if (!*text) break;
        if (score->count >= TUNE_MAX_NOTES) return "max_32_notes";
        char note = upper(*text++);
        bool rest = note == 'R' || note == 'P';
        int semitone = 0;
        if (!rest) {
            switch (note) {
            case 'C': semitone = 0; break;
            case 'D': semitone = 2; break;
            case 'E': semitone = 4; break;
            case 'F': semitone = 5; break;
            case 'G': semitone = 7; break;
            case 'A': semitone = 9; break;
            case 'B': semitone = 11; break;
            default: return "note";
            }
            if (*text == '#') { ++semitone; ++text; }
            else if (*text == 'b') { --semitone; ++text; }
            if (*text < '3' || *text > '5') return "octave_3_5";
            semitone += (*text++ - '3') * 12;
            if (semitone < 0 || semitone >= (int)(sizeof(hz) / sizeof(hz[0])))
                return "range_C3_B5";
        }
        unsigned denominator = 4;
        if (*text == '/') {
            ++text;
            if (!number(&text, &denominator) ||
                !(denominator == 1 || denominator == 2 || denominator == 4 ||
                  denominator == 8 || denominator == 16 || denominator == 32))
                return "duration_1_2_4_8_16_32";
        }
        unsigned ms = (240000U + bpm * denominator / 2U) / (bpm * denominator);
        if (*text == '.') { ms = (ms * 3U + 1U) / 2U; ++text; }
        unsigned frequency = rest ? 0 : hz[semitone];
        score->total_ms += ms + TUNE_GAP_MS;
        pulse_budget += (2U * TUNE_SWING_STEPS * frequency * ms + 999U) / 1000U +
                        (rest ? 0U : 2U * TUNE_SWING_STEPS);
        if (score->total_ms > TUNE_MAX_MS) return "max_10_seconds";
        if (pulse_budget > TUNE_MAX_PULSES) return "pulse_limit";
        score->notes[score->count++] = (MotorNote){(uint16_t)frequency, (uint16_t)ms};
    }
    return score->count ? NULL : "empty";
}

bool MotorTune_SelfTest(void)
{
    /* End requested across many phases; always complete the symmetric cycle. */
    for (unsigned end_at = 0; end_at < 40; ++end_at) {
        MotorOscillation osc = {0};
        bool done = false;
        for (unsigned edge = 0; edge < end_at + 4U * TUNE_SWING_STEPS; ++edge) {
            done = MotorTune_OscEdge(&osc, edge >= end_at);
            if (osc.offset < 0 || osc.offset > (int)TUNE_SWING_STEPS) return false;
            if (done) break;
        }
        if (!done || osc.high || osc.reverse || osc.offset) return false;
    }
    MotorScore s;
    if (MotorTune_Parse(MUSIC_STARTUP_B1, &s) || s.count != 9 || s.total_ms != 1177 ||
        s.notes[2].hz != 370 || s.notes[4].hz != 415 ||
        s.notes[3].hz || s.notes[6].hz || s.notes[8].hz != 523) return false;
    if (MotorTune_Parse("c4f4g4", &s) || s.count != 3 || s.total_ms != 1590 ||
        s.notes[0].hz != 262 || s.notes[1].hz != 349 || s.notes[2].hz != 392) return false;
    if (MotorTune_Parse("T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G3/4.", &s) ||
        s.count != 11 || s.total_ms != 3530 ||
        s.notes[5].hz != 220 || s.notes[10].hz != 196) return false;
    if (MotorTune_Parse("T120 C#4/8 Db4/8 Bb3/4 R/4. P/16", &s) ||
        s.notes[0].hz != s.notes[1].hz || s.notes[2].hz != 233 ||
        s.notes[3].hz || s.notes[3].duration_ms != 750 || s.notes[4].hz) return false;
    const char *bad[] = {"", "   ", "C2", "B6", "Cb3", "B#5", "C4/0", "C4/3",
        "T0 C4", "T999999999 C4", "T120C4", "C4?", "C44", "C4/9999999",
        "T40 C4/1 C4/1", "R4", "C4..", "T120", "C4/8garbage"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (!MotorTune_Parse(bad[i], &s)) return false;
    char many[100];
    for (unsigned i = 0; i < 33; ++i) memcpy(many + i * 3, "R/8", 3);
    many[99] = 0;
    if (!MotorTune_Parse(many, &s)) return false;
    return true;
}

