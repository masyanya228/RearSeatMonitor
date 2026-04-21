#pragma once
#include <stdint.h>

#define REG_L_MODE 0x01
#define REG_L_GetStatus 0x02
#define REG_R_MODE 0x03
#define REG_R_GetStatus 0x04
#define REG_Power 0x10
#define REG_SetRGB 0x11
#define REG_SetBR 0x12
#define REG_GetLightStatus 0x13
#define REG_GetErrorCount 0x05
#define REG_GetNextError 0x06
#define REG_ClearErrors 0x07
#define REG_PING 0x08

// Статусные байты (первый байт любого ответа)
#define STATUS_BUSY          0xFF  // слейв ещё не готов
#define STATUS_OK            0x01  // данные готовы
#define STATUS_ERROR         0x00  // ошибка выполнения

// Таймауты
#define I2C_POLL_INTERVAL_MS   5   // пауза между опросами статуса
#define I2C_READY_TIMEOUT_MS 500   // макс. ожидание готовности слейва
#define I2C_READ_TIMEOUT_MS   50   // макс. ожидание одного байта
#define I2C_RETRY_COUNT        3   // попыток при сбое CRC или таймауте
#define I2C_RETRY_DELAY_MS    20

// CRC8 (Dallas/Maxim, полином 0x07)
inline uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}
