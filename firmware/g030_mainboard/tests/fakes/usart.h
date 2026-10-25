#pragma once
#include "main.h"
typedef struct { void *Instance; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart1, huart2;
#define USART1 ((void *)1)
#define USART2 ((void *)2)
#define UART_FLAG_ORE 1
#define __HAL_UART_GET_FLAG(uart, flag) (false)
#define __HAL_UART_CLEAR_OREFLAG(uart) ((void)0)
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte, uint16_t size);

