bool isDebug=true;
#include <SoftwareSerial.h>
#include <Wire.h>
#include "I2CMaster.h"
I2CMaster master;


#define MasType 0 //seat massage
#define VentType 1 //seat ventilation
#define HeatType 2 //seat heater
#define LightType 3 //ambient light

// UART команды
#define CMD_TOUCH 101 // OnPress onRelease
#define CMD_GET_PIC 113 //Ответ на запрос картиинки
#define CMD_LED_POWER 1 // Вкл/выкл [1, элемент LE, 0/1 LE] len=9
#define CMD_LED_PALITRA 2 // Цвет с палитры [2, элемент LE, x LE, y LE] len=13
#define CMD_LED_COLOR 3 // Цвет с пресета [3, элемент LE, RGB565 LE] len=9
#define CMD_LED_BRIGHT 4 // Яркость [4, элемент LE, Br(0–100) LE] len=9
#define CMD_LED_GETSTATE 5 // [5] len=1
#define CMD_GET_ERRORS 6 // [6][page 0..n LE] len=5
#define CMD_CLEAR_ERRORS 7 // [7] len=1

#define LED_SKY 1 //Звёзды
#define LED_LINES 2 //Линии
#define LED_FLOOR 3 //Стаканы
#define LED_SKY_LINES 4 //Звёзды и линии
#define LED_LINES_FLOOR 5 //Линии и стаканы
#define LED_ALL 6 //Всё

struct LedElement {
  bool    on;
  uint8_t r, g, b;
  uint8_t br;  // 0–100
};

