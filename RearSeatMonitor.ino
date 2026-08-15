bool isDebug=true;
#include <SoftwareSerial.h>
#include <Wire.h>
#include "I2CMaster.h"
I2CMaster master;


#define MasType 0 //seat massage
#define VentType 1 //seat ventilation
#define HeatType 2 //seat heater
#define LightType 3 //ambient light
#define VibroType 4 //ambient light

// UART команды
#define CMD_TOUCH 101 // OnPress onRelease
#define CMD_GET_PIC 113 //Ответ на запрос картиинки
#define CMD_LED_POWER 1 // Вкл/выкл [1, элемент LE, 0/1 LE] len=9
#define CMD_LED_PALITRA 2 // Цвет с палитры [2, элемент LE, y LE] len=9
#define CMD_LED_PALITRA_WHITE 8 // Цвет с палитры белого цвета [2, элемент LE, y LE] len=9
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
  uint8_t colorCursor;
  uint8_t whiteCursor;
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
Module mods[5];
int ModuleLen=5;

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
byte btnLen=8;
Button btns[8];

bool firstShoted = false;
struct LedCommand {
  uint8_t el;
  Color   rgb;
  bool    pending;
};

static LedCommand    _ledBuf     = { 0, {0,0,0}, false };
static unsigned long _lastLedSent = 0;
static const unsigned long LED_THROTTLE_MS = 500;

void setup() {
  delay(2000);
  Serial.begin(115200);

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
  FlushLedColorBuffer();
  if(mySerial.available()){ 
    byte inc = mySerial.read();
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

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "setpic0") {
      DisplaySetVal("pageSofa.m1.pic", 18);
    } else if (command == "setpic1") {
      DisplaySetVal("pageSofa.m1.pic", 19);
    }
  }
}

//I2C commands
void HealthReport() {
  logS("HealthReport");
  for(int m=0; m<ModuleLen; m++)
  {
    Serial.println(mods[m].friendlyName);
    bool ping=I2C_Ping(mods[m].addr);
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

byte NextMode(uint8_t slaveAddr, uint8_t seat) {
  Serial.println("NextMode");
  uint8_t reg = (seat == 0) ? REG_L_MODE : REG_R_MODE;
  uint8_t resp[2];
  auto res = master.transaction(slaveAddr, reg, resp, 2);
  Serial.println("req="+String((res==0?"OK":"NotDelivered")));
  if (res != I2CMaster::OK) return 0xFF;  // ошибка
  Serial.println("resp="+String(resp[1]));
  return resp[1];
}

void ApplyLedPower(uint8_t el, bool power) {
  uint8_t params[] = {el, power ? 1u : 0u};
  uint8_t resp[1];
  master.transaction(LIGHT_ADDR, REG_Power, params, sizeof(params), resp, 1);
}

void ApplyLedColorBuffered(uint8_t el, Color rgb) {
  if (!firstShoted) {
    uint8_t params[] = { el, rgb.r, rgb.g, rgb.b };
    uint8_t resp[1];
    master.transaction(LIGHT_ADDR, REG_SetRGB, params, sizeof(params), resp, 1);
    _lastLedSent = millis();
    firstShoted=true;
    Serial.println("firstshoted");
  } else {
    Serial.println("buffered");
    _ledBuf = { el, rgb, true };
  }
}

void FlushLedColorBuffer() {
  if (!_ledBuf.pending)
  {
    if (firstShoted && (millis() - _lastLedSent) > LED_THROTTLE_MS)
    {
      firstShoted=false;
      Serial.println("fs reseted"+String(millis() - _lastLedSent));
    }
    return;
  }
  if (millis() - _lastLedSent < LED_THROTTLE_MS) return;

  Serial.println("ColorChange");
  uint8_t params[] = { _ledBuf.el, _ledBuf.rgb.r, _ledBuf.rgb.g, _ledBuf.rgb.b };
  uint8_t resp[1];
  master.transaction(LIGHT_ADDR, REG_SetRGB, params, sizeof(params), resp, 1);
  _lastLedSent    = millis();
  _ledBuf.pending = false;
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
  return false;
  Serial.println("GetLedStatus");
  uint8_t resp[16];
  auto res = master.transaction(LIGHT_ADDR, REG_GetLightStatus, resp, sizeof(resp));
  if (res != I2CMaster::OK) return false;
  leds[0].on=resp[1]==1;
  leds[0].r=resp[2];
  leds[0].g=resp[3];
  leds[0].b=resp[4];
  leds[0].br=resp[5];
  leds[1].on=resp[6]==1;
  leds[1].r=resp[7];
  leds[1].g=resp[8];
  leds[1].b=resp[9];
  leds[1].br=resp[10];
  leds[2].on=resp[11]==1;
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

//deprecated
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
  for(int i=0; i<len; i++)
  {
    Serial.print(data[i]);
    Serial.print(" ");
  }
  Serial.println();
  switch (data[0]) {
    case CMD_TOUCH:
      if (len == 4) {
        if (data[3] == 1) TouchPressEvent(data[1], data[2]);
        else if (data[3] == 0) TouchReleaseEvent(data[1], data[2]);
      }
      break;

    case CMD_LED_POWER: HandleLedPower(data, len);      break;
    case CMD_LED_PALITRA: HandleLedPalitra(data, len);      break;
    case CMD_LED_PALITRA_WHITE: HandleLedPalitraWhite(data, len);      break;
    case CMD_LED_COLOR: HandleLedColor(data, len);      break;
    case CMD_LED_BRIGHT: HandleLedBright(data, len); break;
    case CMD_LED_GETSTATE: HandleLedGetState(data, len);   break;
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
  if (len != 9) return;

  int el = data[1];
  if (el < 0 || el > 6) return;
  uint16_t y = (uint16_t)(data[5] | (data[6] << 8));
  Color rgb = HsvToRgb(y);
  if (el == LED_SKY || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].r = rgb.r; leds[LED_SKY].g = rgb.g; leds[LED_SKY].b = rgb.b;
  }
  if (el == LED_LINES || el == LED_LINES_FLOOR || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].r = rgb.r; leds[LED_LINES].g = rgb.g; leds[LED_LINES].b = rgb.b;
  }
  if (el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].r = rgb.r; leds[LED_FLOOR].g = rgb.g; leds[LED_FLOOR].b = rgb.b;
  }
  ApplyLedColorBuffered(el, rgb);
}

