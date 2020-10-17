#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <AliyunIoTSDK.h>

#define DEBUG 0x00

#include <string.h>
#include <math.h>  // 数学工具库
#include <EEPROM.h>// eeprom  存储库
//-------客户端---------
#include <ESP8266HTTPClient.h>
//---------服务器---------
#include <ESP8266WebServer.h>
#include <FS.h>
//ESP获取自身ID
#ifdef ESP8266
extern "C" {
#include "user_interface.h"   //含有system_get_chip_id（）的库
}
#endif
//-----------------------------A-1 库引用结束-----------------//
//-----------------------------A-2 变量声明开始-----------------//
int PIN_Led = D4;
bool PIN_Led_State=0;
ESP8266WebServer server ( 80 );
//储存SN号
String SN;
/*----------------WIFI账号和密码--------------*/
 char ssid[50] = "";    // Enter SSID here
 char password[50] = "";  //Enter Password here
#define DEFAULT_STASSID "CMCC-WWLG"
#define DEFAULT_STAPSW  "67rbmpbc"
// 用于存上次的WIFI和密码
#define MAGIC_NUMBER 0xAA
struct config_type
{
  char stassid[50];
  char stapsw[50];
  uint8_t magic;
};
config_type config_wifi;
//-----------------------------A-2 变量声明结束-----------------//
//-----------------------------A-3 函数声明开始-----------------//
bool wifi_Init();
void saveConfig();
void loadConfig();
String getContentType(String filename);
void handleNotFound();
void handleMain();
void handleWifi();
void LED_Int();
void get_espid();
//---------------------------A-3 函数声明结束------------------------//




typedef struct AliInfo
{
  char *product_key;
  char *device_name;
  char *device_secret;
  char *region_id;
} AliInfoNode;

const AliInfoNode aliInfoNodes[] = 
{
  {
    "a1jPdgK6z6o",
    "device00",
    "243ccfe08e36886922343cec86e07816",
    "cn-shanghai"
  },
  {
    "a188RlrKdmx",
    "device01",
    "2e193f04f76b7beb8063a93f1d74f7a4",
    "cn-shanghai"
  },
  {
    "a188RlrKdmx",
    "device02",
    "4b1f55cbf3982069eacbbef4fd97c1f1",
    "cn-shanghai"
  },
  {
    "a188RlrKdmx",
    "device03",
    "432cf8c687530a4a3355489e8c5d6481",
    "cn-shanghai"
  },
  {
    "a188RlrKdmx",
    "device04",
    "a5e4ebde132f0b72206b8453e2408459",
    "cn-shanghai"
  }
};

typedef unsigned char uint8;

typedef struct _nodeInfo
{
  uint8 tempH;
  uint8 tempL;
  uint8 mq2;
  uint8 alarmSwitch;
  uint8 alarmState;
} NodeInfo;

struct _coreInfo
{
  uint8 alarmSwitch;
  uint8 aliveNum;
  uint8 alarmState;
  uint8 alarmArea;
  uint8 light;
} CoreInfo;


static const unsigned char CLOSE_ALARM[] = {0x3A, 0xFF, 0xFF, 0x0A, 0x00, 0x30, 0x23};
static const unsigned char OPEN_ALARM[] = {0x3A, 0xFF, 0xFF, 0x0A, 0x01, 0x31, 0x23};

char buf[16];
unsigned long lastMsMain = 0;

char ch;
char serialBuf[30];
#define serialMaxLength 29
char serialIndex = 0;
bool serialFlag = 0;
bool serialAvailable = 0;
char serialLength = 0;
char checksum = 0;
//#define nodeInfoNum 5
#define nodeMax 4

NodeInfo nodeInfo[nodeMax];
char rfidAddr = 0;
char rfidBuf[7] = {0};
bool rfidReady = 0;

char nodeAlive = 0;
char nodeLeave = 0;
unsigned long lastMsGet;
WiFiClient *espClient[nodeMax+1];
AliyunIoTSDK *device[nodeMax+1];
bool deviceAlive[nodeMax+1] = {0};

void send_to_ali();
void alarmCallback(JsonVariant p);
void wifiInit(const char *ssid, const char *passphrase);
char NumberToLetter(unsigned char number);