struct Color{
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

LedElement leds[3] = {
  { false, 255, 255, 255, 50 },  // [0] Звёзды
  { false, 255, 255, 255, 50 },  // [1] Линии
  { false, 255, 255, 255, 50 },  // [2] Стаканы
};

// Модули на шине I2C
struct Module{
  byte type=0;
  int addr=0;
  String friendlyName="";
  bool isOnline=false;
};

//i2c модули
Module mods[4];
int ModuleLen=4;

// serial port для связи с дисплеем
SoftwareSerial mySerial(8, 9);
int num=0;
int disPacketPointer=0;
int disPacket[20];
uint32_t lastMessage=0;

int QueueOfRequestsLen=0;
int QueueOfRequests[10];

//Кнопки сидений на дисплее
struct Button{
  byte id=0;
  String objName="";
  String friendlyName="";
  byte mode=0;
  byte modePic[4];
  byte type;
  byte seat;
};
Button btns[6];

//Ошибки в памяти
struct Error{
  uint8_t code=0;
  uint32_t tfs=0;
  uint8_t times=0;
  uint8_t addr=0;
};
Error errors[15];
int sizeErr;
int errLen;
int nextError=0;

const char e10[] PROGMEM = "Massage didnt response";
const char e20[] PROGMEM = "Vent didnt response";
const char e30[] PROGMEM = "Heat didnt response";
const char e40[] PROGMEM = "BLE commander didnt response";
const char e91[] PROGMEM = "BLE commander wrong response on HR";
const char e41[] PROGMEM = "Ambient not scannable";
const char e42[] PROGMEM = "StarSky not scannable";
const char e43[] PROGMEM = "Ambient has no target service";
const char e44[] PROGMEM = "StarSky has no target service";
const char e45[] PROGMEM = "Ambient has no target characteristic";
const char e46[] PROGMEM = "StarSky has no target characteristic";
const char e47[] PROGMEM = "Not supported i2c command";
const char e48[] PROGMEM = "Ambient has no services";
const char e49[] PROGMEM = "StarSky has no services";
const char e50[] PROGMEM = "Ambient has no characteristics";
const char e51[] PROGMEM = "StarSky has no characteristics";
const char e52[] PROGMEM = "Ambient still not ready to command";
const char e53[] PROGMEM = "StarSky still not ready to command";
const char e54[] PROGMEM = "Ambient bad response";
const char e55[] PROGMEM = "StarSky bad response";

struct ErrorDesc {
    uint8_t code;
    const char* description;
};

const ErrorDesc errorDescriptions[] PROGMEM = {
    {10, e10}, {20, e20}, {30, e30}, {40, e40},
    {91, e91}, {41, e41}, {42, e42}, {43, e43},
    {44, e44}, {45, e45}, {46, e46}, {47, e47},
    {48, e48}, {49, e49}, {50, e50}, {51, e51},
    {52, e52}, {53, e53}, {54, e54}, {55, e55},
    {0, nullptr}
};

void setup() {
  delay(2000);
  Serial.begin(115200);

  InitEEPROM();
  SetupBtns();
  SetupMods();
  
  mySerial.begin(9600);
  mySerial.setTimeout(50);
  
  master.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("setup");
}

int endcombyte=0;
void loop() {
  if(mySerial.available()){ 
    byte inc = mySerial.read();
    Serial.println(inc);
    disPacket[disPacketPointer]=inc;
    disPacketPointer++;
    
    if (inc == 0xff) {
      endcombyte++;
    }
    else{
      endcombyte=0;
    }
    if(endcombyte==3){
      while(disPacketPointer>0 && endcombyte>0)
      {
        disPacketPointer--;
        endcombyte--;
        disPacket[disPacketPointer]=0;
      }
      endcombyte=0;
      ToDo(disPacket, disPacketPointer);
      disPacketPointer=0;
    }
  }
  if(millis()-lastMessage>=10000){
    lastMessage=millis();
    HealthReport();
  }
}

//I2C commands
void HealthReport() {
  logS("HealthReport");
  for(int m=0; m<ModuleLen; m++)
  {
    bool ping=I2C_Ping(mods[m].addr);
    if(!ping)
      continue;
    uint8_t count = I2C_GetErrorCount(mods[m].addr);
    if (count == 0) { Serial.println("[HEALTH] Ошибок нет"); return; }
    Serial.print("[HEALTH] Ошибок: ");
    Serial.println(count);
    for (uint8_t i = 0; i < count; i++) {
        Error rec;
        if (I2C_GetError(mods[m].addr, i, rec)) {
            Serial.print("  code=0x");
            Serial.print(rec.code, HEX);
            Serial.print(" times=");
            Serial.println(rec.times);
        }
    }
  }
}

bool I2C_Ping(uint8_t slaveAddr) {
    uint8_t resp[1];
    auto res = master.transaction(slaveAddr, REG_PING, resp, 1);
    if (res != I2CMaster::OK || resp[0] != 0x01) {
        Serial.print(F("[I2C] Ping FAIL: "));
        Serial.println(I2CMaster::resultStr(res));
        return false;
    }
    Serial.println(F("[I2C] Ping OK"));
    return true;
}

uint8_t I2C_GetErrorCount(uint8_t slaveAddr) {
  Serial.println("I2C_GetErrorCount");
  uint8_t resp[2] = {0, 0};
  auto res = master.transaction(slaveAddr, REG_GetErrorCount, resp, sizeof(resp));
  if (res != I2CMaster::OK) {
      Serial.print(F("[I2C] GetErrorCount FAIL: "));
      Serial.println(I2CMaster::resultStr(res));
      return 0;
  }
  return resp[1];
}

bool I2C_GetError(uint8_t slaveAddr, uint8_t index, Error& out) {
    uint8_t resp[7];
    auto res = master.transaction(slaveAddr, REG_GetNextError, &index, 1, resp, 7);
    if (res != I2CMaster::OK || resp[0] == 0) return false;
    out.code  = resp[1];
    memcpy(&out.tfs, &resp[2], 4);
    out.times = resp[6];
    return true;
}

bool I2C_ClearErrors(uint8_t slaveAddr) {
    uint8_t resp[1];
    auto res = master.transaction(slaveAddr, REG_ClearErrors, resp, 1);
    return res == I2CMaster::OK && resp[0] == 0x01;
}

byte NextMode(uint8_t slaveAddr, uint8_t seat) {
    uint8_t reg = (seat == 0) ? REG_L_MODE : REG_R_MODE;
    uint8_t resp[1];
    auto res = master.transaction(slaveAddr, reg, resp, 1);
    if (res != I2CMaster::OK) return 0xFF;  // ошибка
    return resp[0];
}

void ApplyLedPower(uint8_t el, bool power) {
    uint8_t params[] = {el, power ? 1u : 0u};
    uint8_t resp[1];
    master.transaction(LIGHT_ADDR, REG_Power, params, sizeof(params), resp, 1);
}

void ApplyLedColor(uint8_t el, Color rgb) {
    uint8_t params[] = {el, rgb.r, rgb.g, rgb.b};
    uint8_t resp[1];
    master.transaction(LIGHT_ADDR, REG_SetRGB, params, sizeof(params), resp, 1);
}

void ApplyLedBright(uint8_t el, uint8_t br) {
    uint8_t params[] = {el, br};
    uint8_t resp[1];
    master.transaction(LIGHT_ADDR, REG_SetBR, params, sizeof(params), resp, 1);
}

bool GetLedStatus() {
    uint8_t resp[16];
    auto res = master.transaction(LIGHT_ADDR, REG_GetLightStatus, resp, sizeof(resp));
    if (res != I2CMaster::OK) return false;
    leds[0].on=resp[1];
    leds[0].r=resp[2];
    leds[0].g=resp[3];
    leds[0].b=resp[4];
    leds[0].br=resp[5];
    leds[1].on=resp[6];
    leds[1].r=resp[7];
    leds[1].g=resp[8];
    leds[1].b=resp[9];
    leds[1].br=resp[10];
    leds[2].on=resp[11];
    leds[2].r=resp[12];
    leds[2].g=resp[13];
    leds[2].b=resp[14];
    leds[2].br=resp[15];
    return true;
}

uint8_t GetStatus(uint8_t slaveAddr, uint8_t seat) {
    uint8_t reg = (seat == 0) ? REG_L_GetStatus : REG_R_GetStatus;
    uint8_t resp[1];
    auto res = master.transaction(slaveAddr, reg, resp, 1);
    if (res != I2CMaster::OK) return -1;
    return resp[0];
}

void ScanModules(){
  return;
  for (int i=0; i<ModuleLen; i++) {
    Wire.beginTransmission(mods[i].addr);
    if (Wire.endTransmission()) {
      mods[i].isOnline=false;
      SaveError(mods[i].addr+0);
    } else {
      mods[i].isOnline=true;
    }
  }
}

//UART comands
void ToDo(int data[], int len){
  if(len==0)
    return;
  Serial.print("Длинна сообщения: ");
  Serial.print(endcombyte);
  Serial.print(" ");
  Serial.println(len);
  switch (data[0]) {
    case CMD_TOUCH:
      if (len == 4) {
        if (data[3] == 1) TouchPressEvent(data[1], data[2]);
        else if (data[3] == 0) TouchReleaseEvent(data[1], data[2]);
      }
      break;

    case CMD_LED_POWER: HandleLedPower(data, len);      break;
    case CMD_LED_PALITRA: HandleLedPalitra(data, len);      break;
    case CMD_LED_COLOR: HandleLedColor(data, len);      break;
    case CMD_LED_BRIGHT: HandleLedBright(data, len); break;
    case CMD_LED_GETSTATE: HandleLedGetState(data, len);   break;
    case CMD_GET_ERRORS: HandleGetErrors(data, len); break;
    case CMD_CLEAR_ERRORS: HandleClearErrors(data, len); break;
    default:
      Serial.print("[WARN] Неизвестная команда: ");
      Serial.println(data[0]);
      break;
  }
  if(data[0]==113 && len==5){//get pic
    if(QueueOfRequestsLen<=0)
      return;
    int reqId=QueueOfRequests[0];
    int i=0;
    QueueOfRequestsLen--;
    while(i<QueueOfRequestsLen-1)
    {
      QueueOfRequests[i]=QueueOfRequests[i+1];
      i++;
    }
    QueueOfRequests[QueueOfRequestsLen]=0;
    //SetPic(reqId, data[1]);
  }
}

void HandleLedPower(int data[], int len) {
  if (len < 9) return;

  int el = data[1];
  if (el < 0 || el > 6) return;
  bool power = data[5] == 1;
  if(el == LED_SKY || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].on = power;
  }
  if(el == LED_LINES || el == LED_LINES_FLOOR || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].on = power;
  }
  if(el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].on = power;
  }
  ApplyLedPower(el, power);
}

