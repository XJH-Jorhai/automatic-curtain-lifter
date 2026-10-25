#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { PRODUCT_BUTTON_NONE, PRODUCT_BUTTON_SHORT, PRODUCT_BUTTON_LONG };
typedef struct {
    uint32_t changed_at, pressed_at;
    bool raw, stable, armed, fired;
} ProductButton;
void ProductButton_Init(ProductButton *button, bool down, uint32_t now);
unsigned ProductButton_Tick(ProductButton *button, bool down, uint32_t now);
