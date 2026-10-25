#include "motor.h"
#include "main.h"
#include "tim.h"
#include "motor_tune.h"
#include "music_scores.h"
#include <stdio.h>

/* Bounded bench runs only, TIM14 edges + 1 ms safety service.
 * State-machine self-tests use local memory and never call GPIO APIs.
 * No actuator/end-stop control or production motion planner is implemented. */
typedef enum { IDLE, WAKING, ENABLING, RUNNING, FAULT } Phase;
/* Driver enable settling only; the 500 ms diagnostic dwell is removed. */
#define MOTION_ENABLE_SETTLE_MS 1U
typedef struct {
    Phase phase;
    uint32_t start, edge, interval, goal, count, deadline;
    bool step, enable, awake, reverse, held;
} MotorState;
static MotorState live;
static volatile uint32_t serviced;
static volatile unsigned stop_reason; /* 0 boot, 1 done, 2 fault, 3 timeout, 4 stop, 5 main stall, 6 hold limit */
static volatile bool tune_active, tune_sounding;
static unsigned tune_index;
static uint32_t note_start;
static MotorScore active_score;
static MotorOscillation oscillation;
static volatile bool tune_ending;
static volatile bool motion_timer;

/* Disable coils before changing STEP/DIR/current selection. Keeping nSLEEP
 * untouched preserves the indexer without holding current; never pulse it low
 * on the normal stop/start path. Faults and music retain full sleep behavior. */
static void outputs_disable(bool sleep)
{
    if (htim14.Instance) (void)HAL_TIM_Base_Stop_IT(&htim14);
    motion_timer = false;
    tune_active = tune_sounding = false;
    GPIOB->BRR = DRV_ENABLE_Pin | (sleep ? DRV_NSLEEP_Pin : 0U);
    GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
}

static void outputs_off(void) { outputs_disable(true); }

static void stop(MotorState *m, Phase reason)
{
    m->step = m->enable = m->awake = false;
    m->phase = reason;
}

/* 1 second linear frequency ramp, 50 -> 2667 pulses/s.
 * TIM14's 1 MHz timebase schedules each edge; SysTick never drives STEP. */
static uint32_t motion_period(uint32_t elapsed_ms)
{
    uint32_t hz = elapsed_ms >= MOTOR_RAMP_MS ? MOTOR_JOG_HZ :
                  50U + (MOTOR_JOG_HZ - 50U) * elapsed_ms / MOTOR_RAMP_MS;
    return (1000000U + hz - 1U) / hz;
}

/* Pure edge planner, also exercised by the RAM self-test. */
static uint32_t motion_edge(MotorState *m, uint32_t elapsed_ms)
{
    if (m->phase != RUNNING) return 0;
    if (m->step) {
        m->step = false;
        if (!m->held && m->count >= m->goal) { stop(m, IDLE); return 0; }
        m->interval = motion_period(elapsed_ms);
        return m->interval / 2U;
    }
    if (!m->held && m->count >= m->goal) { stop(m, FAULT); return 0; }
    m->step = true;
    ++m->count;
    return m->interval - m->interval / 2U;
}

static bool begin(MotorState *m, uint32_t now, uint32_t steps,
                  uint32_t hz, bool reverse)
{
    if (m->phase != IDLE || steps == 0 || steps > 400 || hz != MOTOR_JOG_HZ)
        return false;
    *m = (MotorState){.phase = WAKING, .start = now, .edge = now,
                     .interval = motion_period(0), .goal = steps, .awake = true,
                     .reverse = reverse,
                     .deadline = 2500U};
    return true;
}

static bool begin_held(MotorState *m, uint32_t now, bool reverse)
{
    if (!begin(m, now, 400, MOTOR_JOG_HZ, reverse)) return false;
    m->held = true;
    m->goal = 0; /* No step target; bounded by release or the 30 s hold limit. */
    m->deadline = 30000U;
    return true;
}

static void tick(MotorState *m, uint32_t now, bool fault)
{
    if (m->phase == IDLE || m->phase == FAULT) return;
    if ((uint32_t)(now - m->start) >= m->deadline) {
        stop(m, m->held ? IDLE : FAULT); return;
    }
    /* Do not interpret the driver's sleep/wake transient as a run-time fault. */
    if (m->phase == WAKING) {
        if ((uint32_t)(now - m->edge) >= 2U) {
            if (fault) { stop(m, FAULT); return; }
            m->enable = true; m->phase = ENABLING; m->edge = now;
        }
        return;
    }
    if (fault) { stop(m, FAULT); return; }
    if (m->phase == ENABLING) {
        if ((uint32_t)(now - m->edge) >= MOTION_ENABLE_SETTLE_MS) {
            m->phase = RUNNING; m->edge = now;
        }
        return;
    }
    /* RUNNING edges belong exclusively to TIM14. */
}

