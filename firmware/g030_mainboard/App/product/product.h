#pragma once
#include "motion.h"

typedef enum { PRODUCT_NONE, PRODUCT_LOCAL, PRODUCT_REMOTE } ProductOwner;
typedef enum {
    PRODUCT_CAL_NONE, PRODUCT_CAL_UPPER, PRODUCT_CAL_LOWER,
    PRODUCT_CAL_INVALID, PRODUCT_CAL_UNAVAILABLE, PRODUCT_CAL_STORAGE_ERROR
} ProductCalibration;
enum { PRODUCT_TRACE_EMPTY, PRODUCT_TRACE_RUNNING, PRODUCT_TRACE_STALL,
       PRODUCT_TRACE_FREE, PRODUCT_TRACE_CANCELED, PRODUCT_TRACE_FAULT, PRODUCT_TRACE_LIMIT };
#define PRODUCT_PROBE_TRACE_SAMPLES 160U
/* Read-only diagnostics in RAM. Each packed sample is running_ms << 12 | ADC.
 * No periodic EEPROM writes or serial streaming during motor operation. */
typedef struct {
    uint32_t generation, count;
    uint16_t used, total, minimum, maximum, last, next;
    uint8_t result, error;
    uint32_t samples[PRODUCT_PROBE_TRACE_SAMPLES];
} ProductProbeTrace;
/* Confirmed Product/capture: slots 0/1 retain last automatic UP/DOWN stop.
 * Legacy unknown-direction profile: slots 0/1 retain probes.
 * Slot 2 always holds the latest working segment ring. */
const ProductProbeTrace *Product_ProbeTrace(unsigned index);
void Product_Init(void);
void Product_PlayStartup(void);
void Product_Poll(void);
void Product_TickISR(void);
bool Product_Request(uint8_t percent, ProductOwner owner);
/* Stops first, commits with motor disabled, returns verified persistence result. */
ProductCalibration Product_Calibrate(bool up);
void Product_Stop(LiftError error);
void Product_RemoteLostISR(void);
bool Product_PairingRequested(void);
bool Product_Busy(void);
bool Product_RemoteActive(void);
uint8_t Product_Percent(void);
uint8_t Product_Target(void);
bool Product_Opening(void);
void Product_Report(char *buffer, unsigned capacity);

