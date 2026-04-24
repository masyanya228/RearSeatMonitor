#include <EEPROM.h>

void InitEEPROM() {
  sizeErr = sizeof(errors[0]);
  errLen = sizeof(errors)/sizeErr;
  LoadErrors();                     // загружаем текущее состояние
  if (false) {            // первый запуск
    FirstInit();
  }
}

void FirstInit(){
  if (isDebug) Serial.println("EEPROM");
  int i = 0;
  while (true) {
    uint8_t code = pgm_read_byte(&errorDescriptions[i].code);
    if (code == 0) break;

    errors[i].code  = code;
    errors[i].tfs   = 0;
    errors[i].times = 0;
    errors[i].addr  = 0;
    i++;
  }
  EEPROM.put(0, errors);        // сохраняем только разрешённые коды
  if (isDebug) Serial.print("Initialized "); Serial.print(errLen); Serial.println(" error slots");
}

String GetErrorDescription(uint8_t code) {
    for (uint8_t i = 0; ; i++) {
        uint8_t c = pgm_read_byte(&errorDescriptions[i].code);
        if (c == 0) break;
        if (c == code) {
            const char* ptr = (const char*)pgm_read_ptr(&errorDescriptions[i].description);
            return String((__FlashStringHelper*)ptr);
        }
    }
    return String(F("Unknown"));
}

void LoadErrors() {
  EEPROM.get(0, errors);
}

// =============================================
// Получение списка ошибок с пагинацией
// page  — номер страницы (начинается с 0)
// limit — количество ошибок на одной странице
// Возвращает количество фактически возвращённых ошибок
// =============================================
int GetErrors(int page, int limit, Error* resultArray) 
{
    if (page < 0 || limit <= 0) {
        return 0;
    }

    int totalErrors = errLen;
    int startIndex = page * limit;

    if (startIndex >= totalErrors) {
        return 0;
    }

    int count = 0;
    for (int i = startIndex; i < totalErrors && count < limit; i++) {
        // Копируем ошибку в результирующий массив
        resultArray[count] = errors[i];
        count++;
    }

    return count;
}

int IndexOfError(uint8_t code) {
  for (int i = 0; i < errLen; i++) {
    if (errors[i].code == code) {
      return i;
    }
  }
  return -1;
}

// Проверка, разрешён ли код ошибки
bool IsErrorCodeAllowed(uint8_t code) {
    for (int i = 0; ; i++) {
        uint8_t c = pgm_read_word(&errorDescriptions[i].code);
        if (c == 0) break;
        if (c == code) return true;
    }
    return false;
}

void SaveError(uint8_t code) {
  SaveError(code, 0, 0, 0);
}

void SaveError(uint8_t code, uint32_t tfs, uint8_t addr, uint8_t times) {
  if (!IsErrorCodeAllowed(code)) {
    if (isDebug) { Serial.print("Error code "); Serial.print(code); Serial.println(" not allowed"); }
    return;
  }

  int i = IndexOfError(code);
  if (i == -1) return;

  if(times==0)
    errors[i].times++;
  else
    errors[i].times=times;
  errors[i].addr = addr;

  if (errors[i].tfs == 0) {
    errors[i].tfs = (tfs == 0) ? millis() : tfs;
  }
  EEPROM.put(sizeErr * i, errors[i]);
}

void ClearAllErrors() {
  for (int i = 0; i < errLen; i++) {
    errors[i].tfs   = 0;
    errors[i].times = 0;
    errors[i].addr  = 0;
  }
  EEPROM.put(0, errors);
  if (isDebug) Serial.println(F("All errors cleared (times and tfs reset)"));
}

void ResetError(uint8_t code){
  int i = IndexOfError(code);
  if (i == -1) return;
  errors[i].tfs=0;
  errors[i].times=0;
  errors[i].addr=0;
  EEPROM.put(sizeErr*i, errors[i]);
  logI("reseted", code);
}