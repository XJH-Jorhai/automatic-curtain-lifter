/* Exercise the production EEPROM module against a RAM I2C backend.
 * No HAL I2C transaction from this test can reach the physical device. */
#include "i2c.h"
#include "eeprom.h"
#include "motor.h"
#include "music.h"
#include <string.h>
static uint8_t memory[512];
static uint32_t fake_tick, physical_calls;
static bool fail_write;
static uint32_t fail_at;
static HAL_StatusTypeDef fake_read(I2C_HandleTypeDef *h, uint16_t d, uint16_t a,
                                  uint16_t mode, uint8_t *p, uint16_t n, uint32_t timeout)
{
    (void)h; (void)mode; (void)timeout;
    a += ((d >> 1) & 7U) * 256U;
    if (a + n > sizeof(memory)) return HAL_ERROR;
    memcpy(p, memory + a, n); return HAL_OK;
}
static HAL_StatusTypeDef fake_write(I2C_HandleTypeDef *h, uint16_t d, uint16_t a,
                                   uint16_t mode, uint8_t *p, uint16_t n, uint32_t timeout)
{
    (void)h; (void)mode; (void)timeout;
    a += ((d >> 1) & 7U) * 256U;
    ++physical_calls;
    if (fail_write || (fail_at && physical_calls == fail_at) ||
        a + n > sizeof(memory)) return HAL_ERROR;
    memcpy(memory + a, p, n); return HAL_OK;
}
static HAL_StatusTypeDef fake_ready(I2C_HandleTypeDef *h, uint16_t d, uint32_t trials, uint32_t timeout)
{ (void)h; (void)d; (void)trials; (void)timeout; return HAL_OK; }
static uint32_t fake_time(void) { return fake_tick; }

#define Eeprom_Read Test_Read
#define Eeprom_Update Test_Update
#define Eeprom_UpdateLiftRecord Test_UpdateLiftRecord
#define Eeprom_ProbeBanks Test_ProbeBanks
#define Eeprom_Report Test_Report
#define Eeprom_WriteScoreSlot Test_WriteScoreSlot
#define HAL_I2C_Mem_Read fake_read
#define HAL_I2C_Mem_Write fake_write
#define HAL_I2C_IsDeviceReady fake_ready
#define HAL_GetTick fake_time
#include "eeprom.c"

bool Eeprom_PolicySelfTest(void)
{
    uint8_t data[16] = {0};
    memset(memory, 0, sizeof(memory));
    attempts = writes = skipped = rejected = last_write = physical_calls = 0;
    fake_tick = 0xfffffff0U; fail_write = false;
    if (!Test_Update(0, data, 16) || physical_calls || skipped != 1) return false;
    if (Test_Update(0, data, 2048) || Test_Update(15, data, 2) ||
        Test_Update(2048, data, 1) || physical_calls) return false;
    data[3] = 1;
    if (!Test_Update(0, data, 16) || physical_calls != 1 || writes != 1) return false;
    data[3] = 2;
    fake_tick += 59999U;
    if (Test_Update(0, data, 16) || physical_calls != 1) return false;
    ++fake_tick; /* cooldown across tick wrap */
    if (!Test_Update(0, data, 16) || physical_calls != 2) return false;
    fake_tick += 60000U; fail_write = true; data[3] = 3;
    if (Test_Update(0, data, 16) || attempts != 3 || physical_calls != 3) return false;
    fail_write = false;
    if (Test_Update(0, data, 16) || physical_calls != 3) return false;
    for (unsigned i = 3; i < 8; ++i) {
        fake_tick += 60000U; data[3] = (uint8_t)(i + 1);
        if (!Test_Update(0, data, 16)) return false;
    }
    fake_tick += 60000U; data[3] = 99;
    return !Test_Update(0, data, 16) && physical_calls == 8 && attempts == 8;
}

static bool fake_active(void) { return false; }
static unsigned play_calls;
static const char *fake_play(const char *text)
{
    (void)text; ++play_calls; return NULL;
}
static void fake_stop(void) {}
#define Motor_Active fake_active
#define Motor_PlayText fake_play
#define Motor_Stop fake_stop
#define Music_SaveScore Test_SaveScore
#define Music_GetScore Test_GetScore
#define Music_PlayStored Test_PlayStored
#define Music_Stop Test_Stop
#include "music.c"

/* Production record and page code, RAM-only bus and motor substitutes. */
bool Music_StorageSelfTest(void)
{
    char out[192];
    memset(memory, 0xff, sizeof(memory));
    attempts = writes = skipped = rejected = last_write = physical_calls = 0;
    reads = 0; fake_tick = 0; fail_write = false; fail_at = 0;
    loaded = false; play_calls = 0;
    if (!Test_PlayStored() || play_calls) return false; /* blank */
    if (Test_SaveScore("c4f4g4")) return false;
    uint32_t before_reads = reads, before_writes = physical_calls;
    if (Test_SaveScore("c4f4g4") || reads != before_reads ||
        physical_calls != before_writes) return false;
    if (!Test_SaveScore("invalid") || physical_calls != before_writes) return false;
    if (Test_PlayStored() || play_calls != 1 || reads != before_reads) return false;
    loaded = false; /* reboot cache */
    if (Test_GetScore(out, sizeof(out)) || strcmp(out, "c4f4g4")) return false;
    /* Abort each page-write position of a second-slot save. The committed
     * first slot must remain usable. An unchanged page may be skipped. */
    for (unsigned cut = 1; cut <= 15; ++cut) {
        memset(memory + 256, 0xff, 256);
        attempts = 0; fake_tick += 60000U; loaded = false;
        fail_at = physical_calls + cut;
        const char *error = Test_SaveScore("a4b4c5");
        fail_at = 0; loaded = false;
        if (Test_GetScore(out, sizeof(out))) return false;
        if (strcmp(out, error ? "c4f4g4" : "a4b4c5")) return false;
    }
    /* Corrupt latest payload: fall back to the older valid record. */
    memory[256 + 16] ^= 1U; loaded = false;
    if (Test_GetScore(out, sizeof(out)) || strcmp(out, "c4f4g4")) return false;
    return play_calls == 1;
}
