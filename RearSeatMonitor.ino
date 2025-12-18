bool isDebug=true;

#include <SoftwareSerial.h>
#include <Wire.h>

#define MASSAGE_ADDR 10
#define VENTILATION_ADDR 20
#define HEAT_ADDR 30

#define REG_L_MODE 0x01
#define REG_L_GetStatus 0x02
#define REG_R_MODE 0x03
#define REG_R_GetStatus 0x04
#define REG_GetErrorCount 0x05
#define REG_GetNextError 0x06

#define MasType 0; //seat massage
#define VentType 1; //seat ventilation
#define HeatType 2; //seat heater

// Модули на шине I2C
struct Module{
  byte type=0;
  int addr=0;
  String friendlyName="";
  bool isOnline=false;
};
Module mods[3];
int ModuleLen=3;

// serial port для связи с дисплеем
SoftwareSerial mySerial(8, 9);
int num=0;
int disPacketPointer=0;
int disPacket[8];
uint32_t lastMessage=0;

int QueueOfRequestsLen=0;
int QueueOfRequests[10];

//Кнопки на дисплее
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
  uint16_t code=0;
  uint32_t tfs=0;
  uint8_t times=0;
  uint8_t addr=0;
  uint32_t tfsr=0;
};
Error errors[15];
int sizeErr;
int nextError=0;

void setup() {
  sizeErr=sizeof(errors[0]);
  SetupBtns();
  SetupMods();
  
  mySerial.begin(9600);
  mySerial.setTimeout(50);
  Serial.begin(9600);
  Serial.println("Hello!");
  Wire.begin();
  Wire.setWireTimeout(250000, false);
  pinMode(LED_BUILTIN, OUTPUT);
  ScanModules();
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
  HealthReport();
}

bool HealthReport(){
  if(millis()-lastMessage>=10000){
    lastMessage=millis();
    for(int i=0; i<ModuleLen; i++){
      if(!mods[i].isOnline)
        continue;
      if(HasErrors(i)){
        while(GetNextError(mods[i].addr)){
          nextError++;
          if(nextError>20) break;
        }
        nextError=0;
      }
    }
  }
}

void ToDo(int data[], int len){
  if(len==0)
    return;
  Serial.print("Длинна сообщения: ");
  Serial.print(disPacketPointer);
  Serial.print(" ");
  Serial.print(endcombyte);
  Serial.print(" ");
  Serial.println(len);
  if(data[0]==101 && len==4){//touch
    if(data[3]==1)
      TouchPressEvent(data[1], data[2]);
    else if(data[3]==0)
      TouchReleaseEvent(data[1], data[2]);
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

void TouchPressEvent(int page, int id){
  Serial.print("Нажата кнопка: ");
  Serial.print(id);

  Button& btn=GetBtn(id);
  Module& mod=GetModule(btn.type);
  Serial.println(btn.friendlyName);
  int newMode=NextMode(mod.addr, btn.seat);
  btn.mode = newMode;
  DisplaySetVal("pageSofa."+btn.objName+".pic", btn.modePic[newMode]);
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
  mySerial.print(path);   // Отправляем данные dev(номер экрана, название переменной) на Nextion
  mySerial.print("=");   // Отправляем данные =(знак равно, далее передаем сами данные) на Nextion 
  mySerial.print(val);  // Отправляем данные data(данные) на Nextion
  comandEnd();
}

// функция отправки конца команды «0xFF 0xFF 0xFF»
void comandEnd() {
  for (int i = 0; i < 3; i++) {
    mySerial.write(0xff);
  }
}

int NextMode(int addr, int seat) {
  Wire.beginTransmission(addr);
  Wire.write(seat==0 ? REG_L_MODE : REG_R_MODE);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 1);
  return Wire.read();
}

int GetStatus(int addr, int seat) {
  Wire.beginTransmission(addr);
  Wire.write(seat==0 ? REG_L_GetStatus : REG_R_GetStatus);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 1);
  return Wire.read();
}

bool HasErrors(int i){
  Wire.beginTransmission(mods[i].addr);
  Wire.write(REG_GetErrorCount);
  Wire.endTransmission(false);
  Wire.requestFrom(mods[i].addr, 5);
  uint32_t now=0;
  uint8_t numOfErrors=0;
  I2C_readAnything(numOfErrors);
  I2C_readAnything(now);

  if(numOfErrors==255){
    SaveError(4+i);
    return false;
  }
  return numOfErrors>0;
}

bool GetNextError(int addr){
  Wire.beginTransmission(addr);
  Wire.write(REG_GetNextError);
  Wire.write(nextError);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 8);
  if(Wire.read()==0) return false;

  uint16_t code=0;
  uint32_t tfs=0;
  uint8_t times=0;
  I2C_readAnything(code);
  I2C_readAnything(tfs);
  I2C_readAnything(times);
  
  SaveRemoteError(code, tfs, times, addr);
  return true;
}

void ScanModules(){
  for (int i=0; i<3; i++) {
    Wire.beginTransmission(mods[i].addr);
    if (Wire.endTransmission()) {
      mods[i].isOnline=false;
      SaveError(i+1);
    } else {
      mods[i].isOnline=true;
    }
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
