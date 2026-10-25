#include "eeprom.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

static uint32_t reads, read_errors, probes;
static uint32_t attempts, writes, skipped, rejected, last_write;

/* AT24C16C: 2048 bytes, 16-byte pages, block bits in the I2C address.
 * HAL expects the 7-bit device address shifted left once. */
static uint16_t device(uint16_t address)
{
    return (uint16_t)((0x50U | (address >> 8)) << 1);
}

uint8_t Eeprom_ProbeBanks(void)
{
    uint8_t mask = 0;
    for (unsigned bank = 0; bank < 8; ++bank) {
        ++probes;
        if (HAL_I2C_IsDeviceReady(&hi2c1, (0x50U + bank) << 1, 1, 10) == HAL_OK)
            mask |= (uint8_t)(1U << bank);
    }
    return mask;
}

bool Eeprom_Read(uint16_t address, uint8_t *data, uint16_t length)
{
    if (!data || !length || address >= 2048 || length > 2048U - address)
        return false;
    while (length) {
        uint16_t chunk = 256U - (address & 255U);
        if (chunk > length) chunk = length;
        ++reads;
        if (HAL_I2C_Mem_Read(&hi2c1, device(address), address & 255U,
                            I2C_MEMADD_SIZE_8BIT, data, chunk, 50) != HAL_OK) {
            ++read_errors; return false;
        }
        address += chunk; data += chunk; length -= chunk;
    }
    return true;
}

static bool reserve_write(void)
{
    uint32_t now = HAL_GetTick();
    if (attempts >= 8 || (attempts && (uint32_t)(now - last_write) < 60000U)) {
        ++rejected; return false;
    }
    ++attempts; last_write = now;
    return true;
}

static bool update_page(uint16_t address, const uint8_t *data, uint16_t length,
                        unsigned charge)
{
    uint8_t old[16], verify[16];
    if (!data || !length || length > 16 || address >= 2048 ||
        length > 16U - (address & 15U)) {
        ++rejected;
        return false;
    }
    if (!Eeprom_Read(address, old, length)) return false;
    if (!memcmp(old, data, length)) { ++skipped; return true; }
    /* Failed/uncertain bus writes consume the budget too; no automatic retry. */
    if (charge == 2U) {
        if (attempts >= 8U) { ++rejected; return false; }
        ++attempts; last_write = HAL_GetTick();
    } else if (charge && !reserve_write()) return false;
    unsigned first = 0, end = length;
    while (old[first] == data[first]) ++first;
    while (old[end - 1] == data[end - 1]) --end;
    uint16_t dev = device(address);
    if (HAL_I2C_Mem_Write(&hi2c1, dev, (address + first) & 255U,
                         I2C_MEMADD_SIZE_8BIT, (uint8_t *)data + first, end - first, 50) != HAL_OK)
        return false;
    uint32_t start = HAL_GetTick();
    while (HAL_I2C_IsDeviceReady(&hi2c1, dev, 1, 2) != HAL_OK)
        if ((uint32_t)(HAL_GetTick() - start) >= 10U) return false;
    if (!Eeprom_Read(address, verify, length) || memcmp(verify, data, length)) return false;
    ++writes;
    return true;
}

bool Eeprom_Update(uint16_t address, const uint8_t *data, uint16_t length)
{
    return update_page(address, data, length, true);
}

bool Eeprom_UpdateLiftRecord(uint16_t address, const uint8_t record[16])
{
    if (address != 0x200U && address != 0x210U) { ++rejected; return false; }
    return update_page(address, record, 16U, 2U);
}

/* One bounded logical save, never a whole-chip operation. Header is committed
 * last. The other slot must remain untouched until this one verifies. */
bool Eeprom_WriteScoreSlot(unsigned slot, const uint8_t record[208])
{
    if (slot > 1 || !record) return false;
    uint16_t base = (uint16_t)(slot * 256U);
    uint8_t invalid[16] = {0};
    if (!reserve_write()) return false;
    if (!update_page(base, invalid, 16, false)) return false;
    for (unsigned offset = 16; offset < 208; offset += 16)
        if (!update_page(base + offset, record + offset, 16, false)) return false;
    return update_page(base, record, 16, false);
}

/* This reports RAM counters only. It never accesses the I2C bus. */
void Eeprom_Report(char *buffer, unsigned capacity)
{
    snprintf(buffer, capacity,
             "EE reads=%lu probes=%lu errors=%lu attempts=%lu writes=%lu skip=%lu reject=%lu",
             (unsigned long)reads, (unsigned long)probes, (unsigned long)read_errors,
             (unsigned long)attempts, (unsigned long)writes, (unsigned long)skipped,
             (unsigned long)rejected);
}
