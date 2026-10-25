/* Real EEPROM module, RAM-only I2C backend. Never connects to hardware. */
#include "i2c.h"
#include "eeprom.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
static uint8_t memory[2048];
static uint32_t now, calls;
static bool fail_write, tear_write;
uint32_t HAL_GetTick(void) { return now; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t at,
                                 uint16_t mode, uint8_t *out, uint16_t n, uint32_t timeout)
{
    (void)h; (void)mode; (void)timeout;
    at += ((dev >> 1) & 7U) * 256U;
    assert(at + n <= sizeof(memory));
    memcpy(out, memory + at, n); return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t at,
                                  uint16_t mode, uint8_t *in, uint16_t n, uint32_t timeout)
{
    (void)h; (void)mode; (void)timeout;
    at += ((dev >> 1) & 7U) * 256U;
    assert(at + n <= sizeof(memory) && n <= 16U - (at & 15U));
    ++calls;
    if (fail_write) return HAL_ERROR;
    memcpy(memory + at, in, tear_write ? n / 2U : n);
    return tear_write ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t dev, uint32_t trials, uint32_t timeout)
{ (void)h; (void)dev; (void)trials; (void)timeout; return HAL_OK; }

int main(int argc, char **argv)
{
    memset(memory, 0xff, sizeof(memory));
    uint8_t record[16] = {'L', 'W', 6U, 2U};
    if (argc > 1 && !strcmp(argv[1], "generic")) {
        assert(Eeprom_Update(0x200U, record, 16U) && calls == 1U);
        assert(Eeprom_Update(0x200U, record, 16U) && calls == 1U);
        ++record[3]; now = 59999U;
        assert(!Eeprom_Update(0x200U, record, 16U) && calls == 1U);
        now = 60000U; assert(Eeprom_Update(0x200U, record, 16U) && calls == 2U);
        assert(!Eeprom_Update(0x20fU, record, 2U));
        assert(!Eeprom_UpdateLiftRecord(0x100U, record));
        assert(calls == 2U);
        puts("PASS real EEPROM: generic 60 s cooldown, same-value skip, cross-page and music-slot rejection unchanged");
        return 0;
    }
    assert(Eeprom_UpdateLiftRecord(0x200U, record));
    uint8_t first[16]; memcpy(first, memory + 0x200U, 16U);
    ++record[3]; now = 10U;
    if (argc > 1 && !strcmp(argv[1], "torn")) {
        tear_write = true;
        assert(!Eeprom_UpdateLiftRecord(0x210U, record));
        assert(!memcmp(memory + 0x200U, first, 16U) && calls == 2U);
        char report[256]; Eeprom_Report(report, sizeof(report));
        assert(strstr(report, "attempts=2 writes=1"));
        puts("PASS real EEPROM: torn new page reports failure and preserves previous journal slot");
        return 0;
    }
    assert(Eeprom_UpdateLiftRecord(0x210U, record) && calls == 2U);
    ++record[3]; now = 20U;
    assert(Eeprom_UpdateLiftRecord(0x200U, record) && calls == 3U);
    assert(Eeprom_UpdateLiftRecord(0x200U, record) && calls == 3U);
    ++record[3]; fail_write = true;
    assert(!Eeprom_UpdateLiftRecord(0x210U, record) && calls == 4U);
    fail_write = false;
    for (unsigned i = 4U; i < 8U; ++i) {
        ++record[3]; assert(Eeprom_UpdateLiftRecord(0x200U, record));
    }
    assert(calls == 8U);
    assert(Eeprom_UpdateLiftRecord(0x200U, record) && calls == 8U);
    ++record[3]; assert(!Eeprom_UpdateLiftRecord(0x210U, record) && calls == 8U);
    for (unsigned i = 0; i < 0x200U; ++i) assert(memory[i] == 0xffU);
    char report[256]; Eeprom_Report(report, sizeof(report));
    assert(strstr(report, "attempts=8 writes=7"));
    puts("PASS real EEPROM: direction/top/bottom events within 60 s, verified pages, shared 8-attempt cap including failures, music untouched");
    return 0;
}
