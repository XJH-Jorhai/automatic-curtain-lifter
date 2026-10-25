#include "product_button.h"

void ProductButton_Init(ProductButton *b, bool down, uint32_t now)
{
    *b = (ProductButton){.raw = down, .stable = down, .armed = !down,
        .changed_at = now, .pressed_at = now};
}

unsigned ProductButton_Tick(ProductButton *b, bool down, uint32_t now)
{
    if (down != b->raw) { b->raw = down; b->changed_at = now; }
    if (b->stable != b->raw && (uint32_t)(now - b->changed_at) >= 30U) {
        b->stable = b->raw;
        if (b->stable) { b->pressed_at = now; b->fired = false; }
        else {
            bool short_press = b->armed && !b->fired;
            b->armed = true;
            b->fired = false;
            return short_press ? PRODUCT_BUTTON_SHORT : PRODUCT_BUTTON_NONE;
        }
    }
    if (b->armed && b->stable && !b->fired &&
        (uint32_t)(now - b->pressed_at) >= 5000U) {
        b->fired = true;
        return PRODUCT_BUTTON_LONG;
    }
    return PRODUCT_BUTTON_NONE;
}
