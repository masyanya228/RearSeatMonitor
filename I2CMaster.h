#pragma once
#include <Wire.h>
#include "i2c_protocol.h"

// Адреса слейвов
#define MASSAGE_ADDR     10
#define VENTILATION_ADDR 20
#define HEAT_ADDR        30
#define LIGHT_ADDR       40
extern bool isDebug;

/**
 * I2CMaster — класс для Arduino Nano в роли I2C мастера
 * Поддерживает несколько слейвов на одной шине.
 *
 * Использование:
 *   I2CMaster master;
 *
 *   void setup() {
 *     master.begin();
 *   }
 *
 *   void loop() {
 *     uint8_t resp[1];
 *     master.transaction(LIGHT_ADDR, REG_PING, resp, 1);
 *     master.transaction(HEAT_ADDR,  REG_PING, resp, 1);
 *   }
 */

class I2CMaster {
public:

    // ─── Результат транзакции ─────────────────────────────────────────────────
    enum Result : uint8_t {
        OK        = 0,
        ERR_NACK,       // слейв не ответил на запись
        ERR_BUSY,       // слейв не вышел из BUSY за отведённое время
        ERR_TIMEOUT,    // нет данных в буфере
        ERR_CRC,        // контрольная сумма не совпала
    };

    // ─── Инициализация ────────────────────────────────────────────────────────
    void begin(uint32_t clock = 100000) {
        Wire.begin();
        Wire.setClock(clock);
    }

    // ─── Основная функция: полная транзакция с повторами ─────────────────────
    // addr     — адрес слейва (MASSAGE_ADDR, LIGHT_ADDR и т.д.)
    // reg      — код команды
    // params   — дополнительные байты запроса (или nullptr)
    // paramLen — длина params
    // resp     — буфер для ответа
    // respLen  — ожидаемая длина ответа (без CRC)
    Result transaction(uint8_t addr, uint8_t reg,
                       const uint8_t* params, uint8_t paramLen,
                       uint8_t* resp, uint8_t respLen) {
        Result lastErr = ERR_NACK;
        for (uint8_t attempt = 1; attempt <= I2C_RETRY_COUNT; attempt++) {
            if (attempt > 1) delay(I2C_RETRY_DELAY_MS);

            lastErr = _send(addr, reg, params, paramLen);
            if (lastErr != OK) continue;

            lastErr = _waitReady(addr);
            if (lastErr != OK) continue;

            lastErr = _readData(addr, resp, respLen);
            if (lastErr != OK) continue;

            return OK;
        }
        return lastErr;
    }

    // ─── Хелпер: транзакция без параметров ───────────────────────────────────
    Result transaction(uint8_t addr, uint8_t reg,
                       uint8_t* resp, uint8_t respLen) {
        return transaction(addr, reg, nullptr, 0, resp, respLen);
    }

    // ─── Текстовое описание ошибки (для Serial) ───────────────────────────────
    static const __FlashStringHelper* resultStr(Result r) {
        switch (r) {
            case OK:          return F("OK");
            case ERR_NACK:    return F("NACK");
            case ERR_BUSY:    return F("BUSY timeout");
            case ERR_TIMEOUT: return F("Read timeout");
            case ERR_CRC:     return F("CRC error");
            default:          return F("Unknown");
        }
    }

private:

    Result _send(uint8_t addr, uint8_t reg,
                 const uint8_t* params, uint8_t paramLen) {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        if (params && paramLen > 0)
            Wire.write(params, paramLen);
        uint8_t err = Wire.endTransmission(true);
        return (err == 0) ? OK : ERR_NACK;
    }

    bool _readByte(uint8_t addr, uint8_t& out) {
        Wire.requestFrom(addr, (uint8_t)1);
        uint32_t t = millis();
        while (!Wire.available()) {
            if (millis() - t > I2C_READ_TIMEOUT_MS) return false;
            delayMicroseconds(200);
        }
        out = (uint8_t)Wire.read();
        return true;
    }

    Result _waitReady(uint8_t addr) {
        uint32_t deadline = millis() + I2C_READY_TIMEOUT_MS;
        while (millis() < deadline) {
            uint8_t status;
            if (!_readByte(addr, status)) return ERR_TIMEOUT;
            if (status == STATUS_OK)      return OK;
            if (status == STATUS_ERROR)   return ERR_NACK;
            delay(I2C_POLL_INTERVAL_MS);
        }
        return ERR_BUSY;
    }

    Result _readData(uint8_t addr, uint8_t* buf, uint8_t dataLen) {
        uint8_t total = dataLen + 1;
        Wire.requestFrom(addr, total);

        uint32_t deadline = millis() + I2C_READ_TIMEOUT_MS;
        while (Wire.available() < total) {
            if (millis() > deadline) return ERR_TIMEOUT;
            delayMicroseconds(200);
        }

        for (uint8_t i = 0; i < dataLen; i++) buf[i] = Wire.read();
        uint8_t rxCRC   = Wire.read();
        uint8_t calcCRC = crc8(buf, dataLen);

        return (rxCRC == calcCRC) ? OK : ERR_CRC;
    }
};