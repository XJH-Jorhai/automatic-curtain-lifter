#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t IDR, ODR, BSRR, BRR; } GPIO_TypeDef;
extern GPIO_TypeDef fake_a, fake_b, fake_c;
#define GPIOA (&fake_a)
#define GPIOB (&fake_b)
#define GPIOC (&fake_c)
#define DRV_ENABLE_Pin (1U << 0)
#define DRV_NSLEEP_Pin (1U << 1)
#define DRV_NFAULT_Pin (1U << 2)
#define LOW_CURRENT_SEL_Pin (1U << 4)
#define DRV_STEP_Pin (1U << 6)
#define DRV_DIR_Pin (1U << 7)
#define KEY_UP_Pin (1U << 6)
#define KEY_DOWN_Pin (1U << 7)
#define NETCONTROL_BTN_Pin (1U << 13)
uint32_t HAL_GetTick(void);
extern uint32_t fake_primask;
static inline uint32_t __get_PRIMASK(void) { return fake_primask; }
static inline void __disable_irq(void) { fake_primask = 1U; }
static inline void __set_PRIMASK(uint32_t value) { fake_primask = value; }
void HAL_NVIC_ClearPendingIRQ(int irq);
void Error_Handler(void);

