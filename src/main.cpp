#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ACAN2515.h>
#include <spi.h>

#define VERSION 0.06
#define DISPLAY_POWER_PIN 15
#define DISPLAY_BACKLIGHT 38
#define DISPLAY_HIGHT 170
#define DISPLAY_WIDTH 320
#define CAN_INTERRUPT 3
#define CAN_CS 10
#define CAN_MOSI 11
#define CAN_MISO 13
#define CAN_SCK 12
#define CAN_8MHz 8UL * 1000UL * 1000UL
#define CAN_500Kbs 500UL * 1000UL

TFT_eSPI tft = TFT_eSPI();
ACAN2515 can((int)CAN_CS, SPI, (int)CAN_INTERRUPT);

#include "../config/config.h"

//Loops
unsigned long DisplayRefreshLastRun = 0;
const unsigned int DisplayRefreshInterval = 0.25 * 1000; 
unsigned long SendDataLastRun = 0;
const unsigned int SendDataInterval = 30 * 1000; 
unsigned long SerialOutputLastRun = 0;

// CAN values
int Value_ECU_ODO = -1;
int Value_ECU_Speed = -1;
int Value_OBC_RemainingMinutes = -1;
float Value_12VBattery = -1;
int Value_Battery_Temp1 = -1;
int Value_Battery_Temp2 = -1;
float Value_Battery_Volt = -1;
float Value_Battery_Current = -1;
int Value_Battery_SoC = -1;
const char* Value_Display_Gear = "?";
int Value_Display_RemainingDistance = -1;
int Value_Display_Ready = -1;
int Value_Status_Handbrake = -1;

// globals
bool CanInterrupt = false;
bool TransmitValuesChanged = true;
unsigned long CanMessagesProcessed = 0;

// put function declarations here:
void CANCheckMessage();
void CanInterruptISR();
bool WIFIConnect();
bool WIFICheckConnection();
void WIFIDisconnect();
void DisplayCreateUI();
void DisplayRefresh();
void DebugFakeValues();
bool SendDataSimpleAPI();
void SerialPrintValues();

void SetALIVEStatusInidcator(uint16_t color = TFT_RED) { tft.fillCircle(82, 157, 8, color); }
void SetCANStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(155, 157, 8, color); }
void SetWIFIStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(245, 157, 8, color); }
void SetTxStatusInidcator(uint16_t color = TFT_DARKGREY) { tft.fillCircle(308, 157, 8, color); }

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // CAN Module pins
  pinMode(CAN_CS, OUTPUT);
  pinMode(CAN_INTERRUPT, INPUT_PULLUP);

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

  // CAN message filter
  /* Relevant IDs
  010110000001
  010110000010
  010110010011
  010110010100
  010110000000
  011100010011
  011100010100

  010100000000 - Filter
  010101100000 - Mask
  */
  const ACAN2515Mask rxm0 = standard2515Mask (0b010101100000,0,0); // Only ID is relevant for filtering
  const ACAN2515AcceptanceFilter canFilters[] = { 
    {standard2515Filter (0b010100000000,0,0)} // All Ids matching this filter
  };

  // CAN Module
  SetCANStatusInidcator(TFT_BLUE);
  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI);
  ACAN2515Settings CanSettings (CAN_8MHz, CAN_500Kbs);
  CanSettings.mRequestedMode = ACAN2515Settings::ListenOnlyMode ;
  
  int CanError = can.begin(CanSettings, [] { can.isr () ; });

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
  //DebugFakeValues();
}

// Main Loop ===========================================================================================
void loop() {
  unsigned long currentMillis = millis();
  //Serial.println(" - Tick: " + String(currentMillis));
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval){
    DisplayRefreshLastRun = currentMillis;
    DisplayRefresh();
    //DebugFakeValues();
  }

  if (currentMillis - SendDataLastRun >= SendDataInterval && TransmitValuesChanged && Value_ECU_Speed <= 1) // Send only when speed is 0 and values has changed
  {
    SendDataLastRun = currentMillis;
    WIFICheckConnection();
    SendDataSimpleAPI();
  }
  else if (Value_ECU_Speed > 1) { // deactivate tranismitting when driving
    SetWIFIStatusInidcator(TFT_DARKGREY);
    SetTxStatusInidcator(TFT_DARKGREY);
  } 

  if (can.available() || CanInterrupt) {
    SetCANStatusInidcator(TFT_YELLOW);
    CANCheckMessage();
  }
  
  //Serial Output
  if (currentMillis - SerialOutputLastRun >= 5000) {
    SerialOutputLastRun = currentMillis;
 // Print every second
    SerialPrintValues();
  }

  delay(50); //loop delay
}