void setup()
{
    Serial.begin(115200);
    
    get_espid();
    LED_Int();
    loadConfig();// 读取信息 WIFI
    if(!wifi_Init()) // 30s尝试
    {
      SET_AP(); // 建立WIFI
      SPIFFS.begin();
      Server_int();
      while(true)
      {
        server.handleClient();
      }
    }
    digitalWrite(PIN_Led, HIGH);
    espClient[0] = new WiFiClient;
    device[0] = new AliyunIoTSDK;
    device[0]->begin(*(espClient[0]), aliInfoNodes[0].product_key, aliInfoNodes[0].device_name, aliInfoNodes[0].device_secret, aliInfoNodes[0].region_id);
    device[0]->bindData("AlarmSwitch", alarmCallback);
    deviceAlive[0] = 1;
    delay(5000);
    lastMsMain = millis();
    lastMsGet = millis();
}


void loop()
{
    device[0]->loop();
    for(int i = 1; i <= nodeMax; i++)
    {
      if(deviceAlive[i])
      {
        device[i]->loop();
      }
    }
    
    if (millis() - lastMsMain >= 7000)
    {
        send_to_ali();
        lastMsMain = millis();
    }

    if (millis() - lastMsGet >= 3000)
    {
        Serial.write("\x3A\xFF\xFF\x01\x3B\x23", 6);
        delay(50);
        Serial.write("\x3A\x00\x00\x04\x3E\x23", 6);
        lastMsGet = millis();
    }

    if(!serialAvailable && Serial.available())
    {
      if(serialFlag)
      {
        serialBuf[serialIndex] = Serial.read();
        checksum ^= serialBuf[serialIndex];
        if(serialBuf[serialIndex] == 0x23 || ++serialIndex >= serialMaxLength)
        {
          serialFlag = 0;
          if(checksum == 0x23)
          {
            serialAvailable = 1;
            serialLength = serialIndex;
          }
          serialIndex = 0;
          checksum = 0;
          serialFlag = 0;
        }
      }
      else
      {
        ch = Serial.read();
        if(ch == 0x3a || ch == 0x31)
        {
          serialFlag = 1;
          serialBuf[serialIndex++] = ch;
          checksum = ch;
        }
      }
    }


    if(serialAvailable)
    {
      #if DEBUG
      #endif
      serialAvailable = 0;
      switch(serialBuf[0])
      {
        case 0x31:
          switch(serialBuf[3])
          {
            case 0x01:
              rfidAddr = serialBuf[2];
              break;
            case 0x02:
              nodeAlive = serialBuf[2];
              break;
            case 0x03:
              nodeLeave = serialBuf[2];
              break;
            default:
              break;
          }
          break;
        case 0x3a:
          switch(serialBuf[3])
          {
            case 0x01:
              memcpy(nodeInfo, serialBuf+4, nodeMax*sizeof(NodeInfo));
              break;
            case 0x03:
              memcpy(rfidBuf, serialBuf+4, 6);
              rfidBuf[6] = '\0';
              rfidReady = 1;
              rfidAddr = serialBuf[2];
            case 0x04:
              if(serialBuf[2] == 0)
              {
                memcpy(&CoreInfo, serialBuf+4, sizeof(CoreInfo));
              }
              else
              {
                memcpy(nodeInfo+(serialBuf[2]-1), serialBuf+4, sizeof(NodeInfo));
              }
              break;
          }
          break;
        default:
          break;
      }
    }
    
    if(nodeAlive)
    {
      #if DEBUG
      Serial.print("Alive:");
      Serial.println(nodeAlive);
      #endif
      espClient[nodeAlive] = new WiFiClient;
      device[nodeAlive] = new AliyunIoTSDK;
      device[nodeAlive]->begin(*(espClient[nodeAlive]), aliInfoNodes[nodeAlive].product_key, aliInfoNodes[nodeAlive].device_name, aliInfoNodes[nodeAlive].device_secret, aliInfoNodes[nodeAlive].region_id);
      deviceAlive[nodeAlive] = 1;
      nodeAlive = 0;
    }
    
    if(nodeLeave)
    {
      #if DEBUG
      Serial.print("Leave:");
      Serial.println(nodeLeave);
      #endif
      free(espClient[nodeAlive]);
      free(device[nodeAlive]);
      deviceAlive[nodeLeave] = 0;
      nodeLeave = 0;
    }

    if(rfidReady)
    {
      if(rfidBuf[0]==0x04 && rfidBuf[1]==0x00)
      {
        char Card_Buff[20]={0};
        for(int i=0; i<4; i++)
        {
            unsigned char temp= rfidBuf[2+i];
            Card_Buff[i*2]=NumberToLetter((temp>>4)&0x0f);
            Card_Buff[i*2+1]=NumberToLetter(temp&0x0f);
        }
        #if DEBUG
        Serial.print("Rfid:");
        Serial.println(rfidAddr);
        Serial.println(Card_Buff);
        #else
        device[0]->send("RfidId", Card_Buff);
        device[0]->send("RfidArea", rfidAddr);
        #endif
      }
      
      rfidReady = 0;
      rfidAddr = 0;
    }
    
    if(rfidAddr)
    {
      rfidAddr = 0;
      Serial.write("\x3a\x00\x04\x03\x3d\x23", 6);
    }

    delay(100);
}
//BCD转ASC码表
char NumberToLetter(unsigned char number)
{
    char buff[]="0123456789ABCDEF";
    if(number>15) return 0;
    return buff[number];

}

