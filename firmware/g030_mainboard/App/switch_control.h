#pragma once
#include <stdbool.h>
void SwitchControl_Init(void);
void SwitchControl_TickISR(void);
void SwitchControl_Poll(void);
void SwitchControl_Stop(void);
void SwitchControl_Report(char *buffer, unsigned capacity);
bool SwitchControl_SelfTest(void);