void HandleLedPalitraWhite(int data[], int len) {
  if (len != 9) return;

  int el = data[1];
  if (el < 0 || el > 6) return;
  uint16_t y = (uint16_t)(data[5] | (data[6] << 8));
  Color rgb = WarmToColor(y);
  if (el == LED_SKY || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_SKY].r = rgb.r; leds[LED_SKY].g = rgb.g; leds[LED_SKY].b = rgb.b;
  }
  if (el == LED_LINES || el == LED_LINES_FLOOR || el == LED_SKY_LINES || el == LED_ALL) {
    leds[LED_LINES].r = rgb.r; leds[LED_LINES].g = rgb.g; leds[LED_LINES].b = rgb.b;
  }
  if (el == LED_FLOOR || el == LED_LINES_FLOOR || el == LED_ALL) {
    leds[LED_FLOOR].r = rgb.r; leds[LED_FLOOR].g = rgb.g; leds[LED_FLOOR].b = rgb.b;
  }
  ApplyLedColorBuffered(el, rgb);
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
  return;
  Serial.println("HandleLedGetState");
  if(GetLedStatus()){
    Serial.println("HandleLedGetState->GetLedStatus");
    DisplaySetVal("pageMain.vaP1.val", leds[0].on);
    DisplaySetVal("pageMain.vaCol1.val", RgbToRgb565(leds[0].r, leds[0].g, leds[0].b));
    DisplaySetVal("pageMain.vaBr1.val", leds[0].br);
    DisplaySetVal("pageMain.", leds[0].colorCursor);

    DisplaySetVal("pageMain.vaP2.val", leds[1].on);
    DisplaySetVal("pageMain.vaCol2.val", RgbToRgb565(leds[1].r, leds[1].g, leds[1].b));
    DisplaySetVal("pageMain.vaBr2.val", leds[1].br);

    DisplaySetVal("pageMain.vaP3.val", leds[2].on);
    DisplaySetVal("pageMain.vaCol3.val", RgbToRgb565(leds[2].r, leds[2].g, leds[2].b));
    DisplaySetVal("pageMain.vaBr3.val", leds[2].br);
  }
}

void TouchPressEvent(int page, int id){
  Serial.print("Нажата кнопка: ");
  Serial.print(id);

  Button& btn=GetBtn(id);
  Module& mod=GetModule(btn.type);
  byte newMode =NextMode(mod.addr, btn.seat);
  btn.mode=newMode;
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "%s.pic", btn.objName.c_str());
  DisplaySetCharVal(cmd, btn.modePic[newMode]);
}

void TouchReleaseEvent(int page, int id){

}

void SetPic(int id, int mode){
  Button& mod=GetBtn(id);
  DisplaySetVal((mod.objName)+".pic", mod.modePic[mode]);
}