void alarmCallback(JsonVariant p)
{
    int alarmSwitch = p["AlarmSwitch"];
    if(alarmSwitch)
    {
      Serial.write(OPEN_ALARM, 7);
    }
    else
    {
      Serial.write(CLOSE_ALARM, 7);
    }
}

void send_to_ali()
{
  #if DEBUG
//  Serial.print("AlarmState");
//  Serial.println(CoreInfo.alarmState);
//  Serial.print("AlarmArea");
//  Serial.println(CoreInfo.alarmArea);
//  Serial.print("AlarmSwitch");
//  Serial.println(CoreInfo.alarmSwitch);
//  Serial.print("AliveNum");
//  Serial.println(CoreInfo.aliveNum);
  #else
  device[0]->send("AlarmState", CoreInfo.alarmState);
  if(CoreInfo.alarmArea)
  {
    device[0]->send("AlarmArea", CoreInfo.alarmArea);
  }
  device[0]->send("AlarmSwitch", CoreInfo.alarmSwitch);
  device[0]->send("AliveNum", CoreInfo.aliveNum);
  
  device[0]->send("Light", CoreInfo.light);
  device[0]->send("NodeNum", nodeMax);
  
  #endif
  for(int i = 1; i <= nodeMax; i++)
  {
    if(deviceAlive[i])
    {
      int addr = i-1;
      #if DEBUG
//      Serial.print("Addr");
//      Serial.println(addr+1);
//      Serial.print("CurrentTemperature");
//      Serial.println(nodeInfo[addr].tempH+nodeInfo[addr].tempL*0.1);
//      Serial.print("AlarmState");
//      Serial.println(nodeInfo[addr].alarmState);
//      Serial.print("AQI");
//      Serial.println(nodeInfo[addr].mq2);
      #else
      device[i]->send("CurrentTemperature", nodeInfo[addr].tempH+nodeInfo[addr].tempL*0.1);
      device[i]->send("AlarmState", nodeInfo[addr].alarmState);
      device[i]->send("AQI", nodeInfo[addr].mq2);
      #endif
    }
  }
}











//-----------------------------A-3 函数-----------------//
//3-1管脚初始化
void get_espid(){
   SN = (String )system_get_chip_id();
  }
void LED_Int(){
   pinMode(D4, OUTPUT);
  }
//3-2WIFI初始化
bool wifi_Init(){
  WiFi.begin(config_wifi.stassid, config_wifi.stapsw);
 int retries=30;
 unsigned long start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
  //server.handleClient();
  if (millis() - start_time > 30000) {
     return false;
  }
  ESP.wdtFeed();
  delay(500);
  PIN_Led_State = !PIN_Led_State;
  digitalWrite(PIN_Led, PIN_Led_State);
  }
  return true;
  }
