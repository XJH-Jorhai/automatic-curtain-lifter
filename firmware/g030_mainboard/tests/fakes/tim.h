#pragma once
#include "main.h"
typedef struct { uint32_t CR1, DIER, SR, ARR, CNT, EGR; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
extern TIM_TypeDef fake_timer;
extern TIM_HandleTypeDef htim14;
#define TIM14 (&fake_timer)
#define TIM_DIER_UIE 1U
#define TIM_CR1_CEN 1U
#define TIM_CR1_ARPE 128U
#define TIM_EGR_UG 1U
#define TIM14_IRQn 14

