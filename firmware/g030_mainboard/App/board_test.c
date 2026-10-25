#include "board_test.h"
#include "main.h"
#include "adc.h"
#include "eeprom.h"
#include "motor.h"
#include "motor_tune.h"
#include "music.h"
#include "switch_control.h"
#include <stdio.h>
#include <string.h>

static bool adc_ready;
bool Eeprom_PolicySelfTest(void);
bool Music_StorageSelfTest(void);
static unsigned input(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

void BoardTest_Init(void)
{
    adc_ready = HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK;
    Motor_Init();
}

static bool sample(uint32_t channel, uint16_t *value)
{
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if (!adc_ready || HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK) return false;
    /* Covers internal VREF/temperature startup; discard first conversion. */
    HAL_Delay(1);
    uint32_t sum = 0;
    for (unsigned i = 0; i < 17; ++i) {
        if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
        if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc1); return false;
        }
        uint32_t raw = HAL_ADC_GetValue(&hadc1);
        if (HAL_ADC_Stop(&hadc1) != HAL_OK) return false;
        if (i) sum += raw;
    }
    *value = (uint16_t)((sum + 8U) / 16U);
    return true;
}

bool BoardTest_Command(const char *cmd, char *reply, size_t cap)
{
    if (Motor_Active() && strcmp(cmd, "MOTOR STATUS") && strcmp(cmd, "STOP") &&
        strcmp(cmd, "ADC") && strcmp(cmd, "INPUTS") && strcmp(cmd, "EE STATUS") &&
        strcmp(cmd, "SW STATUS")) {
        snprintf(reply, cap, "ERR MOTOR BUSY"); return true;
    }
    if (!strcmp(cmd, "SW STATUS")) {
        SwitchControl_Report(reply, cap);
    } else if (!strcmp(cmd, "SW TEST")) {
        snprintf(reply, cap, "SW TEST=%s NO_MOTION", SwitchControl_SelfTest() ? "PASS" : "FAIL");
    } else if (!strncmp(cmd, "MUSIC SAVE ", 11)) {
        const char *error = Music_SaveScore(cmd + 11);
        snprintf(reply, cap, "%s", error ? error : "MUSIC SAVED");
    } else if (!strcmp(cmd, "MUSIC GET")) {
        char score[192];
        const char *error = Music_GetScore(score, sizeof(score));
        snprintf(reply, cap, "%s", error ? error : score);
    } else if (!strcmp(cmd, "TUNE TEST")) {
        snprintf(reply, cap, "TUNE TEST=%s NO_MOTION",
                 MotorTune_SelfTest() && Music_StorageSelfTest() ? "PASS" : "FAIL");
    } else if (!strncmp(cmd, "CHECK ", 6)) {
        MotorScore score;
        const char *error = MotorTune_Parse(cmd + 6, &score);
        if (error) snprintf(reply, cap, "ERR TUNE %s", error);
        else snprintf(reply, cap, "TUNE VALID notes=%u ms=%lu first=%u last=%u OSCILLATE NO_MOTION",
                      score.count, (unsigned long)score.total_ms, score.notes[0].hz,
                      score.notes[score.count - 1].hz);
    } else if (!strncmp(cmd, "PLAY ", 5)) {
        const char *error = Motor_PlayText(cmd + 5);
        if (error) snprintf(reply, cap, "ERR TUNE %s", error);
        else snprintf(reply, cap, "TUNE PLAY=ACCEPTED OSCILLATE");
    } else if (!strcmp(cmd, "LED ON") || !strcmp(cmd, "LED OFF")) {
        bool on = !strcmp(cmd, "LED ON");
        HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin,
                         on ? GPIO_PIN_RESET : GPIO_PIN_SET);
        HAL_Delay(1);
        snprintf(reply, cap, "LED requested=%s ODR=%u IDR=%u",
                 on ? "ON" : "OFF", (unsigned)((GPIOB->ODR >> 12) & 1U),
                 input(GPIOB, GPIO_PIN_12));
    } else if (!strcmp(cmd, "INPUTS")) {
        snprintf(reply, cap, "INPUTS UP=%u DOWN=%u NET=%u EXT=%u nFAULT=%u",
                 input(GPIOC, KEY_UP_Pin), input(GPIOC, KEY_DOWN_Pin),
                 input(GPIOC, NETCONTROL_BTN_Pin), input(GPIOB, EXT_SENSOR_DIG_Pin),
                 input(GPIOB, DRV_NFAULT_Pin));
    } else if (!strcmp(cmd, "ADC")) {
        uint16_t trq, ext, ref, temp;
        if (!sample(ADC_CHANNEL_0, &trq) || !sample(ADC_CHANNEL_11, &ext) ||
            !sample(ADC_CHANNEL_VREFINT, &ref) || !sample(ADC_CHANNEL_TEMPSENSOR, &temp) || !ref) {
            snprintf(reply, cap, "ERR ADC state=%lu error=%lu",
                     (unsigned long)HAL_ADC_GetState(&hadc1), (unsigned long)HAL_ADC_GetError(&hadc1));
        } else {
            uint32_t vdda = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(ref, ADC_RESOLUTION_12B);
            int32_t degc = __HAL_ADC_CALC_TEMPERATURE(vdda, temp, ADC_RESOLUTION_12B);
            snprintf(reply, cap, "ADC TRQ=%u/%lumV EXT=%u/%lumV VREF=%u VDDA=%lumV TEMP=%u/%ldC",
                     trq, (unsigned long)(trq * vdda / 4095U),
                     ext, (unsigned long)(ext * vdda / 4095U), ref,
                     (unsigned long)vdda, temp, (long)degc);
        }
    } else if (!strcmp(cmd, "EE SCAN")) {
        snprintf(reply, cap, "EE BANK_MASK=%02X", Eeprom_ProbeBanks());
    } else if (!strcmp(cmd, "EE STATUS")) {
        Eeprom_Report(reply, cap);
    } else if (!strcmp(cmd, "EE TEST")) {
        uint8_t original[16];
        bool pass = Eeprom_PolicySelfTest() && Eeprom_Read(0, original, sizeof(original)) &&
                    Eeprom_Update(0, original, sizeof(original)) &&
                    !Eeprom_Update(0, original, 2048) &&
                    !Eeprom_Update(15, original, 2) &&
                    !Eeprom_Update(2048, original, 1);
        snprintf(reply, cap, "EE POLICY_TEST=%s NO_PHYSICAL_WRITE", pass ? "PASS" : "FAIL");
    } else if (!strncmp(cmd, "EE R ", 5)) {
        unsigned address; char tail;
        uint8_t data[16];
        if (sscanf(cmd + 5, "%u%c", &address, &tail) != 1 || address > 2032 ||
            !Eeprom_Read((uint16_t)address, data, sizeof(data))) {
            snprintf(reply, cap, "ERR EE READ");
        } else {
            int used = snprintf(reply, cap, "EE DATA %u ", address);
            for (unsigned i = 0; i < sizeof(data); ++i)
                used += snprintf(reply + used, cap - (size_t)used, "%02X", data[i]);
        }
    } else if (!strncmp(cmd, "EE W ", 5)) {
        snprintf(reply, cap, "ERR EEPROM RAW WRITE DISABLED");
    } else if (!strcmp(cmd, "MOTOR TEST")) {
        snprintf(reply, cap, "MOTOR LOGIC_TEST=%s",
                 Motor_SelfTest() ? "PASS" : "FAIL");
    } else if (!strcmp(cmd, "MOTOR IO")) {
        snprintf(reply, cap, "MOTOR IO_TEST=%s ENABLE=0 nSLEEP=0",
                 Motor_IOTest() ? "PASS" : "FAIL");
    } else if (!strcmp(cmd, "MOTOR START")) {
        snprintf(reply, cap, "ERR USE MOTOR RUN F OR R");
    } else if (!strcmp(cmd, "MOTOR STATUS")) {
        Motor_Report(reply, cap);
    } else if (!strcmp(cmd, "MOTOR TUNE")) {
        snprintf(reply, cap, "MOTOR TUNE=%s", Motor_PlayPhrase() ? "ACCEPTED" : "REJECTED");
    } else if (!strcmp(cmd, "MOTOR RUN F") || !strcmp(cmd, "MOTOR RUN R")) {
        snprintf(reply, cap, "MOTOR RUN=%s",
                 Motor_Start(400, MOTOR_JOG_HZ, !strcmp(cmd, "MOTOR RUN R")) ? "ACCEPTED" : "REJECTED");
    } else if (!strcmp(cmd, "STOP")) {
        SwitchControl_Stop();
        snprintf(reply, cap, "STOP OK MOTOR_DISABLED");
    } else if ((((cmd[0] >= 'a' && cmd[0] <= 'g') || (cmd[0] >= 'A' && cmd[0] <= 'G')) &&
                ((cmd[1] >= '0' && cmd[1] <= '9') || cmd[1] == '#' || cmd[1] == 'b')) ||
               ((cmd[0] == 'T' || cmd[0] == 't') && cmd[1] >= '0' && cmd[1] <= '9')) {
        const char *error = Motor_PlayText(cmd);
        if (error) snprintf(reply, cap, "ERR TUNE %s", error);
        else snprintf(reply, cap, "TUNE PLAY=ACCEPTED OSCILLATE");
    } else {
        return false;
    }
    return true;
}

