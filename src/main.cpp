#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ACAN2515.h>
#include <spi.h>

#define DISPLAY_POWER_PIN 15
#define DISPLAY_BACKLIGHT 38
#define DISPLAY_HIGHT 170
#define DISPLAY_WIDTH 320
#define CAN_INTERRUPT 3
#define CAN_CS 10
#define CAN_MOSI 11
#define CAN_MISO 13
#define CAN_SCK 12
#define CAN_16MHz 16UL * 1000UL * 1000UL
#define CAN_500Kbs 500UL * 1000UL

TFT_eSPI tft = TFT_eSPI();
ACAN2515 can((int)CAN_CS, SPI, (int)CAN_INTERRUPT);

#include "../config/config.h"

//Loops
unsigned long DisplayRefreshLastRun = 0;
const long DisplayRefreshInterval = 500; //Milliseconds
unsigned long SendDataLastRun = 0;
const long SendDataInterval = 10000; //Milliseconds

// CAN values
int Value_ECU_ODO = -1;
int Value_ECU_Speed = -1;
int Value_OBC_RemainingTime = -1;
float Value_12VBattery = -1;
float Value_Battery_Temp1 = -1;
float Value_Battery_Temp2 = -1;
float Value_Battery_Volt = -1;
float Value_Battery_Current = -1;
int Value_Battery_SoC = -1;
char* Value_Display_Gear = "?";
int Value_Display_RemainingDistance = -1;
int Value_Display_Break = -1;
int Value_Display_Ready = -1;
int Value_Status_Brake = -1;

// globals
bool CanInterrupt = false;

// put function declarations here:
void CANCheckMessage();
void IRAM_ATTR CanInterruptISR();
bool WIFIConnect();
bool WIFICheckConnection();
void WIFIDisconnect();
void DisplayCreateUI();
void DisplayRefresh();
void DebugFakeValues();
bool SendDataSimpleAPI();

void SetALIVEStatusInidcator(uint16_t color = TFT_RED) { tft.fillCircle(82, 157, 8, color); }
void SetCANStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(155, 157, 8, color); }
void SetWIFIStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(245, 157, 8, color); }
void SetTxStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(308, 157, 8, color); }

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // Turn on display power
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  pinMode(DISPLAY_BACKLIGHT, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);
  digitalWrite(DISPLAY_BACKLIGHT, HIGH);
  delay(500);

  // Initialize display
  tft.init();
  tft.setRotation(7);
  tft.fillScreen(TFT_BLACK);
  
  DisplayCreateUI();

  // Text Defaults
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);

  delay(1000);

  // CAN Module
  SetCANStatusInidcator(TFT_BLUE);
  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI);
  ACAN2515Settings CanSettings (CAN_16MHz, CAN_500Kbs);
  CanSettings.mRequestedMode = ACAN2515Settings::ListenOnlyMode ;
  
  int CanError = can.begin(CanSettings, CanInterruptISR);

  if ( CanError == 0) {
    Serial.println("CAN-Module Initialized Successfully!");
    SetCANStatusInidcator(TFT_DARKGREY);
    } 
  else {
    Serial.println("ERROR: Initializing CAN-Module failed! ErrCode: " + String(CanError));
    SetCANStatusInidcator(TFT_RED);
    }

  // WIFI
  WIFIConnect();

  // Debug
  // DebugFakeValues();
}

// Main Loop ===========================================================================================
void loop() {
  unsigned long currentMillis = millis();
  Serial.println(" - Tick: " + String(currentMillis));
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval){
    DisplayRefreshLastRun = currentMillis;
    DisplayRefresh();
    WIFICheckConnection();
    //DebugFakeValues();
  }

  if (currentMillis - SendDataLastRun >= SendDataInterval){
    SendDataLastRun = currentMillis;
    SendDataSimpleAPI();
  }

  if (CanInterrupt) {
    CANCheckMessage();
  }
  

  delay(250); //loop delay
}

// put function definitions here: ======================================================================
void IRAM_ATTR CanInterruptISR() {
  CanInterrupt = true;
  //SetCANStatusInidcator(TFT_YELLOW);
}

