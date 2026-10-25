#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MOTOR_JOG_HZ 2667U
#define MOTOR_RAMP_MS 1000U
/* Bench motion cap; not a calibrated travel time or position. */
#define MOTOR_TIMED_MAX_MS 2000U
void Motor_Init(void);
void Motor_Stop(void);
bool Motor_Start(uint32_t steps, uint32_t rate_hz, bool reverse);
bool Motor_StartHeld(bool reverse);
bool Motor_StartTimed(bool reverse, uint32_t duration_ms);
bool Motor_SelfTest(void);
bool Motor_IOTest(void);
void Motor_TickISR(void);
void Motor_KeepAlive(void);
bool Motor_Active(void);
void Motor_Report(char *buffer, unsigned capacity);
bool Motor_PlayPhrase(void);
const char *Motor_PlayText(const char *text);
