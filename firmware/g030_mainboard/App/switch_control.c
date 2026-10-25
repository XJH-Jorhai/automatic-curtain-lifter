#include "switch_control.h"
#include "main.h"
#include "motor.h"
#include <stdint.h>
#include <stdio.h>

enum { CENTER, UP, DOWN, CONFLICT };
enum { NONE, START_UP, START_DOWN, STOP };
typedef struct {
    uint8_t candidate, ticks, direction;
    bool armed, owned;
} SwitchState;
static SwitchState state;
static volatile bool initialized;
static volatile unsigned pending;
static uint32_t requested_at, starts_up, starts_down, stops, rejected;

static unsigned raw_input(void)
{
    uint32_t pins = GPIOC->IDR;
    return (!(pins & KEY_UP_Pin) ? UP : 0U) |
           (!(pins & KEY_DOWN_Pin) ? DOWN : 0U);
}

/* Called once per SysTick. No GPIO/I2C in this policy; also used by RAM tests.
 * Neutral must be stable for 30 samples before every new direction request.
 * Loss of the active direction stops immediately, without release debounce. */
static unsigned update(SwitchState *s, unsigned raw, bool busy)
{
    if (raw != s->candidate) {
        s->candidate = raw; s->ticks = 1;
    } else if (s->ticks < 30) {
        ++s->ticks;
    }
    if (raw == CONFLICT || (s->owned && raw != s->direction)) {
        bool must_stop = s->owned || (raw == CONFLICT && busy);
        s->armed = s->owned = false;
        return must_stop ? STOP : NONE;
    }
    if (s->owned && !busy) s->owned = false;
    if (raw == CENTER) {
        if (s->ticks >= 30 && !busy) s->armed = true;
        return NONE;
    }
    if (busy && !s->owned) s->armed = false; /* Never queue behind music/motion. */
    if (s->ticks >= 30 && s->armed && !busy) {
        s->armed = false; s->owned = true; s->direction = raw;
        return raw == UP ? START_UP : START_DOWN;
    }
    return NONE;
}

void SwitchControl_Init(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    state = (SwitchState){0};
    pending = 0;
    initialized = true;
    __set_PRIMASK(mask);
}

void SwitchControl_TickISR(void)
{
    if (!initialized) return;
    unsigned action = update(&state, raw_input(), Motor_Active() || pending != NONE);
    if (action == STOP) {
        pending = NONE;
        Motor_Stop();
        ++stops;
    } else if (action == START_UP || action == START_DOWN) {
        pending = action;
        requested_at = HAL_GetTick();
    }
}

/* Start only from the main loop, never during a blocking diagnostic/I2C call.
 * Recheck raw inputs atomically before enabling the driver's wake sequence. */
void SwitchControl_Poll(void)
{
    if (!pending) return;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    unsigned request = pending;
    pending = NONE;
    if (request) {
        unsigned expected = request == START_UP ? UP : DOWN;
        if (raw_input() == expected && (uint32_t)(HAL_GetTick() - requested_at) < 50U &&
            Motor_StartHeld(request == START_DOWN)) {
            if (request == START_UP) ++starts_up; else ++starts_down;
        } else {
            state.owned = state.armed = false;
            ++rejected;
        }
    }
    __set_PRIMASK(mask);
}

void SwitchControl_Stop(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    pending = NONE;
    state.owned = state.armed = false;
    state.ticks = 0;
    Motor_Stop();
    __set_PRIMASK(mask);
}

void SwitchControl_Report(char *buffer, unsigned capacity)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    unsigned raw = raw_input(), armed = state.armed, owned = state.owned, request = pending;
    uint32_t up = starts_up, down = starts_down, stopped = stops, failed = rejected;
    __set_PRIMASK(mask);
    snprintf(buffer, capacity,
             "SW raw=%u armed=%u owned=%u pending=%u up=%lu down=%lu stops=%lu reject=%lu",
             raw, armed, owned, request, (unsigned long)up, (unsigned long)down,
             (unsigned long)stopped, (unsigned long)failed);
}

static bool settle(SwitchState *s, unsigned raw, bool busy, unsigned expected)
{
    for (unsigned i = 0; i < 30; ++i) {
        unsigned result = update(s, raw, busy);
        if (result != (i == 29 ? expected : NONE)) return false;
    }
    return true;
}

bool SwitchControl_SelfTest(void)
{
    for (unsigned direction = UP; direction <= DOWN; ++direction) {
        SwitchState s = {0};
        unsigned start = direction == UP ? START_UP : START_DOWN;
        unsigned other = direction == UP ? DOWN : UP;
        if (!settle(&s, direction, false, NONE) || s.armed) return false; /* Boot held. */
        if (!settle(&s, CENTER, false, NONE) || !s.armed) return false;
        for (unsigned i = 0; i < 20; ++i) /* Contact bounce cannot start. */
            if (update(&s, i & 1 ? direction : CENTER, false) != NONE) return false;
        if (!settle(&s, CENTER, false, NONE)) return false;
        if (!settle(&s, direction, false, start) || !s.owned) return false;
        if (update(&s, other, true) != STOP || s.armed || s.owned) return false;
        if (!settle(&s, other, false, NONE)) return false; /* Direct reversal blocked. */
        if (!settle(&s, CENTER, false, NONE)) return false;
        if (!settle(&s, direction, false, start)) return false;
        if (update(&s, CENTER, true) != STOP) return false; /* First release sample. */
        if (!settle(&s, CENTER, false, NONE)) return false;
        if (!settle(&s, direction, false, start)) return false;
        if (!settle(&s, direction, true, NONE)) return false;
        if (!settle(&s, direction, false, NONE)) return false; /* Limit reached, held. */
        if (!settle(&s, CENTER, false, NONE)) return false;
        if (!settle(&s, direction, true, NONE) || s.armed) return false; /* Music busy. */
        if (!settle(&s, direction, false, NONE)) return false;
        if (update(&s, CONFLICT, true) != STOP || s.armed) return false;
    }
    return true;
}