void CANCheckMessage(){
  Serial.println("CAN check message"); 
  CanInterrupt = false;
  
  CANMessage canMsg;
  if (can.available()) {
    SetCANStatusInidcator(TFT_GREEN);
    can.receive(canMsg);
    Serial.println("CAN Message recived - ID: " + String(canMsg.id, HEX)); 
    Serial.println("CAN Message recived - DLC: " + String(canMsg.len));
    Serial.println("CAN Message recived - RTR:" + String(canMsg.rtr));
    Serial.println("CAN Message recived - Data: " + String(canMsg.data[0], HEX) + " " + String(canMsg.data[1], HEX) + " " + String(canMsg.data[2], HEX) + " " + String(canMsg.data[3], HEX) + " " + String(canMsg.data[4], HEX) + " " + String(canMsg.data[5], HEX) + " " + String(canMsg.data[6], HEX) + " " + String(canMsg.data[7], HEX) + " " + String(canMsg.data[8], HEX));

    if (!canMsg.rtr) {
      switch (canMsg.id) {
        case 0x581:
          Serial.println("CAN: ECU");
          break;
        case 0x582:
          Serial.println("CAN: OBC");
          break;
        case 0x593: {
          Serial.println("CAN: 12V Batt");
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          unsigned int value = (canMsg.data[1] << 8) + canMsg.data[0];
          Serial.println("CAN Value: " + String(value));
          Value_12VBattery = (unsigned int)canMsg.data16 * 0.01;
          }
          break;
        case 0x594:
          Serial.println("CAN: MainBatt Temp");
          break;
        case 0x580:
          Serial.println("CAN: MainBatt Voltage");
          break;
        case 0x713:
          Serial.println("CAN: Display");
          break;
        case 0x714:
          Serial.println("CAN: Break");
          break;
      }
    }
    else {
      SetCANStatusInidcator(TFT_BROWN);
    }
    
  }
  else {
    SetCANStatusInidcator(TFT_CYAN);
  }
}

bool WIFIConnect(){
  Serial.print("WIFI Connect to SSID: "); 
  Serial.println(YourWIFI_SSID);
  
  // Check if already connected
  if (WIFICheckConnection()) {return true; }
  
  SetWIFIStatusInidcator(TFT_BLUE);
  // Activate WIFI
  WiFi.setHostname("TopolinoInfoDisplay");
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(YourWIFI_SSID, YourWIFI_Passphrase);  

  int i = 0;
  //Wait for connect
  while (WiFi.status () != WL_CONNECTED) {
    delay(1000);
    Serial.print("WIFI Connecting....");
    Serial.println(WiFi.status());
    i++;
    if ( i > 5) { break; }
  }

  return WIFICheckConnection();
}

bool WIFICheckConnection() { 
  if (WiFi.status () == WL_CONNECTED) {
    Serial.print("WIFI Connected! IP: ");
    Serial.println(WiFi.localIP());
    SetWIFIStatusInidcator(TFT_GREEN);
    return true;
  }
  else {
    Serial.print("WIFI NOT connected! Status: ");
    Serial.println(WiFi.status());
    SetWIFIStatusInidcator(TFT_RED);
    return false;
  }
}

void WIFIDisconnect(){
  Serial.println("WIFI disconnect");
  WiFi.disconnect();
  SetWIFIStatusInidcator(TFT_DARKGREY);
}

void DisplayCreateUI(){
  Serial.println("Disaply Create UI");
  // Titel
  tft.setTextColor(TFT_DARKCYAN);
  tft.setTextSize(2);
  tft.drawString("Topolino Info Display vDEV", 0, 0);
  tft.fillRectHGradient(0, 20, 320, 3, TFT_WHITE, TFT_RED);

  // Statusleiste
  tft.fillRectHGradient(0, 142, 320, 1, TFT_WHITE, TFT_RED);
  tft.drawString("Alive:", 0, 150);
  SetALIVEStatusInidcator();
  tft.drawString("CAN:", 100, 150);
  SetCANStatusInidcator();
  tft.drawString("WIFI:", 175, 150);
  SetWIFIStatusInidcator();
  tft.drawString("Tx:", 263, 150);
  SetTxStatusInidcator();
  
  //Resthöhe:  21 bis 141 = 120px

  // 45V Temp
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("Temp 1:", 0, 25);
  tft.drawString("Temp 2:", 0, 64);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_Battery_Temp1, 1) + " C", 20, 37);
  tft.drawString(String(Value_Battery_Temp2, 1) + " C", 20, 76);

  // 12V Spannung
  tft.fillRect(135, 23, 1, 77, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("12V Batt:", 140, 25);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_12VBattery, 1) + "V", 150, 37);
  
  // 45V SoC
  tft.fillRect(135, 61, 130, 1, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("SoC:", 140, 64);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_Battery_SoC) + "%", 160, 76);

  //Ganganzeige
  tft.fillRect(265, 23, 1, 77, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("Gear:", 270, 25);
  tft.setTextSize(7);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(Value_Display_Gear, 275, 42);

  // Stromabnahme
  tft.fillRect(0, 100, 320, 1, TFT_DARKGREY);
  tft.fillRect(110, 100, 2, 42, TFT_WHITE);
  tft.fillRectHGradient(10, 106, 100, 30, TFT_DARKGREEN, TFT_GREEN);
  tft.fillRectHGradient(112, 106, 200, 30, TFT_YELLOW, TFT_RED);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_Battery_Current, 1) + " A", 130, 110);
  tft.setTextSize(2);
  tft.drawString(String(Value_Battery_Volt, 1) + "V", 20, 115);
}