static bool motion_timer_start(void)
{
    (void)HAL_TIM_Base_Stop_IT(&htim14);
    __HAL_TIM_SET_AUTORELOAD(&htim14, live.interval / 2U - 1U);
    __HAL_TIM_SET_COUNTER(&htim14, 0);
    __HAL_TIM_CLEAR_FLAG(&htim14, TIM_FLAG_UPDATE);
    HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);
    motion_timer = true;
    if (HAL_TIM_Base_Start_IT(&htim14) == HAL_OK) return true;
    stop(&live, FAULT); stop_reason = 2; outputs_off();
    return false;
}

void Motor_Stop(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool sleep = live.phase == FAULT || tune_active;
    stop(&live, IDLE);
    outputs_disable(sleep);
    stop_reason = 4;
    __set_PRIMASK(mask);
}

void Motor_Init(void) { Motor_Stop(); outputs_off(); stop_reason = 0; }

static void note_begin(uint32_t now)
{
    (void)HAL_TIM_Base_Stop_IT(&htim14);
    GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin;
    oscillation = (MotorOscillation){0};
    live.step = live.reverse = false;
    note_start = now;
    tune_ending = false;
    uint32_t hz = active_score.notes[tune_index].hz;
    tune_sounding = hz != 0;
    if (!hz) return; /* Rest: no pulses. */
    /* Preserve note pitch as swing amplitude changes. Timer base is 1 MHz. */
    uint32_t edges_per_second = 4U * TUNE_SWING_STEPS * hz;
    uint32_t edge_us = (1000000U + edges_per_second / 2U) / edges_per_second;
    __HAL_TIM_SET_AUTORELOAD(&htim14, edge_us - 1U);
    __HAL_TIM_SET_COUNTER(&htim14, 0);
    __HAL_TIM_CLEAR_FLAG(&htim14, TIM_FLAG_UPDATE);
    if (HAL_TIM_Base_Start_IT(&htim14) != HAL_OK) {
        stop(&live, FAULT); stop_reason = 2; outputs_off();
    }
}

const char *Motor_PlayText(const char *text)
{
    MotorScore pending;
    const char *error = MotorTune_Parse(text, &pending);
    if (error) return error; /* Parse completely before touching any motor output. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (live.phase != IDLE || !htim14.Instance) {
        __set_PRIMASK(mask); return "busy_or_fault";
    }
    outputs_off();
    active_score = pending;
    uint32_t now = HAL_GetTick();
    live = (MotorState){.phase = WAKING, .start = now, .edge = now,
                       .awake = true, .deadline = pending.total_ms + 100U};
    oscillation = (MotorOscillation){0};
    tune_index = 0;
    tune_active = true;
    serviced = now; stop_reason = 0;
    GPIOA->BSRR = LOW_CURRENT_SEL_Pin;
    GPIOB->BSRR = DRV_NSLEEP_Pin;
    __set_PRIMASK(mask);
    return NULL;
}

bool Motor_PlayPhrase(void)
{
    return Motor_PlayText(MUSIC_TEST_DOUDIZHU) == NULL;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); /* Atomic with SysTick STOP/fault handling. */
    if (timer == &htim14 && motion_timer && !tune_active && live.phase == RUNNING) {
        uint32_t now = HAL_GetTick();
        if (!(GPIOB->IDR & DRV_NFAULT_Pin) ||
            (uint32_t)(now - serviced) >= 250U ||
            (uint32_t)(now - live.start) >= live.deadline) {
            stop_reason = !(GPIOB->IDR & DRV_NFAULT_Pin) ? 2 :
                          ((uint32_t)(now - serviced) >= 250U ? 5 :
                           (live.held ? 6 : 3));
            stop(&live, stop_reason == 6 ? IDLE : FAULT);
            outputs_disable(stop_reason != 6);
        } else {
            uint32_t delay_us = motion_edge(&live, now - live.edge);
            GPIOA->BSRR = live.step ? DRV_STEP_Pin : (uint32_t)DRV_STEP_Pin << 16;
            if (!delay_us) {
                stop_reason = live.phase == IDLE ? 1 : 2;
                outputs_disable(live.phase == FAULT);
            } else {
                /* Restart interval from this serviced edge, never catch up.
                 * ARR preload is disabled in CubeMX TIM14 configuration. */
                __HAL_TIM_DISABLE(&htim14);
                __HAL_TIM_SET_AUTORELOAD(&htim14, delay_us - 1U);
                __HAL_TIM_SET_COUNTER(&htim14, 0);
                __HAL_TIM_CLEAR_FLAG(&htim14, TIM_FLAG_UPDATE);
                __HAL_TIM_ENABLE(&htim14);
            }
        }
        __set_PRIMASK(mask); return;
    }
    if (timer != &htim14 || !tune_active || !tune_sounding || live.phase != RUNNING) {
        __set_PRIMASK(mask); return;
    }
    if (!(GPIOB->IDR & DRV_NFAULT_Pin) || live.count >= TUNE_MAX_PULSES) {
        stop(&live, FAULT); stop_reason = 2; outputs_off();
        __set_PRIMASK(mask); return;
    }
    bool finished = MotorTune_OscEdge(&oscillation, tune_ending);
    live.step = oscillation.high;
    if (live.step) {
        GPIOA->BSRR = DRV_STEP_Pin;
        ++live.count;
    } else {
        GPIOA->BRR = DRV_STEP_Pin;
        /* Change DIR only at a falling edge; next rise is >=84 us away
         * with the current three-microstep swing and B5 maximum pitch. */
        live.reverse = oscillation.reverse;
        GPIOA->BSRR = live.reverse ? DRV_DIR_Pin : (uint32_t)DRV_DIR_Pin << 16;
    }
    if (finished) {
        (void)HAL_TIM_Base_Stop_IT(&htim14);
        tune_sounding = false;
    }
    __set_PRIMASK(mask);
}