void ReqPic(int id){
  Button& mod=GetBtn(id);
  Serial.println(mod.objName);
  mySerial.print("get "+(mod.objName)+".pic");
  comandEnd();
  QueueOfRequests[QueueOfRequestsLen]=id;
  QueueOfRequestsLen++;
}

int Snap(int n, int sn, int en, int min, int max) {
    if (n < sn) return min;
    if (n > en) return max;
    return map(n, sn, en, min, max);
}

Color HsvToRgb(uint16_t y) {
  if(y<50) y=50;
  y=y-50;
  //y=185-y;
  //y=Snap(y, 50, 135, 0, 185);

  float sat = 1.0;
  float val = 1.0;
  float hue = (float)y * 360.0 / 185.0;

  float c  = val * sat;
  float x_ = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
  float m  = val - c;

  float r = 0, g = 0, b = 0;

  if      (hue < 60)  { r = c;  g = x_; b = 0;  }
  else if (hue < 120) { r = x_; g = c;  b = 0;  }
  else if (hue < 180) { r = 0;  g = c;  b = x_; }
  else if (hue < 240) { r = 0;  g = x_; b = c;  }
  else if (hue < 300) { r = x_; g = 0;  b = c;  }
  else                { r = c;  g = 0;  b = x_; }

  Color rgb;
  rgb.r = (uint8_t)((r + m) * 255.0);
  rgb.g = (uint8_t)((g + m) * 255.0);
  rgb.b = (uint8_t)((b + m) * 255.0);
  return rgb;
}

Color WarmToColor(uint8_t y) {
  // y: число от 0 до 185
  // 0   -> #c8e1ff = rgb(200, 225, 255)  холодный белый
  // 185 -> #ffc882 = rgb(255, 200, 130)  тёплый белый
  if(y<50) y=50;
  y=y-50;
  float t = (float)y / 185.0;  // 0.0 .. 1.0

  Color rgb;
  rgb.r = (uint8_t)(200 + t * (255 - 200));
  rgb.g = (uint8_t)(225 + t * (200 - 225));
  rgb.b = (uint8_t)(255 + t * (130 - 255));
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

uint16_t RgbToRgb565(byte r, byte g, byte b)
{
  // Преобразование в 16-бит RGB565 (Nextion формат)
  uint8_t r5 = r >> 3;   // 5 бит
  uint8_t g6 = g >> 2;   // 6 бит
  uint8_t b5 = b >> 3;   // 5 бит
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






Button& GetBtn(byte id){
  byte len=btnLen;
  while(len>0)
  {
    len--;
    if(btns[len].id==id)
      return btns[len];
  }
  Serial.println("out of array");
  return btns[0];
}

Module& GetModule(byte type){
  byte len=ModuleLen;
  while(len>0)
  {
    len--;
    if(mods[len].type==type)
      return mods[len];
  }
  Serial.println("out of array");
  return mods[0];
}

void DisplaySetCharVal(const char* path, int val) {
  mySerial.print(path);
  mySerial.print("=");
  mySerial.print(val);
  comandEnd();
}


void DisplaySetVal(String path, int val){
  mySerial.print(path);
  mySerial.print("="); 
  mySerial.print(val);
  comandEnd();
}

void DisplaySetTxt(String path, String val){
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
  btns[5].seat=1;

  btns[6]={};
  btns[6].id=5;
  btns[6].objName="mv1";
  btns[6].friendlyName="Vibro left";
  btns[6].modePic[0]=24;
  btns[6].modePic[1]=25;
  btns[6].modePic[2]=26;
  btns[6].modePic[3]=27;
  btns[6].type=VibroType;
  btns[6].seat=0;

  btns[7]={};
  btns[7].id=4;
  btns[7].objName="mv2";
  btns[7].friendlyName="Vibro right";
  btns[7].modePic[0]=10;
  btns[7].modePic[1]=11;
  btns[7].modePic[2]=12;
  btns[7].modePic[3]=13;
  btns[7].type=VibroType;
  btns[7].seat=1;
}

void SetupMods(){
  mods[0].type=MasType;
  mods[0].addr=MASSAGE_ADDR;
  mods[0].friendlyName="Massage";

  mods[1].type=VentType;
  mods[1].addr=VENTILATION_ADDR;
  mods[1].friendlyName="Ventilation";

  mods[2].type=HeatType;
  mods[2].addr=HEAT_ADDR;
  mods[2].friendlyName="Heat";
  mods[3].type=LightType;
  mods[3].addr=LIGHT_ADDR;
  mods[3].friendlyName="Ambient";

  mods[4].type=VibroType;
  mods[4].addr=VIBRO_ADDR;
  mods[4].friendlyName="Vibro";
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