#pragma once
#include <stdbool.h>
#include <stddef.h>
void BoardTest_Init(void);
bool BoardTest_Command(const char *command, char *reply, size_t capacity);
