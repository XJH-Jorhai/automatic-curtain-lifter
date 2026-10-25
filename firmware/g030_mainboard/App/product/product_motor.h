#pragma once
#include "motion.h"

typedef enum {
    PRODUCT_MUSIC_IDLE, PRODUCT_MUSIC_PLAYING, PRODUCT_MUSIC_DONE,
    PRODUCT_MUSIC_STOPPED, PRODUCT_MUSIC_FAULT
} ProductMusicState;
typedef struct {
    ProductMusicState state;
    uint32_t pulses;
    int32_t offset;
} ProductMusicFeedback;

bool ProductMotor_Start(const LiftDrive *drive);
void ProductMotor_Feedback(LiftFeedback *feedback);
void ProductMotor_MusicFeedback(ProductMusicFeedback *feedback);
bool ProductMotor_MusicActive(void);
void ProductMotor_TimerISR(void);