void HandleLedPalitra(int data[], int len) {
  if (len != 13) return;

  int el = data[1];
  if (el < 0 || el > 6) return;
  uint16_t x = (uint16_t)(data[5] | (data[6] << 8));
  uint16_t y = (uint16_t)(data[9] | (data[10] << 8));
  Color rgb = HsvToRgb(x, y);
  if (el == LED_SKY || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].r = rgb.r; leds[LED_SKY].g = rgb.g; leds[LED_SKY].b = rgb.b;
  }
  if (el == LED_LINES || el == LED_LINES_FLOOR || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].r = rgb.r; leds[LED_LINES].g = rgb.g; leds[LED_LINES].b = rgb.b;
  }
  if (el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].r = rgb.r; leds[LED_FLOOR].g = rgb.g; leds[LED_FLOOR].b = rgb.b;
  }
  ApplyLedColor(el, rgb);
}

void HandleLedColor(int data[], int len) {
  if (len != 9) return;

  int el = data[1];
  if (el < 0 || el > 6) return;
  uint16_t rgb565 = (uint16_t)(data[5] | (data[6] << 8));
  Color rgb = Rgb565ToRgb(rgb565);
  if(el == LED_SKY || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].r = rgb.r;
    leds[LED_SKY].g = rgb.g;
    leds[LED_SKY].b = rgb.b;
  }
  if(el == LED_LINES || el == LED_LINES_FLOOR || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].r = rgb.r;
    leds[LED_LINES].g = rgb.g;
    leds[LED_LINES].b = rgb.b;
  }
  if(el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].r = rgb.r;
    leds[LED_FLOOR].g = rgb.g;
    leds[LED_FLOOR].b = rgb.b;
  }
  ApplyLedColor(el, rgb);
}

