#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Demand-driven only: never call reads or updates from a periodic task. */
bool Eeprom_Read(uint16_t address, uint8_t *data, uint16_t length);
/* <=16 bytes within ONE page, compare-before-write, 60s cooldown, 8 attempts/boot.
 * No erase/full-array write API. Not exposed through the diagnostic UART. */
bool Eeprom_Update(uint16_t address, const uint8_t *data, uint16_t length);
/* Direction / first-endpoint event only: one verified 16-byte journal page at
 * 0x200 or 0x210. Shares 8 attempts/boot; permits these distinct startup events
 * within 60 s. Callers suppress unchanged state and never retry periodically. */
bool Eeprom_UpdateLiftRecord(uint16_t address, const uint8_t record[16]);
/* Reserved score slots at 0x000/0x100, 208 bytes each. Main context only.
 * One save consumes one of the shared 8 attempts/boot, with 60s cooldown.
 * At most 14 page writes, compare-before-write; commit header last. */
bool Eeprom_WriteScoreSlot(unsigned slot, const uint8_t record[208]);
uint8_t Eeprom_ProbeBanks(void);
void Eeprom_Report(char *buffer, unsigned capacity);