/*
 *3-/2 保存参数到EEPROM
*/
void saveConfig()
{
  EEPROM.begin(1024);
  uint8_t *p = (uint8_t*)(&config_wifi);
  for (int i = 0; i < sizeof(config_wifi); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

/*
 * 从EEPROM加载参数
*/
void loadConfig()
{
  EEPROM.begin(1024);
  uint8_t *p = (uint8_t*)(&config_wifi);
  for (int i = 0; i < sizeof(config_wifi); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();
  //出厂自带
  if (config_wifi.magic != MAGIC_NUMBER)
  {
    strcpy(config_wifi.stassid, DEFAULT_STASSID);
    strcpy(config_wifi.stapsw, DEFAULT_STAPSW);
    config_wifi.magic = MAGIC_NUMBER;
    saveConfig();
    Serial.println("Restore config!");
  }
  Serial.println(" ");
  Serial.println("-----Read config-----");
  Serial.print("stassid:");
  Serial.println(config_wifi.stassid);
  Serial.print("stapsw:");
  Serial.println(config_wifi.stapsw);
  Serial.println("-------------------");
   //  ssid=String(config.stassid);
   //  password=String(config.stapsw);
}


//3-3ESP8266建立无线热点
void SET_AP(){
  // 设置内网
  IPAddress softLocal(192,168,4,1);   // 1 设置内网WIFI IP地址
  IPAddress softGateway(192,168,4,1);
  IPAddress softSubnet(255,255,255,0);
  WiFi.softAPConfig(softLocal, softGateway, softSubnet);
  String apName = ("ESP8266_"+(String)ESP.getChipId());  // 2 设置WIFI名称
  const char *softAPName = apName.c_str();
  WiFi.softAP(softAPName, "admin");      // 3创建wifi  名称 +密码 adminadmin
  IPAddress myIP = WiFi.softAPIP();  // 4输出创建的WIFI IP地址
  }
//3-4ESP建立网页服务器
void Server_int(){
   server.on ("/", handleMain); // 绑定‘/’地址到handleMain方法处理 ----  返回主页面 一键配网页面
   server.on ("/wifi", HTTP_GET, handleWifi); // 绑定‘/wifi’地址到handlePWIFI方法处理  --- 重新配网请求
   server.onNotFound ( handleNotFound ); // NotFound处理
   server.begin();
  }
//3-5-1 网页服务器主页
void handleMain() {
  /* 返回信息给浏览器（状态码，Content-type， 内容）
   * 这里是访问当前设备ip直接返回一个String
   */
  Serial.print("handleMain");
  File file = SPIFFS.open("/index.html", "r");
  size_t sent = server.streamFile(file, "text/html");
  file.close();
  return;
}

//3-5-3 网页修改普通家庭WIFI连接账号密码
/* WIFI更改处理
 * 访问地址为htp://192.162.xxx.xxx/wifi?config=on&name=Testwifi&pwd=123456
  根据wifi进入 WIFI数据处理函数
  根据config的值来进行 on
  根据name的值来进行  wifi名字传输
  根据pwd的值来进行   wifi密码传输
 */
void handleWifi(){
   if(server.hasArg("config")) { // 请求中是否包含有a的参数
    String config = server.arg("config"); // 获得a参数的值
        String wifiname;
        String wifipwd;
    if(config == "on") { // a=on
          if(server.hasArg("name")) { // 请求中是否包含有a的参数
        wifiname = server.arg("name"); // 获得a参数的值
          }
    if(server.hasArg("pwd")) { // 请求中是否包含有a的参数
         wifipwd = server.arg("pwd"); // 获得a参数的值
           }
          String backtxt= "Wifiname: "+ wifiname  +"/r/n wifipwd: "+ wifipwd ;// 用于串口和网页返回信息
          
          server.send ( 200, "text/html", backtxt); // 网页返回给手机提示
           // wifi连接开始
         wifiname.toCharArray(config_wifi.stassid, 50);    // 从网页得到的 WIFI名
         wifipwd.toCharArray(config_wifi.stapsw, 50);  //从网页得到的 WIFI密码
         saveConfig();
         ESP.restart();
          return;
    } else if(config == "off") { // a=off
                server.send ( 200, "text/html", "config  is off!");
        return;
    }
    server.send ( 200, "text/html", "unknown action"); return;
  }
  server.send ( 200, "text/html", "action no found");
  }

//3-5-。。。 网页没有对应请求如何处理
void handleNotFound() {
  String path = server.uri();
  Serial.print("load url:");
  Serial.println(path);
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }
  server.send ( 404, "text/plain", message );
}
// 解析请求的文件
/**
 * 根据文件后缀获取html协议的返回内容类型
 */
String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