void HandleLedBright(int data[], int len) {
  if (len < 9) return;
  
  int el    = data[1];        // байт элемента
  int value = data[5];        // первый байт из 4-байтного int
  
  if (el < 0 || el > 6)     return;
  if (value < 0 || value > 100) return;
  if(el == LED_SKY || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].br = value;
  }
  if(el == LED_LINES || el == LED_LINES_FLOOR || el==LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].br = value;
  }
  if(el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].br = value;
  }
  ApplyLedBright(el, value);
}

void HandleLedGetState(int data[], int len){
  if(GetLedStatus()){
    // DisplaySetVal("pageMain."+"p0"+".val", leds[0].on);
    // DisplaySetVal("pageMain."+"r0"+".val", leds[0].r);
    // DisplaySetVal("pageMain."+"g0"+".val", leds[0].g);
    // DisplaySetVal("pageMain."+"b0"+".val", leds[0].b);
    // DisplaySetVal("pageMain."+"br0"+".val", leds[0].br);

    // DisplaySetVal("pageMain."+"p1"+".val", leds[1].on);
    // DisplaySetVal("pageMain."+"r1"+".val", leds[1].r);
    // DisplaySetVal("pageMain."+"g1"+".val", leds[1].g);
    // DisplaySetVal("pageMain."+"b1"+".val", leds[1].b);
    // DisplaySetVal("pageMain."+"br1"+".val", leds[1].br);

    // DisplaySetVal("pageMain."+"p2"+".val", leds[2].on);
    // DisplaySetVal("pageMain."+"r2"+".val", leds[2].r);
    // DisplaySetVal("pageMain."+"g2"+".val", leds[2].g);
    // DisplaySetVal("pageMain."+"b2"+".val", leds[2].b);
    // DisplaySetVal("pageMain."+"br2"+".val", leds[2].br);
    DisplaySetVal("pageMain.t0.en", 1);
  }
}

