#include <EEPROM.h>

void InitEEPROM(){
  EEPROM.get(0, errors);
  if(errors[0].code==0){
    if(isDebug) Serial.println("EEPROM Init");
    errors[0].code=1;
    errors[1].code=2;
    errors[2].code=3;
    errors[3].code=4;
    errors[4].code=5;
    errors[5].code=6;
    errors[6].code=7;
    errors[7].code=8;
    errors[8].code=9;
    errors[9].code=10;
    errors[10].code=11;
    errors[11].code=12;
    errors[12].code=13;
    errors[13].code=14;
    errors[14].code=15;
    EEPROM.put(0, errors); 
  }
}

int IndexOfError(uint16_t code){
  int len=sizeof(errors) / sizeErr;
  for(int i=0; i<len; i++)
  {
    if(errors[i].code==code){
      return i;
    }
  }
  return -1;
}

void SaveError(uint16_t code){
  logI("encountered", code);
  int i = IndexOfError(code);
  errors[i].times++;
  EEPROM.update(sizeErr*i+2+4, errors[i].times);
  if(errors[i].tfs==0){
    errors[i].tfs=millis();
    EEPROM.update(sizeErr*i+2, errors[i].tfs);
  }
}

void SaveRemoteError(uint16_t code, uint32_t tfs, uint8_t times, uint8_t addr){
  int i = IndexOfError(code);
  errors[i].times=times;
  EEPROM.update(sizeErr*i+2, errors[i].tfs);
  EEPROM.update(sizeErr*i+2+4, errors[i].times);
  EEPROM.update(sizeErr*i+2+4+1, errors[i].addr);
  if(errors[i].tfsr==0){
    errors[i].tfsr=millis();
    EEPROM.update(sizeErr*i+2+4+1+1, errors[i].tfsr);
  }
}

void ResetError(uint16_t code){
  int i = IndexOfError(code);
  errors[i].tfs=0;
  errors[i].times=0;
  errors[i].tfsr=0;
  errors[i].addr=0;
  EEPROM.put(sizeErr*i, errors[i]);
  logI("reseted", code);
}

void LogError(uint16_t code){
  if(!isDebug) return;
  
  int i = IndexOfError(code);

  String friendlyName="";
  for(int j=0; j<3; j++){
    if(mods[j].addr==errors[i].addr)
    {
      friendlyName=mods[j].friendlyName;
      break;
    }
  }
  logS("==================================");
  logI("Module I2C addr", errors[i].addr);
  Serial.print("Module friendly name"); Serial.println(friendlyName);
  logI("Error code", errors[i].code);
  logI("Sub module: time from start(ms)", errors[i].tfs);
  logI("Main module: time from start(ms)", errors[i].tfsr);
  logI("Times", errors[i].times);
  logS("==================================");
}