static bool start_motion(uint32_t steps, uint32_t rate_hz, bool reverse, bool held)
{
    /* Bounded diagnostic: <=400 eighth-steps; held mode has a 30 s limit.
     * 100 rpm (rounded STEP rate) assumes 1.8 deg, 1/8 step. */
    if (steps > 400 || rate_hz != MOTOR_JOG_HZ || !htim14.Instance) return false;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = HAL_GetTick();
    bool accepted = held ? begin_held(&live, now, reverse) :
                           begin(&live, now, steps, rate_hz, reverse);
    if (accepted) {
        outputs_disable(false);
        /* Normal motion: PA4 low selects ~1.50 A full-scale (VREF measured 1.978 V).
         * outputs_disable() already drove PA4 low with ENABLE disabled.
         * Music keeps its separate PA4-high ~0.42 A setting. */
        GPIOA->BSRR = reverse ? DRV_DIR_Pin : 0U;
        GPIOB->BSRR = DRV_NSLEEP_Pin;
        serviced = now;
        stop_reason = 0;
    }
    __set_PRIMASK(mask);
    return accepted;
}

bool Motor_Start(uint32_t steps, uint32_t rate_hz, bool reverse)
{
    return start_motion(steps, rate_hz, reverse, false);
}

bool Motor_StartHeld(bool reverse)
{
    return start_motion(400, MOTOR_JOG_HZ, reverse, true);
}

bool Motor_StartTimed(bool reverse, uint32_t duration_ms)
{
    if (duration_ms == 0 || duration_ms > MOTOR_TIMED_MAX_MS) return false;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool accepted = start_motion(400, MOTOR_JOG_HZ, reverse, true);
    if (accepted) live.deadline = duration_ms;
    __set_PRIMASK(mask);
    return accepted;
}

bool Motor_Active(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool active = live.phase == WAKING || live.phase == ENABLING || live.phase == RUNNING;
    __set_PRIMASK(mask);
    return active;
}

void Motor_KeepAlive(void) { serviced = HAL_GetTick(); }

