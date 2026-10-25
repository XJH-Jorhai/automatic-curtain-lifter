#include "music.h"
#include "eeprom.h"
#include "motor.h"
#include "motor_tune.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool loaded;
static int selected = -1;
static uint32_t generation;
static char cached[192];

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8U * i));
}
/* CRC includes schema, length, generation and payload; excludes magic/CRC. */
static uint32_t crc(const uint8_t *r)
{
    uint32_t c = 0xffffffffU;
    for (unsigned i = 4; i < 208; ++i) {
        if (i >= 12 && i < 16) continue;
        c ^= r[i];
        for (unsigned b = 0; b < 8; ++b)
            c = (c >> 1) ^ ((c & 1U) ? 0xedb88320U : 0);
    }
    return ~c;
}
static bool valid(const uint8_t *r)
{
    MotorScore parsed;
    unsigned len = r[5];
    return !memcmp(r, "MSC1", 4) && r[4] == 1 && len && len < 192 &&
           r[6] == 0 && r[7] == 0 && r[16 + len] == 0 &&
           strlen((const char *)r + 16) == len && get32(r + 12) == crc(r) &&
           !MotorTune_Parse((const char *)r + 16, &parsed);
}
static const char *load(void)
{
    if (loaded) return NULL;
    uint8_t r[208];
    selected = -1;
    cached[0] = 0;
    generation = 0;
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (!Eeprom_Read(slot * 256U, r, sizeof(r))) return "EEPROM_READ";
        if (valid(r) && (selected < 0 ||
            (int32_t)(get32(r + 8) - generation) > 0)) {
            selected = (int)slot;
            generation = get32(r + 8);
            memcpy(cached, r + 16, sizeof(cached));
        }
    }
    loaded = true;
    return NULL;
}
const char *Music_SaveScore(const char *text)
{
    if (Motor_Active()) return "MOTOR_BUSY";
    if (!text) return "SCORE_NULL";
    unsigned len = 0;
    while (len < sizeof(cached) && text[len]) ++len;
    if (!len || len >= sizeof(cached)) return "SCORE_LENGTH";
    MotorScore parsed;
    const char *error = MotorTune_Parse(text, &parsed);
    if (error) return error;
    error = load();
    if (error) return error;
    if (selected >= 0 && !strcmp(cached, text)) return NULL;
    uint8_t record[208] = {0};
    memcpy(record, "MSC1", 4);
    record[4] = 1; record[5] = (uint8_t)len;
    put32(record + 8, generation + 1U);
    memcpy(record + 16, text, len + 1U);
    put32(record + 12, crc(record));
    unsigned next = selected == 0 ? 1U : 0U;
    if (!Eeprom_WriteScoreSlot(next, record)) {
        /* ACK/readback may fail after a commit. Reconcile on next explicit
         * request, never retry writes or reload from a periodic task. */
        loaded = false;
        return "EEPROM_SAVE_OR_RATE_LIMIT";
    }
    memcpy(cached, record + 16, sizeof(cached));
    generation++;
    selected = (int)next;
    return NULL;
}
const char *Music_GetScore(char *text, size_t capacity)
{
    if (Motor_Active()) return "MOTOR_BUSY";
    const char *error = load();
    if (error) return error;
    if (selected < 0) return "SCORE_NOT_SET";
    if (!text || capacity <= strlen(cached)) return "BUFFER_SMALL";
    strcpy(text, cached);
    return NULL;
}
const char *Music_PlayStored(void)
{
    if (Motor_Active()) return "MOTOR_BUSY";
    const char *error = load();
    if (error) return error;
    if (selected < 0) return "SCORE_NOT_SET";
    return Motor_PlayText(cached);
}
void Music_Stop(void) { Motor_Stop(); }