void DisplayRefresh(){
  Serial.println("Display Refresh"); 
  //Statusleiste
  if (tft.readPixel(82, 157) == 58623) {SetALIVEStatusInidcator(TFT_GREEN);} else {SetALIVEStatusInidcator(TFT_WHITE);}

  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);

  // 45V Temp
  tft.fillRect(0, 36, 133, 24, TFT_BLACK);
  tft.fillRect(0, 75, 133, 24, TFT_BLACK);
  tft.drawString(String(Value_Battery_Temp1, 1) + " C", 20, 37);
  tft.drawString(String(Value_Battery_Temp2, 1) + " C", 20, 76);

  // 12V Batt
  tft.fillRect(145, 36, 118, 24, TFT_BLACK);
  tft.drawString(String(Value_12VBattery, 1) + "V", 150, 37);

  // SoC
  tft.fillRect(145, 75, 118, 24, TFT_BLACK);
  tft.drawString(String(Value_Battery_SoC) + "%", 160, 76);
  
  // Gear
  tft.fillRect(275, 41, 44, 58, TFT_BLACK);
  tft.setTextSize(7);
  tft.drawString(Value_Display_Gear, 275, 42);

  // Stromverbrauch
  int gradientLenght = 0;
  tft.fillRect(10, 106, 100, 30, TFT_BLACK);
  tft.fillRect(112, 106, 200, 30, TFT_BLACK);
  if (Value_Battery_Current > 0) {
    gradientLenght = (int)Value_Battery_Current;
    if (gradientLenght > 100) {gradientLenght = 100;}
    tft.fillRectHGradient(110-gradientLenght, 106, gradientLenght, 30, TFT_DARKGREEN, TFT_GREEN);
  }
  else if (Value_Battery_Current < 0)  {
    gradientLenght = (int)Value_Battery_Current * -1;
    if (gradientLenght > 200) {gradientLenght = 200;}
    tft.fillRectHGradient(112, 106, gradientLenght, 30, TFT_YELLOW, TFT_RED);
  }

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.drawString(String(Value_Battery_Current, 1) + " A", 130, 110);
  tft.setTextSize(2);
  tft.drawString(String(Value_Battery_Volt, 1) + "V", 20, 115); 
}

void DebugFakeValues() {
  Value_12VBattery = random(0, 150) / 10;
  Value_Battery_SoC = random(0, 100);
  Value_Display_Gear = "D";
  Value_Battery_Temp1 = random(-90, 450) / 10;
  Value_Battery_Temp2 = random(-90, 450) / 10;
  Value_Battery_Volt = random(210, 560) / 10;
  Value_Battery_Current = random(-2050, 1050) / 10;
}

bool SendDataSimpleAPI() {
  Serial.println("Send Data via REST");
  SetTxStatusInidcator(TFT_BLUE);

  HTTPClient http;
  String URL = "http://10.0.1.51:8087/set/0_userdata.0.topolino.SoC?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password + "&ack=true&value=" + String(Value_Battery_SoC);
  Serial.print("URL: ");
  Serial.println(URL);

  http.begin(URL);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    SetTxStatusInidcator(TFT_GREEN);
    return true;
  }
  else {
    SetTxStatusInidcator(TFT_RED);
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
    return false;
  }

  http.end();
}