void HandleGetErrors(int data[], int len){
  Serial.print("HandleGetErrors ");
  if (len < 5) return;
  int page = data[1];
  Serial.println("page #"+String(page));
  const int limit = 8;                    // например, по 5 ошибок на "страницу"
  Error pageErrors[limit];                // временный массив для текущей страницы
  int count = GetErrors(page, limit, pageErrors);
  Serial.print("count #"+String(count));
  String payload = "";                    // сюда собираем все ошибки

  for (int i = 0; i < count; i++)
  {
    uint32_t minutesAgo = 0;
    if (pageErrors[i].tfs > 0) {
      minutesAgo = (millis() - pageErrors[i].tfs) / 60000UL;   // минуты назад
    }

    uint8_t code = pageErrors[i].code;
    auto desc = GetErrorDescription(pageErrors[i].code);
    String line = String(pageErrors[i].code) + "|" +
                  String(pageErrors[i].addr) + "|" +
                  String(pageErrors[i].times) + "|" +
                  String(minutesAgo) + "|" +
                  desc;

    if (i > 0) payload += "\r";
    payload += line;
  }
  if (count == 0) {
    payload = "0|0|0|0|No errors on this page";
  }
  DisplaySetVal("pageHR.t1.txt", payload);
}

void HandleClearErrors(int data[], int len){
  if (len < 1) return;
  ClearAllErrors();
  DisplaySetVal("pageHR.t1.txt", "");
}

void TouchPressEvent(int page, int id){
  Serial.print("Нажата кнопка: ");
  Serial.print(id);

  Button& btn=GetBtn(id);
  Module& mod=GetModule(btn.type);
  Serial.println(btn.friendlyName);
  btn.mode =NextMode(mod.addr, btn.seat);
  DisplaySetVal("pageSofa."+btn.objName+".pic", btn.modePic[btn.mode]);
}

void TouchReleaseEvent(int page, int id){

}

void SetPic(int id, int mode){
  Button& mod=GetBtn(id);
  DisplaySetVal("pageSofa."+(mod.objName)+".pic", mod.modePic[mode]);
}

void ReqPic(int id){
  Button& mod=GetBtn(id);
  Serial.println(mod.objName);
  mySerial.print("get pageSofa."+(mod.objName)+".pic");
  comandEnd();
  QueueOfRequests[QueueOfRequestsLen]=id;
  QueueOfRequestsLen++;
}

Color HsvToRgb(uint16_t x, uint16_t y) {
  x=x-60;
  y=y-50;
  y=185-y;
  // x: 0..184  → Hue 0..359
  // y: 0..184  → Saturation 0.0 .. 1.0 (сверху белый, снизу яркий цвет)

  float hue = (float)x * 360.0 / 184.0;   // переводим в градусы
  float sat = (float)y / 184.0;           // 0.0 = белый, 1.0 = чистый цвет
  float val = 1.0;                        // всегда максимальная яркость

  // Стандартное преобразование HSV → RGB (0..255)
  float c = val * sat;
  float x_ = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
  float m = val - c;

  float r = 0, g = 0, b = 0;

  if      (hue < 60)  { r = c; g = x_; b = 0; }
  else if (hue < 120) { r = x_; g = c; b = 0; }
  else if (hue < 180) { r = 0; g = c; b = x_; }
  else if (hue < 240) { r = 0; g = x_; b = c; }
  else if (hue < 300) { r = x_; g = 0; b = c; }
  else                { r = c; g = 0; b = x_; }

  r = (r + m) * 255.0;
  g = (g + m) * 255.0;
  b = (b + m) * 255.0;
  Color rgb = {r, g, b};
  return rgb;
}

uint16_t RgbToRgb565(Color rgb)
{
  // Преобразование в 16-бит RGB565 (Nextion формат)
  uint8_t r5 = (uint8_t)rgb.r >> 3;   // 5 бит
  uint8_t g6 = (uint8_t)rgb.g >> 2;   // 6 бит
  uint8_t b5 = (uint8_t)rgb.b >> 3;   // 5 бит
  return (r5 << 11) | (g6 << 5) | b5;
}

