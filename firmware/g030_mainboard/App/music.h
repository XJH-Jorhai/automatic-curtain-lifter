#pragma once
#include <stddef.h>
/* Main-loop context only, never ISR. NULL means success, otherwise a static
 * error string. No periodic task or implicit writes. The application now
 * explicitly calls PlayStored once after startup initialization.
 * Save accepts a NUL-terminated score up to 191 bytes. */
const char *Music_SaveScore(const char *text);
const char *Music_GetScore(char *text, size_t capacity);
const char *Music_PlayStored(void);
void Music_Stop(void);
