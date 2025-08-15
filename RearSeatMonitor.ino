// библиотека для эмуляции Serial порта
#include <SoftwareSerial.h>
// создаём объект mySerial и передаём номера управляющих пинов RX и TX
// RX - цифровой вывод 8, необходимо соединить с выводом TX дисплея
// TX - цифровой вывод 9, необходимо соединить с выводом RX дисплея
SoftwareSerial mySerial(8, 9);
int num=0;
int disPacketPointer=0;
int disPacket[8];

int QueueOfRequestsLen=0;
int QueueOfRequests[10];

struct Module{
  int id=0;
  String objName="";
  String friendlyName="";
  int mode=0;
  int modeSeq[4];
  int modePic[4];
};

Module mods[4];


#include <Wire.h>
#define SLAVE_ADDR 10
#define REG_L_MODE 0x01
#define REG_L_GetStatus 0x02
#define REG_R_MODE 0x03
#define REG_R_GetStatus 0x04
#define REG_GetErrorCount 0x05
#define REG_GetNextError 0x06

void setup() {
  mods[0]={};
  mods[0].id=7;
  mods[0].objName="v1";
  mods[0].friendlyName="Ventilation left";
  mods[0].modeSeq[0]=0;
  mods[0].modeSeq[1]=2;
  mods[0].modeSeq[2]=1;
  mods[0].modePic[0]=21;
  mods[0].modePic[1]=22;
  mods[0].modePic[2]=23;

  mods[1]={};
  mods[1].id=2;
  mods[1].objName="v2";
  mods[1].friendlyName="Ventilation right";
  mods[1].modeSeq[0]=0;
  mods[1].modeSeq[1]=2;
  mods[1].modeSeq[2]=1;
  mods[1].modePic[0]=3;
  mods[1].modePic[1]=4;
  mods[1].modePic[2]=5;

  mods[2]={};
  mods[2].id=6;
  mods[2].objName="m1";
  mods[2].friendlyName="Massage left";
  mods[2].modeSeq[0]=0;
  mods[2].modeSeq[1]=1;
  mods[2].modeSeq[2]=2;
  mods[2].modeSeq[3]=3;
  mods[2].modePic[0]=17;
  mods[2].modePic[1]=18;
  mods[2].modePic[2]=19;
  mods[2].modePic[3]=20;
   
   // открываем последовательный порт
  mySerial.begin(9600);
  mySerial.setTimeout(50);
  Serial.begin(9600);
  Serial.println("Hello!");
  Wire.begin();
  Wire.setWireTimeout(25000, false);
  pinMode(LED_BUILTIN, OUTPUT);
}

int endcombyte=0;
void loop() {
  if (mySerial.available()) {  
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
  Serial.println(id);
  
  if(id==7)
  {
    //v1
    //ReqPic(id);
    int newMode=NextMode(0);
    Module& mod=GetMod(id);
    mod.mode = newMode;
    SendInt("pageSofa.v1.pic", mods[0].modePic[newMode]);
  }
  if(id==2)
  {
    //v2
    ReqPic(id);
  }
  if(id==6)
  {
    //m1
    ReqPic(id);
  }
  if(id==3)
  {
    //m2
  }
}


void TouchReleaseEvent(int page, int id){

}

void SetPic(int id, int curPic){
  Module& mod=GetMod(id);
  mod.mode = GetCurMode(mod, curPic);
  mod.mode = GetNextMode(mod);
  mySerial.print("pageSofa."+(mod.objName)+".pic=");
  mySerial.print(mod.modePic[mod.mode]);
  comandEnd();
}

void ReqPic(int id){
  Module& mod=GetMod(id);
  Serial.println(mod.objName);
  mySerial.print("get pageSofa."+(mod.objName)+".pic");
  comandEnd();
  QueueOfRequests[QueueOfRequestsLen]=id;
  QueueOfRequestsLen++;
}

Module& GetMod(int id){
  int len=3;
  while(len>0)
  {
    len--;
    if(mods[len].id==id)
      return mods[len];
  }
  Serial.println("out of array");
}

int GetCurMode(Module& mod, int curPic){
  int i=0;
  while(i<4)
  {
    if(mod.modePic[i]==curPic)
    {
      break;
    }
    i++;
  }
  if(i>=4){
    Serial.println("out of array");
    return -1;
  }
  return i;
}

int GetNextMode(Module& mod){
  int i=0;
  while(i<4)
  {
    if(mod.modeSeq[i]==mod.mode)
    {
      i++;
      break;
    }
    i++;
  }
  if(i>=4)
    i=0;
  return mod.modeSeq[i];
}

// функция отправки конца команды «0xFF 0xFF 0xFF»
void comandEnd() {
  for (int i = 0; i < 3; i++) {
    mySerial.write(0xff);
  }
}

void SendInt(String path, int val){
  mySerial.print(path);   // Отправляем данные dev(номер экрана, название переменной) на Nextion
  mySerial.print("=");   // Отправляем данные =(знак равно, далее передаем сами данные) на Nextion 
  mySerial.print(val);  // Отправляем данные data(данные) на Nextion
  comandEnd();
}

int NextMode(int seat) {
    Wire.beginTransmission(SLAVE_ADDR);
    Wire.write(seat==0 ? REG_L_MODE : REG_R_MODE);
    Wire.endTransmission(false);
    Wire.requestFrom(SLAVE_ADDR, 1);
    return Wire.read();
}

int GetStatus(int seat) {
    Wire.beginTransmission(SLAVE_ADDR);
    Wire.write(seat==0 ? REG_L_GetStatus : REG_R_GetStatus);
    Wire.endTransmission(false);
    Wire.requestFrom(SLAVE_ADDR, 1);
    return Wire.read();
}