// put function definitions here: ======================================================================
void CanInterruptISR() {
  CanInterrupt = true;
}

void CANCheckMessage(){
  //Serial.println("CAN check message"); 
  CanInterrupt = false;

  CANMessage canMsg;
  while (can.receive(canMsg)) {
    SetCANStatusInidcator(TFT_GREEN);

    //can.receive(canMsg);
    //Serial.println("CAN Message recived - ID: " + String(canMsg.id, HEX)); 
    //Serial.println("CAN Message recived - DLC: " + String(canMsg.len));
    //Serial.println("CAN Message recived - RTR:" + String(canMsg.rtr));
    //Serial.println("CAN Message recived - Data: " + String(canMsg.data[0], HEX) + " " + String(canMsg.data[1], HEX) + " " + String(canMsg.data[2], HEX) + " " + String(canMsg.data[3], HEX) + " " + String(canMsg.data[4], HEX) + " " + String(canMsg.data[5], HEX) + " " + String(canMsg.data[6], HEX) + " " + String(canMsg.data[7], HEX) + " " + String(canMsg.data[8], HEX));

    if (!canMsg.rtr) {
      CanMessagesProcessed++;
      switch (canMsg.id) {
        // Electronic control Unit (ECU)
        case 0x581: { 
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          // Odo
          unsigned int value1 = canMsg.data[6] << 16 | canMsg.data[5] << 8 | canMsg.data[4];
          //Serial.println("- CAN Value ODO: " + String(value1));
          Value_ECU_ODO = (float)value1 / 10;

          // Speed 
          unsigned int value = canMsg.data[7];
          //Serial.println("- CAN Value Speed: " + String(value));          
          Value_ECU_Speed= value;
          }
          break;

        // Onboard Charger
        case 0x582: { 
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          
          // Remaining Time
          unsigned int value = canMsg.data[1] << 8 | canMsg.data[0];
          //Serial.println("- CAN Value Remaining Time: " + String(value)); 
          Value_OBC_RemainingMinutes = value / 60;
          }
          break;

        // 12V Battery
        case 0x593: {
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          unsigned int value = (canMsg.data[1] << 8) | canMsg.data[0];
          //Serial.println("- CAN Value 12V: " + String(value));
          Value_12VBattery = (float)value / 100;
          }
          break;

        // Main Battery Temperature
        case 0x594: {
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          
          // Temp 1
          int value1 = canMsg.data[0];
          //Serial.println("- CAN Value Temp1: " + String(value1));
          Value_Battery_Temp1 = value1;

          //Temp 2
          int value2 = canMsg.data[3];
          //Serial.println("- CAN Value Temp2: " + String(value2));
          Value_Battery_Temp2 = value2;
          }          
          break;
        
        // Main Battery Voltage and Current
        case 0x580: {
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}

          // Current
          int value1 = (canMsg.data[1] << 8 | canMsg.data[0]);
          if (value1 > 32767) {
            value1 = value1 - 65536; // Convert to signed int
            }
          //Serial.println("- CAN Value Current: " + String(value1));
          Value_Battery_Current = (float)value1 / 10;

          //Voltage
          int value2 = (canMsg.data[3] << 8) | canMsg.data[2];
          //Serial.println("- CAN Value Voltage: " + String(value2));
          if (value2 > 4200 && value2 < 6500) { // Check is value is in valid range
            Value_Battery_Volt = (float)value2 / 100;
          }
        
          // SoC
          unsigned int value3 = canMsg.data[5];
          //Serial.println("- CAN Value SoC: " + String(value3));
          Value_Battery_SoC = value3;
          TransmitValuesChanged = true; // Values changed, so we need to send them
          }
          break;

        // Display
        case 0x713: {
          SetCANStatusInidcator(TFT_BLUE);
          if (!canMsg.len == 7) {Serial.println("CAN: Wrong Lenght"); break;}
          //Serial.println("CAN: Display Message Data:" + String(canMsg.data[0],HEX) + " " + String(canMsg.data[1],HEX) + " " + String(canMsg.data[2],HEX) + " " + String(canMsg.data[3],HEX) + " " + String(canMsg.data[4],HEX) + " " + String(canMsg.data[5],HEX) + " " + String(canMsg.data[6],HEX));

          int value1 = ((canMsg.data[1] & 0x01) << 1) | ((canMsg.data[0] >> 7) & 0x01);
          // Gear
          switch (value1) {
            case 0b00: // 0
              Value_Display_Gear = "R";
              break;
            case 0b01: // 1
              Value_Display_Gear = "N";
              break;
            case 0b10: // 2
              Value_Display_Gear = "D";
              break; 
            case 0b11: // 3
              Value_Display_Gear = "-";
              break; 
            default:
              Value_Display_Gear = "?";
              break;
            }
          //Serial.println("- CAN Value Gear Selected Bits: " + String(value1, BIN));

          // Remaining Distance
          unsigned int value2 = canMsg.data[0] & 0x3F; // Mask to get the lower 6 bits
          //Serial.println("- CAN Value Remaining Distance2: " + String(value2));
          Value_Display_RemainingDistance = value2;
          
          //Ready indicator
          switch ((canMsg.data[2] & 0b00000111)) { // Check the last 3 bits
            case 0b101:
              Value_Display_Ready = 1; // Ready
              break;
            case 0b11:
              Value_Display_Ready = 0; // Not ready
              break;
            default:
              Value_Status_Handbrake = -1; // Default case for safety
              break;
          }
          //Serial.println("- CAN Value Ready: " + String(canMsg.data[2], BIN)); //ready 0b101  not 0b11
          }
          break;

        // Handbreak
        case 0x714: {
          SetCANStatusInidcator(TFT_BLUE);
          //Serial.println("CAN:  Handbreak");
          if (!canMsg.len == 2) {Serial.println("CAN: Wrong Lenght"); break;}

          // Brake
          if (canMsg.data[0] == 0x01) {
            Value_Status_Handbrake = 1;
            }
          else if (canMsg.data[0] == 0x00) {
            Value_Status_Handbrake = 0;
            }
          else {
            Value_Status_Handbrake = -1;
            }
          }
          //Serial.println("- CAN Value Brake: " + String(canMsg.data[0], HEX));

          break;
        }
      }
    else {
      // unknow CAN Message
      SetCANStatusInidcator(TFT_BROWN);
    }
    
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
  tft.drawString("Topolino Info Display " + String(VERSION), 0, 0);
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

  // 51V Temp
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("Temp 1:", 10, 25);
  tft.drawString("Temp 2:", 10, 64);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_Battery_Temp1, 0), 30, 37);
  tft.drawString(String(Value_Battery_Temp2, 0), 30, 76);
  tft.drawCircle(70, 37, 3, TFT_WHITE);
  tft.drawCircle(70, 76, 3, TFT_WHITE);
  tft.drawString("C", 80, 37);
  tft.drawString("C", 80, 76);

  // 12V Spannung
  tft.fillRect(135, 23, 1, 77, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKCYAN);
  tft.drawString("12V Batt:", 140, 25);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(Value_12VBattery, 1) + "V", 150, 37);
  
  // 51V SoC
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
  //Serial.println("Display Refresh"); 
  //Statusleiste
  if (tft.readPixel(82, 157) == 58623) {SetALIVEStatusInidcator(TFT_GREEN);} else {SetALIVEStatusInidcator(TFT_WHITE);}

  tft.setTextSize(3);

  // 51V Temp
  tft.fillRect(0, 36, 133, 24, TFT_BLACK);
  tft.fillRect(0, 75, 133, 24, TFT_BLACK);
  if (Value_Battery_Temp1 > 45) { tft.setTextColor(TFT_RED); }
  else if (Value_Battery_Temp1 < 15) { tft.setTextColor(TFT_YELLOW); }
  else if (Value_Battery_Temp1 < 5) { tft.setTextColor(TFT_BLUE); }
  else { tft.setTextColor(TFT_WHITE); }
  tft.drawString(String(Value_Battery_Temp1, 0), 30, 37);

  if (Value_Battery_Temp2 > 45) { tft.setTextColor(TFT_RED); }
  else if (Value_Battery_Temp2 < 15) { tft.setTextColor(TFT_YELLOW); }
  else if (Value_Battery_Temp2 < 5) { tft.setTextColor(TFT_BLUE); }
  else { tft.setTextColor(TFT_WHITE); }
  tft.drawString(String(Value_Battery_Temp2, 0), 30, 76);
  int positionX = 70;
  if (Value_Battery_Temp1 <= -10 || Value_Battery_Temp2 <= -10) {positionX = 100;}
  tft.drawCircle(positionX, 37, 3, TFT_WHITE);
  tft.drawCircle(positionX, 76, 3, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("C", positionX + 10, 37);
  tft.drawString("C", positionX + 10, 76);

  // 12V Batt
  tft.fillRect(145, 36, 118, 24, TFT_BLACK);
  if (Value_12VBattery < 11.5) { tft.setTextColor(TFT_RED); }
  else if (Value_12VBattery < 12.0) { tft.setTextColor(TFT_YELLOW); }
  else if (Value_12VBattery < 12.5) { tft.setTextColor(TFT_BLUE); }
  else { tft.setTextColor(TFT_WHITE); }
  tft.drawString(String(Value_12VBattery, 1) + "V", 150, 37);

  // SoC
  tft.fillRect(145, 75, 118, 24, TFT_BLACK);
  if (Value_Battery_SoC < 15) { tft.setTextColor(TFT_RED); }
  else if (Value_Battery_SoC < 30) { tft.setTextColor(TFT_YELLOW); }
  else { tft.setTextColor(TFT_WHITE); }
  tft.drawString(String(Value_Battery_SoC) + "%", 160, 76);
  
  // Gear
  tft.fillRect(275, 41, 44, 58, TFT_BLACK);
  tft.setTextSize(7);
  if (Value_Display_Gear == "R") { tft.setTextColor(TFT_RED); }
  else if (Value_Display_Gear == "N") { tft.setTextColor(TFT_YELLOW); }
  else if (Value_Display_Gear == "D") { tft.setTextColor(TFT_GREEN); }
  else { tft.setTextColor(TFT_WHITE); }
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

  tft.setTextSize(3);
  if (Value_Battery_Current > 0) { tft.setTextColor(TFT_GREEN); }
  else if (Value_Battery_Current < -75) { tft.setTextColor(TFT_RED); }
  else { tft.setTextColor(TFT_WHITE); }
  tft.drawString(String(Value_Battery_Current, 1) + " A", 130, 110);
  tft.setTextSize(2);
  if (Value_Battery_Volt < 51.2 || Value_Battery_Volt > 58) { tft.setTextColor(TFT_RED); }
  else { tft.setTextColor(TFT_WHITE); }
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
  TransmitValuesChanged = false;
  SetTxStatusInidcator(TFT_BLUE);

  HTTPClient http;
  String URL = "http://10.0.1.51:8087/set/0_userdata.0.topolino.SoC?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password + "&ack=true&value=" + String(Value_Battery_SoC);
  Serial.print("URL: ");
  Serial.println(URL);

  http.begin(URL);
  http.setConnectTimeout(3 * 1000); //3 Sekunden
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

void SerialPrintValues() {
  Serial.println("Topolino Info Display - Values");
  Serial.println("Can Messages Processed: " + String(CanMessagesProcessed));
  Serial.println(" - ECU ODO: " + String(Value_ECU_ODO) + " km");
  Serial.println(" - ECU Speed: " + String(Value_ECU_Speed) + " km/h");
  Serial.println(" - OBC Remaining Time: " + String(Value_OBC_RemainingMinutes) + " minutes");
  Serial.println(" - 12V Battery: " + String(Value_12VBattery) + " V");
  Serial.println(" - Battery Temp1: " + String(Value_Battery_Temp1) + " °C");
  Serial.println(" - Battery Temp2: " + String(Value_Battery_Temp2) + " °C");
  Serial.println(" - Battery Volt: " + String(Value_Battery_Volt) + " V");
  Serial.println(" - Battery Current: " + String(Value_Battery_Current) + " A");
  Serial.println(" - Battery SoC: " + String(Value_Battery_SoC) + " %");
  Serial.println(" - Display Gear: " + String(Value_Display_Gear) + " (0=R, 1=N, 2=D, 3=-)");
  Serial.println(" - Display Remaining Distance: " + String(Value_Display_RemainingDistance) + " km");
  Serial.println(" - Display Ready: " + String(Value_Display_Ready) + " (1=Ready, 0=Not Ready, -1=Unknown)");
  Serial.println(" - Status Handreake: " + String(Value_Status_Handbrake) + " (1=On, 0=Off, -1=Unknown)");
  Serial.println("=============================");
}