Color Rgb565ToRgb(uint16_t rgb565)
{
  uint8_t r = (rgb565 >> 11) & 0x1F;
  uint8_t g = (rgb565 >> 5)  & 0x3F;
  uint8_t b =  rgb565        & 0x1F;
  r = (r * 255) / 31;
  g = (g * 255) / 63;
  b = (b * 255) / 31;
  Color rgb = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
  return rgb;
}






Button& GetBtn(int id){
  int len=4;
  while(len>0)
  {
    len--;
    if(btns[len].id==id)
      return btns[len];
  }
  Serial.println("out of array");
}

Module& GetModule(int type){
  int len=3;
  while(len>0)
  {
    len--;
    if(mods[len].type==type)
      return mods[len];
  }
  Serial.println("out of array");
}

void DisplaySetVal(String path, int val){
  mySerial.print(path);
  mySerial.print("="); 
  mySerial.print(val);
  comandEnd();
}

void DisplaySetVal(String path, String val){
  mySerial.print(path);
  mySerial.print("="); 
  mySerial.print(val);
  comandEnd();
}

// функция отправки конца команды «0xFF 0xFF 0xFF»
void comandEnd() {
  for (int i = 0; i < 3; i++) {
    mySerial.write(0xff);
  }
}

void SetupBtns(){
  btns[0]={};
  btns[0].id=7;
  btns[0].objName="v1";
  btns[0].friendlyName="Ventilation left";
  btns[0].modePic[0]=29;
  btns[0].modePic[1]=30;
  btns[0].modePic[2]=31;
  btns[0].modePic[3]=32;
  btns[0].type=VentType;
  btns[0].seat=0;

  btns[1]={};
  btns[1].id=2;
  btns[1].objName="v2";
  btns[1].friendlyName="Ventilation right";
  btns[1].modePic[0]=33;
  btns[1].modePic[1]=34;
  btns[1].modePic[2]=35;
  btns[1].modePic[3]=36;
  btns[1].type=VentType;
  btns[1].seat=1;

  btns[2]={};
  btns[2].id=6;
  btns[2].objName="m1";
  btns[2].friendlyName="Massage left";
  btns[2].modePic[0]=17;
  btns[2].modePic[1]=18;
  btns[2].modePic[2]=19;
  btns[2].modePic[3]=20;
  btns[2].type=MasType;
  btns[2].seat=0;

  btns[3]={};
  btns[3].id=3;
  btns[3].objName="m2";
  btns[3].friendlyName="Massage right";
  btns[3].modePic[0]=6;
  btns[3].modePic[1]=7;
  btns[3].modePic[2]=8;
  btns[3].modePic[3]=9;
  btns[3].type=MasType;
  btns[3].seat=1;

  btns[4]={};
  btns[4].id=8;
  btns[4].objName="h1";
  btns[4].friendlyName="Heat left";
  btns[4].modePic[0]=14;
  btns[4].modePic[1]=15;
  btns[4].modePic[2]=16;
  btns[4].modePic[3]=0;
  btns[4].type=HeatType;
  btns[4].seat=0;

  btns[5]={};
  btns[5].id=1;
  btns[5].objName="h2";
  btns[5].friendlyName="Heat right";
  btns[5].modePic[0]=0;
  btns[5].modePic[1]=1;
  btns[5].modePic[2]=2;
  btns[5].modePic[3]=0;
  btns[5].type=HeatType;
  btns[5].seat=0;
}

void SetupMods(){
  mods[0].type=MasType;
  mods[0].addr=MASSAGE_ADDR;
  mods[0].friendlyName="Пневмомассаж спинки";

  mods[1].type=VentType;
  mods[1].addr=VENTILATION_ADDR;
  mods[1].friendlyName="Вентиляция дивана";

  mods[2].type=HeatType;
  mods[2].addr=HEAT_ADDR;
  mods[2].friendlyName="Обогрев дивана";

  mods[3].type=LightType;
  mods[3].addr=LIGHT_ADDR;
  mods[3].friendlyName="Атмосферная подсветка";
}

void logS(String str){
  if(!isDebug)
    return;
  Serial.println(str);
}

void logI(String str, int i){
  if(!isDebug)
    return;
  Serial.print(str);
  Serial.print(" : ");
  Serial.println(i);
}