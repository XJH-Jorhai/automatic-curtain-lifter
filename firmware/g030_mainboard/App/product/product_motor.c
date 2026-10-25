#include "product_motor.h"
#include "motion_config.h"
#include "motor.h"
#include "motor_tune.h"
#include "main.h"
#include "tim.h"
#include <stddef.h>

typedef enum { PM_IDLE, PM_WAKE, PM_ENABLE, PM_RUN, PM_DONE, PM_FAULT } MotorPhase;
typedef struct {
    MotorPhase phase;
    LiftDrive drive;
    uint32_t started, running_at, serviced, count;
    LiftError error;
    bool high, music, sensing_current;
} ProductMotor;
static volatile ProductMotor motor;
static MotorScore music_score;
static MotorOscillation music_oscillation;
static volatile ProductMusicFeedback music_feedback;
static uint32_t music_note_at;
static uint16_t music_note;
static volatile bool music_sounding, music_ending;

static void off(bool sleep)
{
    TIM14->DIER &= ~TIM_DIER_UIE;
    TIM14->CR1 &= ~TIM_CR1_CEN;
    TIM14->SR = 0U;
    GPIOB->BRR = DRV_ENABLE_Pin | (sleep ? DRV_NSLEEP_Pin : 0U);
    GPIOA->BRR = DRV_STEP_Pin;
    motor.high = false;
}

static void fault(LiftError error)
{
    off(true);
    if (motor.music) {
        music_feedback.state = PRODUCT_MUSIC_FAULT;
        GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
    }
    motor.music = music_sounding = music_ending = false;
    motor.error = error;
    motor.phase = PM_FAULT;
}

void Motor_Stop(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool was_music = motor.music;
    if (htim14.Instance) off(motor.phase == PM_FAULT || was_music);
    else {
        GPIOB->BRR = DRV_ENABLE_Pin | DRV_NSLEEP_Pin;
        GPIOA->BRR = DRV_STEP_Pin;
    }
    if (was_music) {
        music_feedback.state = PRODUCT_MUSIC_STOPPED;
        GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
    }
    motor.music = music_sounding = music_ending = false;
    motor.phase = PM_IDLE;
    __set_PRIMASK(mask);
}

void Motor_Init(void)
{
    Motor_Stop();
    music_feedback = (ProductMusicFeedback){0};
    off(true);
    GPIOA->BRR = DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
    /* Product ramp updates use the timer's shadow ARR to avoid the race of
     * lowering ARR just as CNT passes the new limit. Bench mode is unchanged. */
    TIM14->CR1 |= TIM_CR1_ARPE;
}

bool Motor_Active(void)
{
    MotorPhase phase = motor.phase;
    return phase == PM_WAKE || phase == PM_ENABLE || phase == PM_RUN;
}

void Motor_KeepAlive(void) { motor.serviced = HAL_GetTick(); }

bool ProductMotor_MusicActive(void) { return motor.music && Motor_Active(); }

/* Only Music_PlayStored calls this in Product. No console playback/test entry.
 * Parse before touching outputs; use the existing score and oscillation rules. */
const char *Motor_PlayText(const char *text)
{
    MotorScore pending;
    const char *error = MotorTune_Parse(text, &pending);
    if (error) return error;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (motor.phase != PM_IDLE || !htim14.Instance ||
        (GPIOC->IDR & (KEY_UP_Pin | KEY_DOWN_Pin)) != (KEY_UP_Pin | KEY_DOWN_Pin)) {
        __set_PRIMASK(mask); return "busy_fault_or_key";
    }
    off(true);
    music_score = pending;
    music_oscillation = (MotorOscillation){0};
    music_note = 0U;
    music_sounding = music_ending = false;
    music_feedback = (ProductMusicFeedback){.state = PRODUCT_MUSIC_PLAYING};
    uint32_t now = HAL_GetTick();
    motor = (ProductMotor){.phase = PM_WAKE, .music = true, .started = now,
        .serviced = now, .drive = {.timeout_ms = pending.total_ms + 100U}};
    GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin;
    /* Louder cue uses the existing ~1.50 A working-current setting. */
    GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
    GPIOB->BSRR = DRV_NSLEEP_Pin;
    __set_PRIMASK(mask);
    return NULL;
}

bool ProductMotor_Start(const LiftDrive *drive)
{
    if (!drive || !drive->run || !htim14.Instance || Motor_Active() ||
        (drive->hz != LIFT_UP_HZ && drive->hz != LIFT_PROBE_HZ &&
         drive->hz != LIFT_BOTTOM_HZ && drive->hz != LIFT_DOWN_HZ) ||
        drive->steps > LIFT_MAX_TRAVEL_STEPS || !drive->timeout_ms ||
        drive->timeout_ms > LIFT_SEARCH_MS) return false;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    off(false);
    uint32_t now = HAL_GetTick();
    motor = (ProductMotor){.phase = PM_WAKE, .drive = *drive,
                           .started = now, .serviced = now};
    /* Break away and ramp with the original working current. The lower sensing
     * current is selected once at steady speed, before the settle/ADC window. */
    GPIOA->BSRR = (drive->reverse ? DRV_DIR_Pin : (uint32_t)DRV_DIR_Pin << 16) |
        (uint32_t)LOW_CURRENT_SEL_Pin << 16;
    GPIOB->BSRR = DRV_NSLEEP_Pin;
    __set_PRIMASK(mask);
    return true;
}

/* Round the half-period. Current 360-rpm settings use 52 us halves (~9615 STEP/s).
 * The timer keeps counting throughout the ISR; do not add ISR latency to every
 * edge by resetting CNT. ARR preload applies each change at an update event. */
