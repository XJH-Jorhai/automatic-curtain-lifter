#pragma once
/* Approved startup cue. Provision explicitly with Music_SaveScore; never write
 * this default from boot or a periodic task. Playback loads the EEPROM record. */
#define MUSIC_STARTUP_B1 "T232 C4/16 E4/8 F#4/16 R/16 Ab4/8 G4/16 R/16 B4/16 C5/4"
/* Retained solely as the manual MOTOR TUNE bench test. */
#define MUSIC_TEST_DOUDIZHU "T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G3/4."