void Motor_TickISR(void)
{
    if (live.phase == IDLE || live.phase == FAULT) return;
    uint32_t now = HAL_GetTick();
    bool fault = !(GPIOB->IDR & DRV_NFAULT_Pin);
    if (tune_active) {
        if ((uint32_t)(now - serviced) >= 250U ||
            (uint32_t)(now - live.start) >= live.deadline ||
            (fault && (uint32_t)(now - live.start) >= 2U)) {
            stop_reason = (uint32_t)(now - serviced) >= 250U ? 5 :
                          (fault ? 2 : 3);
            stop(&live, FAULT); outputs_off(); return;
        }
        if (live.phase == WAKING && (uint32_t)(now - live.edge) >= 2U) {
            GPIOB->BSRR = DRV_ENABLE_Pin;
            live.enable = true; live.phase = ENABLING; live.edge = now;
        } else if (live.phase == ENABLING && (uint32_t)(now - live.edge) >= 1U) {
            live.phase = RUNNING; note_begin(now);
        } else if (live.phase == RUNNING) {
            uint32_t elapsed = now - note_start;
            if (tune_sounding && elapsed >= active_score.notes[tune_index].duration_ms) {
                tune_ending = true; /* ISR completes the reverse step before resting. */
            }
            if (!tune_sounding && elapsed >= (uint32_t)active_score.notes[tune_index].duration_ms + TUNE_GAP_MS) {
                if (++tune_index == active_score.count) {
                    stop(&live, IDLE); stop_reason = 1; outputs_off();
                } else {
                    note_begin(now);
                }
            }
        }
        return;
    }
    /* Atomic with the timer's completion/fault path. In particular, a timer
     * completion must not race the decision to start the timer here. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((uint32_t)(now - serviced) >= 250U) {
        stop(&live, FAULT); stop_reason = 5;
    } else {
        tick(&live, now, fault);
        if (live.phase == FAULT)
            stop_reason = (uint32_t)(now - live.start) >= live.deadline ? 3 : 2;
        else if (live.phase == IDLE)
            stop_reason = live.held && (uint32_t)(now - live.start) >= live.deadline ? 6 : 1;
    }
    if (live.phase == IDLE || live.phase == FAULT) {
        outputs_disable(live.phase == FAULT);
    } else {
        GPIOB->BSRR = live.enable ? DRV_ENABLE_Pin : (uint32_t)DRV_ENABLE_Pin << 16;
        if (live.phase == RUNNING && !motion_timer) (void)motion_timer_start();
    }
    __set_PRIMASK(mask);
}

void Motor_Report(char *buffer, unsigned capacity)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    MotorState snapshot = live;
    unsigned reason = stop_reason;
    __set_PRIMASK(mask);
    snprintf(buffer, capacity, "MOTOR phase=%u count=%lu goal=%lu reason=%u dir=%u nFAULT=%u EN=%u SLEEP=%u LOW=%u tune=%u note=%u offset=%d held=%u",
             (unsigned)snapshot.phase, (unsigned long)snapshot.count, (unsigned long)snapshot.goal,
             reason, snapshot.reverse, !!(GPIOB->IDR & DRV_NFAULT_Pin),
             !!(GPIOB->IDR & DRV_ENABLE_Pin), !!(GPIOB->IDR & DRV_NSLEEP_Pin),
             !!(GPIOA->IDR & LOW_CURRENT_SEL_Pin), (unsigned)tune_active, tune_index,
             (int)oscillation.offset, snapshot.held);
}

bool Motor_IOTest(void)
{
    Motor_Stop();
    outputs_off(); /* This explicit GPIO test requires both controls low. */
    HAL_Delay(2);
    bool good = true;
    for (unsigned direction = 0; direction < 2 && good; ++direction) {
        HAL_GPIO_WritePin(GPIOA, DRV_DIR_Pin, direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_Delay(1);
        good = HAL_GPIO_ReadPin(GPIOA, DRV_DIR_Pin) ==
               (direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
        for (unsigned edge = 0; edge < 8 && good; ++edge) {
            /* Two independent hardware controls stay low throughout this test. */
            if (GPIOB->IDR & (DRV_ENABLE_Pin | DRV_NSLEEP_Pin)) { good = false; break; }
            HAL_GPIO_WritePin(GPIOA, DRV_STEP_Pin, (edge & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_Delay(2);
            good = HAL_GPIO_ReadPin(GPIOA, DRV_STEP_Pin) ==
                   ((edge & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
    }
    Motor_Stop();
    return good && !(GPIOA->IDR & (DRV_STEP_Pin | DRV_DIR_Pin)) &&
           !(GPIOB->IDR & (DRV_ENABLE_Pin | DRV_NSLEEP_Pin));
}

bool Motor_SelfTest(void)
{
    MotorState m = {0};
    if (begin(&m, 0, 0, MOTOR_JOG_HZ, false) ||
        begin(&m, 0, 401, MOTOR_JOG_HZ, false) ||
        begin(&m, 0, 400, 0, false)) return false;
    if (motion_period(0) != 20000 || motion_period(500) != 737 ||
        motion_period(1000) != 375 || motion_period(2000) != 375) return false;
    for (unsigned dir = 0; dir < 2; ++dir) {
        for (unsigned goal_index = 0; goal_index < 2; ++goal_index) {
            unsigned goal = goal_index ? 400 : 1;
            m = (MotorState){0};
            uint32_t start = 0xfffffff0U;
            if (!begin(&m, start, goal, MOTOR_JOG_HZ, dir != 0)) return false;
            tick(&m, start + 1, false);
            if (m.enable) return false;
            tick(&m, start + 2, false);
            if (!m.enable || m.step) return false;
            if (begin(&m, start + 2, 1, MOTOR_JOG_HZ, false)) return false;
            tick(&m, start + 3, false);
            if (m.phase != RUNNING || m.count) return false;
            uint32_t us = m.interval / 2, previous_period = m.interval;
            unsigned rises = 0, falls = 0;
            while (m.phase == RUNNING && us < 2000000U) {
                tick(&m, start + 3 + us / 1000U, false);
                uint32_t delay = motion_edge(&m, us / 1000U);
                if (m.step) ++rises; else ++falls;
                if (m.interval > previous_period || m.interval < 375) return false;
                previous_period = m.interval;
                if (delay && (delay < 187 || delay > 10000)) return false;
                us += delay;
            }
            if (m.phase != IDLE || rises != goal || falls != goal ||
                m.count != goal || m.step || m.enable || m.awake) return false;
            if (goal == 400 && (us < 500000 || us > 600000))
                return false;
            if (motion_edge(&m, 999) || m.count != goal) return false;
        }
    }
    /* Held mode must pass the old 400-step limit, stop on release, and
     * expire at 30 s across tick wrap without latching a driver fault. */
    for (unsigned dir = 0; dir < 2; ++dir) {
        m = (MotorState){0};
        uint32_t start = 0xfffffff0U;
        if (!begin_held(&m, start, dir != 0) || !m.held || m.goal) return false;
        tick(&m, start + 2, false);
        tick(&m, start + 3, false);
        uint32_t us = m.interval / 2;
        while (us < 2000000U && m.phase == RUNNING) {
            tick(&m, start + 3 + us / 1000U, false);
            uint32_t delay = motion_edge(&m, us / 1000U);
            if (!delay) return false;
            us += delay;
        }
        if (m.phase != RUNNING || m.count < 3900 || m.count > 4150) return false;
        uint32_t count = m.count;
        stop(&m, IDLE);
        if (motion_edge(&m, 2000) || m.count != count || m.enable) return false;
        if (!begin_held(&m, start, dir != 0)) return false;
        tick(&m, start + 2, false);
        tick(&m, start + 3, false);
        tick(&m, start + 29999U, false);
        if (m.phase != RUNNING) return false;
        tick(&m, start + 30000U, false);
        if (m.phase != IDLE || m.enable || m.step || m.awake ||
            motion_edge(&m, 30001U)) return false;
    }
    /* Timed App runs: absolute 2s cutoff including uint32 tick wrap. */
    for (unsigned dir = 0; dir < 2; ++dir) {
        m = (MotorState){0};
        uint32_t start = 0xfffffff0U;
        if (!begin_held(&m, start, dir != 0)) return false;
        m.deadline = MOTOR_TIMED_MAX_MS;
        tick(&m, start + 2, false); tick(&m, start + 3, false);
        tick(&m, start + 1999, false);
        if (m.phase != RUNNING) return false;
        tick(&m, start + 2000, false);
        if (m.phase != IDLE || m.enable || m.step || motion_edge(&m, 2001)) return false;
    }
    /* Fault, main deadline, stop during enable and stop on STEP high. */
    m = (MotorState){0};
    if (!begin(&m, 0, 400, MOTOR_JOG_HZ, false)) return false;
    tick(&m, 2, true);
    if (m.phase != FAULT || m.enable || m.awake) return false;
    if (begin(&m, 3, 1, MOTOR_JOG_HZ, false)) return false;
    stop(&m, IDLE);
    if (!begin(&m, 0, 400, MOTOR_JOG_HZ, false)) return false;
    tick(&m, m.deadline, false);
    if (m.phase != FAULT || m.count) return false;
    for (unsigned high = 0; high < 2; ++high) {
        stop(&m, IDLE);
        if (!begin(&m, 0, 400, MOTOR_JOG_HZ, false)) return false;
        tick(&m, 2, false);
        if (high) {
            tick(&m, 3, false);
            if (!motion_edge(&m, 10) || !m.step) return false;
        }
        uint32_t count = m.count;
        stop(&m, IDLE);
        tick(&m, 4, false);
        if (motion_edge(&m, 20) || m.step || m.enable || m.awake || m.count != count)
            return false;
    }
    return true;
}