static uint32_t half_period(uint32_t elapsed)
{
    uint32_t hz = elapsed >= LIFT_RAMP_MS ? motor.drive.hz :
        50U + (motor.drive.hz - 50U) * elapsed / LIFT_RAMP_MS;
    return (500000U + hz / 2U) / hz;
}

static void timer_start(uint32_t half_us)
{
    TIM14->CR1 &= ~TIM_CR1_CEN;
    TIM14->ARR = half_us - 1U;
    TIM14->EGR = TIM_EGR_UG;
    TIM14->CNT = 0U;
    TIM14->SR = 0U;
    HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);
    TIM14->DIER |= TIM_DIER_UIE;
    TIM14->CR1 |= TIM_CR1_CEN;
}

static void music_note_begin(uint32_t now)
{
    TIM14->DIER &= ~TIM_DIER_UIE;
    TIM14->CR1 &= ~TIM_CR1_CEN;
    TIM14->SR = 0U;
    GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin;
    music_oscillation = (MotorOscillation){0};
    music_note_at = now;
    music_ending = false;
    uint32_t hz = music_score.notes[music_note].hz;
    music_sounding = hz != 0U;
    if (hz) {
        uint32_t edges = 4U * TUNE_SWING_STEPS * hz;
        timer_start((1000000U + edges / 2U) / edges);
    }
}

static void music_tick(uint32_t now)
{
    uint32_t elapsed = now - music_note_at;
    uint32_t duration = music_score.notes[music_note].duration_ms;
    if (music_sounding && elapsed >= duration) music_ending = true;
    if (!music_sounding && elapsed >= duration + TUNE_GAP_MS) {
        if (++music_note == music_score.count) {
            off(true);
            GPIOA->BRR = DRV_STEP_Pin | DRV_DIR_Pin | LOW_CURRENT_SEL_Pin;
            music_feedback.state = PRODUCT_MUSIC_DONE;
            motor.music = false;
            motor.phase = PM_IDLE;
        } else music_note_begin(now);
    }
}

void Motor_TickISR(void)
{
    if (!Motor_Active()) return;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = HAL_GetTick(), elapsed = now - motor.started;
    if ((uint32_t)(now - motor.serviced) >= 250U) fault(LIFT_MAIN_STALL);
    else if (elapsed >= motor.drive.timeout_ms) fault(LIFT_TIMEOUT);
    else if (elapsed >= 2U && !(GPIOB->IDR & DRV_NFAULT_Pin)) fault(LIFT_DRIVER_FAULT);
    else if (motor.phase == PM_WAKE && elapsed >= 2U) {
        GPIOB->BSRR = DRV_ENABLE_Pin;
        motor.phase = PM_ENABLE;
    } else if (motor.phase == PM_ENABLE && elapsed >= 3U) {
        motor.phase = PM_RUN;
        motor.running_at = now;
        if (motor.music) music_note_begin(now);
        else timer_start(half_period(0U));
    } else if (motor.phase == PM_RUN) {
        if (motor.music) music_tick(now);
        else {
            uint32_t running_ms = now - motor.running_at;
            if (motor.drive.low_current && !motor.sensing_current && running_ms >= LIFT_RAMP_MS) {
                GPIOA->BSRR = LOW_CURRENT_SEL_Pin;
                motor.sensing_current = true;
            }
            TIM14->ARR = half_period(running_ms) - 1U;
        }
    }
    __set_PRIMASK(mask);
}

static void music_edge(void)
{
    if (!music_sounding) return;
    if (music_feedback.pulses >= TUNE_MAX_PULSES) {
        fault(LIFT_DRIVER_FAULT); return;
    }
    bool finished = MotorTune_OscEdge(&music_oscillation, music_ending);
    music_feedback.offset = music_oscillation.offset;
    if (music_oscillation.high) {
        GPIOA->BSRR = DRV_STEP_Pin;
        ++music_feedback.pulses;
    } else {
        GPIOA->BRR = DRV_STEP_Pin;
        /* Switch direction after the falling edge, as in the original player. */
        GPIOA->BSRR = music_oscillation.reverse ? DRV_DIR_Pin : (uint32_t)DRV_DIR_Pin << 16;
    }
    if (finished) {
        TIM14->DIER &= ~TIM_DIER_UIE;
        TIM14->CR1 &= ~TIM_CR1_CEN;
        music_sounding = false;
    }
}

void ProductMotor_TimerISR(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); /* Atomic with SysTick STOP / key / fault paths. */
    TIM14->SR = 0U;
    if (motor.phase != PM_RUN) { __set_PRIMASK(mask); return; }
    if (!(GPIOB->IDR & DRV_NFAULT_Pin)) fault(LIFT_DRIVER_FAULT);
    else if (motor.music) music_edge();
    else if (motor.high) {
        GPIOA->BRR = DRV_STEP_Pin;
        motor.high = false;
        if (motor.drive.steps && motor.count >= motor.drive.steps) {
            off(false);
            motor.phase = PM_DONE;
        }
    } else {
        GPIOA->BSRR = DRV_STEP_Pin;
        motor.high = true;
        ++motor.count;
    }
    __set_PRIMASK(mask);
}

void ProductMotor_Feedback(LiftFeedback *feedback)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *feedback = (LiftFeedback){.generation = motor.drive.generation,
        .count = motor.count, .active = Motor_Active(), .done = motor.phase == PM_DONE,
        .fault = motor.phase == PM_FAULT, .error = motor.error,
        .running_ms = motor.phase == PM_RUN ? HAL_GetTick() - motor.running_at : 0U};
    __set_PRIMASK(mask);
}

void ProductMotor_MusicFeedback(ProductMusicFeedback *feedback)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *feedback = music_feedback;
    __set_PRIMASK(mask);
}

