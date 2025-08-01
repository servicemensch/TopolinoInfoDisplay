#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <spi.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h>
#include <UrlEncode.h>
#include <ACAN2515.h>

//#define DEBUG

const char VERSION[] = "0.66";
#define DISPLAY_POWER_PIN 22
#define CAN_INTERRUPT 27
#define CAN_CS 33
#define CAN_MOSI 13
#define CAN_MISO 12
#define CAN_SCK 14
#define CAN_8MHz 8UL * 1000UL * 1000UL
#define CAN_500Kbs 500UL * 1000UL
#define ONBOARD_LED 2

#define COLOR_ALMOSTBLACK 0x31a6
#define COLOR_BACKGROUND 0x2104
#define COLOR_BG_GREEN 0x0140
#define COLOR_BG_RED 0x3000
#define COLOR_TOPOLINO 0x07fc
#define COLOR_GREY 0xa554

struct trip {
  unsigned long startTime = 0;
  unsigned long endTime;
  float startKM = 0;
  float endKM;
  float maxSpeed = 0;
  int startSoC = 0;
  int endSoC = 0;
};
struct CANValues {
  int ODO = 0;
  bool ODOUp = false;
  int Speed = 0;
  bool SpeedUp = false;
  int OBCRemainingMinutes = -1;
  bool OBCRemainingMinutesUp = false;
  float Battery = 0.0;
  bool BatteryUp = false;
  int Temp1 = -99;
  bool Temp1Up = false;
  int Temp2 = -99;
  bool Temp2Up = false;
  float Volt = 0;
  bool VoltUp = false;
  int SoC = 101;
  bool SoCUp = false;
  String Gear = "?";
  bool GearUp = false;
  int RemainingDistance = 0;
  bool RemainingDistanceUp = false;
  int Ready = -1; // 1=Ready, 0=Not Ready, -1=Unknown
  bool ReadyUp = false;
  float Current = 0;
  bool CurrentUp = false;
  int Handbrake = -1; // 1=On, 0=Off, -1=Unknown
  bool HandbrakeUp = false;
};
struct Charge {
  unsigned long startTime = 0;
  unsigned long endTime;
  int startSoC = 0;
  int endSoC = 0;
  int helperCircal = 0;
};

TFT_eSPI tft = TFT_eSPI();
SPIClass hspi = SPIClass(HSPI);
ACAN2515 can((int)CAN_CS, hspi, (int)CAN_INTERRUPT);

#include "../config/config.h"

//Loops
unsigned long DisplayRefreshLastRun = 0;
const unsigned int DisplayRefreshInterval = 0.25 * 1000; 
unsigned long SendDataLastRun = 0;
const unsigned int SendDataInterval = 30 * 1000; 
unsigned long SerialOutputLastRun = 0;
const unsigned int TripRecordInterval = 1 * 1000;
unsigned long TripRecordingLastRun = 0;
unsigned long KeepAliveLastRun = 0;

// Deep Sleep Timeout
#define DEEP_SLEEP_TIMEOUT 10 * 60 * 1000 // 10 minutes in milliseconds

// Globals
int CanError = 0;
unsigned long CanMessagesProcessed = 0;
unsigned long CanMessagesLastRecived = 0;
float Value_Battery_Current_Buffer = 0;
trip Trip; // Trip data structure
CANValues canValues; // CAN values structure
Charge thisCharge;
bool TripActive = false;
bool DataToSend = false;
unsigned long StatusIndicatorStatus = TFT_DARKGREY;
unsigned long StatusIndicatorCAN = TFT_DARKGREY;
unsigned long StatusIndicatorWIFI = TFT_DARKGREY;
unsigned long StatusIndicatorTx = TFT_DARKGREY;
bool IsSleeping = false;
bool IsCharging = false;

// put function declarations here:
void CanConnect();
void CANCheckMessage();
bool WIFIConnect();
bool WIFICheckConnection();
void WIFIDisconnect();
void DisplayBoot();
void DisplayMainUI();
void DisplayTripResults();
void DisplayCharging();
void DisplayChargingResult();
void ConnectWIFIAndSendData();
bool SendDataSimpleAPI();
void SendRemoteLogSimpleAPI(String message);
void SerialPrintValues();
void TripRecording();
void SleepLightStart();
void SleepDeepStart();
float average(float newvalue, float &buffer, float factor);
void DebugFakeValues();

// =====================================================================================================
// Make everything ready ===============================================================================
// =====================================================================================================
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.flush();
  
  //Set Pins
  pinMode(ONBOARD_LED, OUTPUT); // Set the built-in LED pin as output
  pinMode(CAN_CS, OUTPUT);
  pinMode(CAN_INTERRUPT, INPUT_PULLUP);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);

  //Deep Sleep config
  //esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIMEOUT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)CAN_INTERRUPT, 0); // Wake up on CAN interrupt

  digitalWrite(ONBOARD_LED, HIGH);

  // Turn on display power
  digitalWrite(DISPLAY_POWER_PIN, HIGH);
  delay(50);
  // Initialize display
  tft.init();
  tft.fillScreen(TFT_BLACK);
  delay(500);

  // Initiate CAN
  CanConnect(); 
  delay(500);
  CanMessagesLastRecived = millis();

  // Show Welcome Screen
  DisplayBoot();
  tft.fillScreen(COLOR_BACKGROUND);

  // Connect WIFI to enable OTA update at boot
  WIFIConnect();
  
  // Over The Air update config
  ArduinoOTA.setHostname("TopolinoInfoDisplayOTA");
  ArduinoOTA.begin();
  
  SendRemoteLogSimpleAPI("TopolinoInfoDisplay started - Version: " + String(VERSION));

  digitalWrite(ONBOARD_LED, LOW);

#ifdef DEBUG
  // Debug
  canValues.OBCRemainingMinutes = 0;
  canValues.SoC = 50;
  thisCharge.startSoC = 30;
  thisCharge.startTime = millis();
  canValues.Current = 12.3;
  Serial.println(canValues.SoC);
  Serial.println (thisCharge.startSoC);
  Serial.println(canValues.Current);
  delay(10000);
  DisplayCharging();
  delay(60000);
#endif
}

// =====================================================================================================
// Main Loop ===========================================================================================
// =====================================================================================================
void loop() {
  unsigned long currentMillis = millis();
  //Serial.println(" - Tick: " + String(currentMillis));

  // Be Alive status
  if (currentMillis - KeepAliveLastRun >= 1000) {
    KeepAliveLastRun = currentMillis;
    digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));
    if (StatusIndicatorStatus == TFT_WHITE) {
      if (TripActive) { StatusIndicatorStatus = TFT_DARKCYAN; }
      else { StatusIndicatorStatus = TFT_GREEN; }
    }
    else {StatusIndicatorStatus = TFT_WHITE;}
  }

  // Display Main UI / refresh Values
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval && !IsSleeping && !IsCharging){
    DisplayRefreshLastRun = currentMillis;
    DisplayMainUI();
    #ifdef DEBUG
    DebugFakeValues();
    #endif
  }

  // Check CAN Messages
  if (can.available()) {
    StatusIndicatorCAN = TFT_YELLOW;
    CanMessagesLastRecived = currentMillis;
    CANCheckMessage();
  }
  else if (CanError) {
    Serial.println("CAN Connect...");
    CanConnect();
  }
  else if ( CanMessagesLastRecived - currentMillis > 5000) { // If no CAN messages received for 5 seconds, set indicator to grey
    StatusIndicatorCAN =  TFT_DARKGREY;
  }

  // Check for new trip recording
  if (canValues.Ready == 1 && canValues.Speed > 0 && !TripActive) {
    TripActive = true;
    Serial.println("Trip started at " + String(Trip.startTime) + " with ODO: " + String(Trip.startKM));
    // Reset Trip data
    Trip.startTime = 0;
    Trip.endTime = 0;
    Trip.startKM = 0;
    Trip.endKM = 0;
    Trip.maxSpeed = 0;
    Trip.startSoC = canValues.SoC;
    Trip.endSoC = canValues.SoC;
  } 
  if (currentMillis - TripRecordingLastRun >= TripRecordInterval && TripActive) {
    TripRecordingLastRun = currentMillis;
    TripRecording();
  }

  // Check current Trip has ended
  if ((canValues.Ready == 0 || (canValues.Gear == "N" && canValues.Handbrake && canValues.Speed == 0) ) && TripActive) {
    TripActive = false;
    
    Serial.println("Trip Duration: " + String((Trip.endTime - Trip.startTime) / 1000 / 60) + " minutes");
    Serial.println("Trip Distance: " + String(Trip.endKM - Trip.startKM / 10, 1) + " km");
    Serial.println("Trip Max Speed: " + String(Trip.maxSpeed) + " km/h");
    Serial.println("Trip average Speed: " + String((float)(Trip.endKM - Trip.startKM) / ((Trip.endTime - Trip.startTime) / 1000 / 60)) + " km/h");
    Serial.println("Trip average Energy: " + String((float)(Trip.startSoC - Trip.endSoC) * 0,056 ) + " kWh");
    
    // Show Trip results
    DisplayTripResults();
    //Send Data while showing Results
    ConnectWIFIAndSendData();
    
    delay(20000); // Show Trip results for 15 seconds
    tft.fillScreen(COLOR_BACKGROUND);
    DisplayMainUI();
  }

  // Check if new charge
  if (canValues.OBCRemainingMinutes >= 0 && canValues.Current > 0 && !IsCharging) {
    IsCharging = true;
    thisCharge.startSoC = canValues.SoC;
    thisCharge.startTime = currentMillis;
    tft.fillScreen(COLOR_BACKGROUND);
  }
  if (IsCharging && canValues.OBCRemainingMinutes == -1) {
    IsCharging = false;
    thisCharge.endSoC = canValues.SoC;
    thisCharge.endTime = currentMillis;

    // Show Charge end screen
    DisplayChargingResult();
    ConnectWIFIAndSendData();
    delay(60000);
    tft.fillScreen(COLOR_BACKGROUND);
  }

  // Is currently Charging  
  if (IsCharging && currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval)
  {
    DisplayRefreshLastRun = currentMillis;
    DisplayCharging();
  }

  // Send Data  
  if (DataToSend && (currentMillis - SendDataLastRun >= SendDataInterval) && (canValues.Speed < 1 && canValues.Handbrake)) //send in interval if ignition is off
  {
    SendDataLastRun = currentMillis;
    ConnectWIFIAndSendData();
  }
  else if (canValues.Speed > 1 && WiFi.status() != WL_NO_SHIELD)  { // deactivate tranismitting when driving
    #ifdef DEBUG
    Serial.println("WIFI Stratus: " + String(WiFi.status()));
    #endif
    WIFIDisconnect();
    StatusIndicatorWIFI = TFT_DARKGREY;
    StatusIndicatorTx = TFT_DARKGREY;
  } 

  //Serial Output
  /*
  if (currentMillis - SerialOutputLastRun >= 5000) {
    SerialOutputLastRun = currentMillis;
    SerialPrintValues();
  }
  */

  //Check OTA Updates
  ArduinoOTA.handle();

  // Sleep modes
  if ((currentMillis - CanMessagesLastRecived) > (DEEP_SLEEP_TIMEOUT / 2) && !IsSleeping) {  
    SleepLightStart();
  }
  if ((currentMillis - CanMessagesLastRecived) > DEEP_SLEEP_TIMEOUT) {  
    SleepDeepStart();
  }


  delay(25); //loop delay
}

// Functions ===========================================================================================
  
void CanConnect () {
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
  StatusIndicatorCAN = TFT_BLUE;
  hspi.begin(CAN_SCK, CAN_MISO, CAN_MOSI);
  ACAN2515Settings CanSettings (CAN_8MHz, CAN_500Kbs);
  CanSettings.mRequestedMode = ACAN2515Settings::ListenOnlyMode ;
  
  CanError = can.begin(CanSettings, [] { can.isr () ; });

  if ( CanError == 0) {
    Serial.println("CAN-Module Initialized Successfully!");
    StatusIndicatorCAN = TFT_LIGHTGREY;
    } 
  else {
    Serial.println("ERROR: Initializing CAN-Module failed! ErrCode: " + String(CanError));
    StatusIndicatorCAN = TFT_RED;
    }
  CanMessagesLastRecived = millis();
}

void CANCheckMessage(){
  //Serial.println("CAN check message"); 

  CANMessage canMsg;
  while (can.receive(canMsg)) {
    StatusIndicatorCAN = TFT_GREEN;

    //can.receive(canMsg);
    //Serial.println("CAN Message recived - ID: " + String(canMsg.id, HEX)); 
    //Serial.println("CAN Message recived - DLC: " + String(canMsg.len));
    //Serial.println("CAN Message recived - RTR:" + String(canMsg.rtr));
    //Serial.println("CAN Message recived - Data: " + String(canMsg.data[0], HEX) + " " + String(canMsg.data[1], HEX) + " " + String(canMsg.data[2], HEX) + " " + String(canMsg.data[3], HEX) + " " + String(canMsg.data[4], HEX) + " " + String(canMsg.data[5], HEX) + " " + String(canMsg.data[6], HEX) + " " + String(canMsg.data[7], HEX) + " " + String(canMsg.data[8], HEX));

    if (!canMsg.rtr) {
      CanMessagesProcessed++;
      IsSleeping = false;
      switch (canMsg.id) {
        // Electronic control Unit (ECU)
        case 0x581: { 
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          // Odo
          unsigned int value1 = canMsg.data[6] << 16 | canMsg.data[5] << 8 | canMsg.data[4];
          //Serial.println("- CAN Value ODO: " + String(value1));
          canValues.ODO = (float)value1;
          canValues.ODOUp = true;

          // Speed 
          unsigned int value = canMsg.data[7];
          //Serial.println("- CAN Value Speed: " + String(value));          
          canValues.Speed = value;
          canValues.SpeedUp = true;
          }
          break;

        // Onboard Charger
        case 0x582: { 
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          
          // Remaining Time
          unsigned int value = canMsg.data[1] << 8 | canMsg.data[0];
          //Serial.println("- CAN Value Remaining Time: " + String(value)); 
          canValues.OBCRemainingMinutes = value / 60;
          if (canValues.OBCRemainingMinutes > 600) { canValues.OBCRemainingMinutes = -1; } // not charging
          canValues.OBCRemainingMinutesUp = true;
          }
          break;

        // 12V Battery
        case 0x593: {
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          unsigned int value = (canMsg.data[1] << 8) | canMsg.data[0];
          //Serial.println("- CAN Value 12V: " + String(value));
          canValues.Battery = (float)value / 100;
          canValues.BatteryUp = true;
          }
          break;

        // Main Battery Temperature
        case 0x594: {
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          
          // Temp 1
          int value1 = canMsg.data[0];
          //Serial.println("- CAN Value Temp1: " + String(value1));
          canValues.Temp1 = value1;
          canValues.Temp1Up = true;

          //Temp 2
          int value2 = canMsg.data[3];
          //Serial.println("- CAN Value Temp2: " + String(value2));
          canValues.Temp2 = value2;
          canValues.Temp2Up = true;
          }          
          break;
        
        // Main Battery Voltage and Current
        case 0x580: {
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}

          // Current
          int value1 = (canMsg.data[1] << 8 | canMsg.data[0]);
          if (value1 > 32767) {
            value1 = value1 - 65536; // Convert to signed int
            }
          //Serial.println("- CAN Value Current: " + String(value1));
         canValues.Current = average((float)value1 / 10, Value_Battery_Current_Buffer, 3); // Average over 3 values 
         canValues.CurrentUp = true;

          //Voltage
          int value2 = (canMsg.data[3] << 8) | canMsg.data[2];
          //Serial.println("- CAN Value Voltage: " + String(value2));
          if (value2 > 4200 && value2 < 6500) { // Check is value is in valid range
            canValues.Volt = (float)value2 / 100;
            canValues.VoltUp = true;
          }
        
          // SoC
          unsigned int value3 = canMsg.data[5];
          //Serial.println("- CAN Value SoC: " + String(value3));
          canValues.SoC = value3;
          canValues.SoCUp = true;
          }
          break;

        // Display
        case 0x713: {
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 7) {Serial.println("CAN: Wrong Lenght"); break;}
          //Serial.println("CAN: Display Message Data:" + String(canMsg.data[0],HEX) + " " + String(canMsg.data[1],HEX) + " " + String(canMsg.data[2],HEX) + " " + String(canMsg.data[3],HEX) + " " + String(canMsg.data[4],HEX) + " " + String(canMsg.data[5],HEX) + " " + String(canMsg.data[6],HEX));

          int value1 = ((canMsg.data[1] & 0x01) << 1) | ((canMsg.data[0] >> 7) & 0x01);
          // Gear
          switch (value1) {
            case 0b00: // 0
              canValues.Gear = "R";
              break;
            case 0b01: // 1
              canValues.Gear = "N";
              break;
            case 0b10: // 2
              canValues.Gear = "D";
              break; 
            case 0b11: // 3
              canValues.Gear = "-";
              break; 
            default:
              canValues.Gear = "?";
              break;
            }
            canValues.GearUp = true;
          //Serial.println("- CAN Value Gear Selected Bits: " + String(value1, BIN));

          // Remaining Distance
          unsigned int value2 = canMsg.data[0] & 0x3F; // Mask to get the lower 6 bits
          //Serial.println("- CAN Value Remaining Distance2: " + String(value2));
          canValues.RemainingDistance = value2;
          canValues.RemainingDistanceUp = true; 
          
          //Ready indicator
          switch ((canMsg.data[2] & 0b00000111)) { // Check the last 3 bits
            case 0b101:
              canValues.Ready = 1; // Ready
              break;
            case 0b11:
              canValues.Ready = 0; // Not ready
              break;
            default:
              canValues.Ready = -1; // Default case for safety
              break;
          }
          canValues.ReadyUp = true;
          //Serial.println("- CAN Value Ready: " + String(canMsg.data[2], BIN)); //ready 0b101  not 0b11
          }
          break;

        // Handbreak
        case 0x714: {
          StatusIndicatorCAN = TFT_BLUE;
          //Serial.println("CAN:  Handbreak");
          if (!canMsg.len == 2) {Serial.println("CAN: Wrong Lenght"); break;}

          // Brake
          if (canMsg.data[0] == 0x01) {
            canValues.Handbrake = 1;
            }
          else if (canMsg.data[0] == 0x00) {
            canValues.Handbrake = 0;
            }
          else {
            canValues.Handbrake = -1;
            }
          }
          canValues.HandbrakeUp = true;
          //Serial.println("- CAN Value Brake: " + String(canMsg.data[0], HEX));

          break;
        }
        if (StatusIndicatorCAN == TFT_BLUE) {DataToSend = true; StatusIndicatorTx = TFT_YELLOW; } // knonw CAN message has changed values
      }
    else {
      // unknow CAN Message
      StatusIndicatorCAN = TFT_BROWN;
    }
    
  }
}

bool WIFIConnect(){
  Serial.print("WIFI Connect to SSID: "); 
  Serial.println(YourWIFI_SSID);
  
  // Check if already connected
  if (WIFICheckConnection()) {return true; }
  
  StatusIndicatorWIFI = TFT_BLUE;
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
    StatusIndicatorWIFI = TFT_GREEN;
    return true;
  }
  else {
    Serial.print("WIFI NOT connected! Status: ");
    Serial.println(WiFi.status());
    StatusIndicatorWIFI = TFT_RED;
    return false;
  }
}

void WIFIDisconnect(){
#ifdef DEBUG
  Serial.println("WIFI disconnect");
#endif
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  StatusIndicatorWIFI = TFT_DARKGREY;
}

void DisplayBoot() {
  tft.fillRectHGradient(0, 0, 240, 240, 0x038d, COLOR_TOPOLINO);
  delay(500);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.drawString("Topolino",30,70);
  delay(500);
  tft.drawString("Info",115,100);
  delay(500);
  tft.drawString("Display",90,130);
  delay(500);
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("Version: " + String(VERSION), 40, 160);
  #ifdef DEBUG
    tft.drawString("DEBUG", 75, 180);
  #endif
  delay(3000);
}

void DisplayMainUI() {
  //tft.fillScreen(COLOR_BACKGROUND);
  int valuecolor;
  
  //Consumption Background
  //tft.drawSmoothArc(120,120, 121, 105, 150, 240, COLOR_BG_RED, COLOR_BACKGROUND, true);
  //tft.drawSmoothArc(120,120, 121, 105, 120, 150, COLOR_BG_GREEN, COLOR_BACKGROUND, true);
  tft.drawSmoothArc(120, 120, 121, 105, 120, 240, TFT_DARKGREY, COLOR_BACKGROUND, true);

  //Consumption Arc
  if (canValues.Current < 0) {
    //Consumption negative
    int arcLenght = map(canValues.Current, -150, 0, 240, 150);
    if (arcLenght > 240) {arcLenght = 240;}
    if (arcLenght < 151) {arcLenght = 151;}
    if (canValues.Current < -75) {valuecolor = TFT_RED;} else {valuecolor = TFT_YELLOW;}
    tft.drawSmoothArc(120,120, 120, 105, 150, arcLenght, valuecolor, COLOR_BACKGROUND, true);
  }
  else if (canValues.Current > 0) {
    //Consumption positive
    int arcLenght = map(canValues.Current, 0, 75, 0, 30);
    if (arcLenght > 30) {arcLenght = 30;}
    if (arcLenght < 1) {arcLenght = 1;}
    tft.drawSmoothArc(120,120, 120, 105, 150 - arcLenght, 150, TFT_GREEN, COLOR_BACKGROUND, true);
  }
  else {
    // No Consumption
    tft.drawSmoothArc(120,120, 118, 107, 149, 150, TFT_BLUE, COLOR_BACKGROUND, true);

  }
  
  // Consumption value  
  tft.setTextColor(TFT_WHITE, COLOR_ALMOSTBLACK, true);
  tft.setTextSize(3);
  int textstart = 55;
  if (canValues.Current >=10) { textstart = 95;}
  else if (canValues.Current >= 0) { textstart = 110;}
  else if (canValues.Current < -100 ) { textstart = 60;}
  else if (canValues.Current < -10) { textstart = 75;}
  else if (canValues.Current < 0) { textstart = 95;}
  tft.fillSmoothRoundRect(40, 65, 160, 60, 10, COLOR_ALMOSTBLACK, COLOR_BACKGROUND); // Reset backgroubnd
  tft.drawString(String(canValues.Current,1) + "A", textstart, 85);

  // Battery Temperature
  float tempAverage = (canValues.Temp1 + canValues.Temp2) / 2.0;
  int arcLenght = map(tempAverage, -10, 50, 45, 90);
  if (arcLenght < 46) { arcLenght = 46;}
  if (arcLenght > 90) { arcLenght = 90;}
  if (tempAverage > 45) { valuecolor = TFT_RED; }
  else if (tempAverage < 5) { valuecolor = TFT_BLUE; }
  else if (tempAverage < 20) { valuecolor = TFT_YELLOW; }
  else { valuecolor = 0x2520; }
  tft.setTextSize(1);
  tft.setTextColor(valuecolor);
  tft.drawSmoothArc(120,120, 121, 110, 45, 90, COLOR_GREY, COLOR_BACKGROUND,true);
  tft.drawSmoothArc(120,120, 121, 110, 45, arcLenght, valuecolor, COLOR_BACKGROUND, true);
  tft.fillRect(24, 142, 45, 20, COLOR_BACKGROUND); // Reset background
  tft.drawString(String(tempAverage,1)+ "C", 25, 143, 2); 

  // 12Volt Battery
  arcLenght = map(canValues.Battery, 11.5, 14.5, 0, 45); 
  if (arcLenght < 1) {arcLenght = 1;}
  if (arcLenght > 45) {arcLenght = 45;}
  if (canValues.Battery < 11.5) { valuecolor = TFT_RED; }
  else if (canValues.Battery < 12.0) { valuecolor = TFT_YELLOW; }
  else { valuecolor = 0x2520; }
  tft.setTextSize(1);
  tft.setTextColor(valuecolor);
  tft.drawSmoothArc(120,120, 121, 110, 270, 315, COLOR_GREY, COLOR_BACKGROUND, true); // reset
  tft.drawSmoothArc(120,120, 121, 110, 315 - arcLenght, 315, valuecolor, COLOR_BACKGROUND, true); // value
  tft.fillRect(179, 142, 40, 18, COLOR_BACKGROUND); //Reset text background
  tft.drawString(String(canValues.Battery, 1) + "V", 180, 143, 2); 

  //Akku Voltage
  tft.fillSmoothRoundRect(85, 130, 70, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextSize(2);
  if (canValues.Volt < 51.2 || canValues.Volt > 58) { tft.setTextColor(TFT_ORANGE); } 
  else {tft.setTextColor(TFT_WHITE);}
  tft.drawString(String(canValues.Volt, 1) + "V", 90, 143);

  // SoC
  tft.setTextSize(2);
  if (canValues.SoC < 15) { tft.setTextColor(TFT_RED); }
  else if (canValues.SoC < 30) { tft.setTextColor(TFT_YELLOW); }
  else if (canValues.SoC > 90) { tft.setTextColor(TFT_DARKCYAN); }
  else { tft.setTextColor(COLOR_GREY); }
  tft.fillRect(104, 39, 50, 20, COLOR_BACKGROUND); // Reset text background
  tft.drawString(String(canValues.SoC) + "%", 105, 40);

  // Status Indicator
  tft.drawRoundRect(63, 190, 55, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorStatus);
  tft.setTextSize(1);
  tft.drawString("Status", 73, 197);

  // CAN Indicator
  tft.drawRoundRect(80, 215, 38, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorCAN);
  tft.setTextSize(1);
  tft.drawString("CAN", 90, 222);

  // WIFI Indicator
  tft.drawRoundRect(122, 190, 55, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorWIFI); 
  tft.setTextSize(1);
  tft.drawString("WIFI", 138, 197);

  // Tx Indicator
  tft.drawRoundRect(122, 215, 38, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorTx);
  tft.setTextSize(1);
  tft.drawString("Tx", 135, 222);
}

void DisplayTripResults() {
  /*
  tft.fillRect(0, 21, DISPLAY_WIDTH, 121, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.drawString("Diese Fahrt:", 5, 25);
  tft.setTextSize(2);
  tft.drawString("Dauer: " + String((Trip.endTime - Trip.startTime) / 1000 / 60) + " min. / " + String((Trip.endKM - Trip.startKM) / 10, 1) + " km", 5, 55);
  tft.drawString("km/h:  " + String(Trip.maxSpeed, 0) + " max / " + String((float)Trip.speedBuffer / Trip.records,1) + " avg.", 5, 75);
  tft.drawString("kWh:   " + String(Trip.energyConsumed / 100000, 2) + " / " + String((float)(Trip.energyBuffer / Trip.records) / 1000) + " avg.", 5, 95);
  tft.drawString("Akku:  " + String((Trip.startSoC - Trip.endSoC) * -1) + "% -> (" + String((Trip.startSoC - Trip.endSoC) * 0.06, 2) + " kwh)" , 5, 115);
  */

  float drivenKM = (Trip.endKM - Trip.startKM) / 10;
  int drivenMin = (Trip.endTime - Trip.startTime) / 1000 / 60;
  int drivenSoC = Trip.startSoC - Trip.endSoC;

  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(2);
  tft.drawString("Diese Fahrt:",50, 23, 2);
  int positionX = 25;
  int positionY = 55;
  // Dauer: Min / KM
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("Dauer", positionX +10 , positionY +2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  if (drivenMin >= 10) {
    tft.drawString(String(drivenMin) + " Min | " + String(drivenKM, 1) + "km", positionX +10, positionY +19);
  }
  else {
    tft.drawString(" " + String(drivenMin) + " Min | " + String(drivenKM, 1) + "km", positionX +10, positionY +19);
  }
  // Geschiwndigkeit: Durchschnitt / Max
  positionY += 43;
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("km/h", positionX +10 , positionY +1, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString(String((drivenKM / drivenMin) * 60, 1) + "   | " + String(Trip.maxSpeed, 0), positionX +10, positionY +19);
  tft.drawSmoothCircle(positionX +75, positionY +25, 7, TFT_WHITE, COLOR_BACKGROUND);
  tft.drawLine(positionX +67, positionY +32, positionX +83, positionY +18, TFT_WHITE);
  tft.fillTriangle(positionX +155, positionY +33, positionX +175, positionY +33, positionX +175, positionY +22, TFT_WHITE);
  // Energieverbrauch: Gesammt / je 100 km
  positionY += 43;
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("kWh", positionX +10 , positionY +0, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("  " + String(drivenSoC * 0.06, 1) + "  | " + String((drivenSoC * 0.06) / drivenKM * 100,1), positionX +10, positionY +19);
  tft.drawSmoothCircle(positionX +170, positionY +25, 7, TFT_WHITE, COLOR_BACKGROUND);
  tft.drawLine(positionX +162, positionY +33, positionX +178, positionY +18, TFT_WHITE);
  // Akkuverbrauch
  positionY += 43;
  tft.fillSmoothRoundRect(65, positionY, 110, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.drawString("Akku:", 72, positionY +13);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(drivenSoC * -1) + "%", 132, positionY +13);
}

void DisplayCharging() {
  //tft.fillScreen(COLOR_BACKGROUND);

  tft.setTextColor(COLOR_TOPOLINO, COLOR_BACKGROUND, true);
  tft.setTextSize(2);
  tft.drawString("Ladevorgang:", 42, 50, 2);
  // Ladestrom
  tft.setTextSize(3);
  tft.drawString(String(canValues.Current, 1) + " A", 70, 90);
  // SoC
  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, COLOR_BACKGROUND, true);
  tft.drawString(String(canValues.SoC) + "%", 140, 125);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_GREY, COLOR_BACKGROUND, true);
  tft.drawString(String(thisCharge.startSoC) + "% >", 30, 130);  
  // Ladedauer
  tft.setTextSize(3);
  tft.setTextColor(COLOR_TOPOLINO, COLOR_BACKGROUND, true);
  tft.drawString(String((millis() - thisCharge.startTime) / 1000 / 60) + " Min.", 60, 165);
  
  // Temperatur Akku
  tft.setTextSize(2);
  tft.setTextColor(COLOR_GREY, COLOR_BACKGROUND, true);
  tft.drawString(String((float)(canValues.Temp1 + canValues.Temp2) / 2, 1) + " C", 90, 200);

  // Animation
  tft.drawSmoothArc(120, 120, 121, 105, 0, 360, COLOR_BACKGROUND, COLOR_BACKGROUND);
  int arcLenght = 45;
  if (thisCharge.helperCircal > 360) {thisCharge.helperCircal = 0;}
  int arcStart = thisCharge.helperCircal;
  int arcEnd = arcStart + arcLenght;
  if (arcEnd > 360) {arcEnd = arcEnd - 360;}
  tft.drawSmoothArc(120, 120, 121, 105, arcStart, arcEnd, COLOR_TOPOLINO, COLOR_BACKGROUND, true);
  thisCharge.helperCircal += 5;
}

void DisplayChargingResult() {
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(2);
  tft.drawString("Ladevorgang:", 42, 50, 2);
  // Lademenge
  tft.setTextSize(3);
  tft.drawString(String((thisCharge.endSoC - thisCharge.startSoC) * 0.06, 1) + " kWh", 70, 90);
  // SoC
  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(thisCharge.endSoC) + "%", 140, 125);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_GREY);
  tft.drawString(String(thisCharge.startSoC) + "% >", 30, 130);  
  // Ladedauer
  tft.setTextSize(3);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.drawString(String((thisCharge.endTime - thisCharge.startTime) / 1000 / 60) + " Min.", 60, 165);
  // Temperatur Akku
  tft.setTextSize(2);
  tft.setTextColor(COLOR_GREY, COLOR_BACKGROUND, true);
  tft.drawString(String((float)(canValues.Temp1 + canValues.Temp2) / 2, 1) + " C", 90, 200);


  // Charge
  tft.drawSmoothArc(120, 120, 121, 105, 0, map(thisCharge.endSoC, 0, 100, 1, 360), COLOR_TOPOLINO, COLOR_BACKGROUND, true);
}

void ConnectWIFIAndSendData() {
  if (WIFICheckConnection()) {
    if (SendDataSimpleAPI()) { DataToSend = false; }
  }
  else if (WIFIConnect()) {
    if (SendDataSimpleAPI()) { DataToSend = false; }
  }
}

bool SendDataSimpleAPI() {
  Serial.println("Send Data via REST");
  StatusIndicatorTx = TFT_BLUE;
  bool result = false;

  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/setBulk?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;

  String DataURLEncoded = "";
  if (canValues.SoCUp) {DataURLEncoded += "&0_userdata.0.topolino.SoC=" + String(canValues.SoC);}
  if (canValues.BatteryUp) {DataURLEncoded += "&0_userdata.0.topolino.12VBatt=" + String(canValues.Battery); }
  if (canValues.CurrentUp) { DataURLEncoded += "&0_userdata.0.topolino.BattA=" + String(canValues.Current); }
  if (canValues.Temp1Up) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp1=" + String(canValues.Temp1); }
  if (canValues.Temp2Up) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp2=" + String(canValues.Temp2); }
  if (canValues.VoltUp) { DataURLEncoded += "&0_userdata.0.topolino.BattV=" + String(canValues.Volt); }
  if (canValues.HandbrakeUp) { DataURLEncoded += "&0_userdata.0.topolino.Handbrake=" + String(canValues.Handbrake); }
  if (canValues.ODOUp) { DataURLEncoded += "&0_userdata.0.topolino.ODO=" + String(canValues.ODO / 10); }
  if (canValues.OBCRemainingMinutesUp) { DataURLEncoded += "&0_userdata.0.topolino.OnBoardChargerRemaining=" + String(canValues.OBCRemainingMinutes); }
  if (canValues.ReadyUp) { DataURLEncoded += "&0_userdata.0.topolino.Ready=" + String(canValues.Ready); }
  if (canValues.RemainingDistanceUp) { DataURLEncoded += "&0_userdata.0.topolino.RemainingKM=" + String(canValues.SoC * 0.75); }
  if (canValues.GearUp) { DataURLEncoded += "&0_userdata.0.topolino.gear=" + String(canValues.Gear); }
  if (canValues.SpeedUp) { DataURLEncoded += "&0_userdata.0.topolino.speed=" + String(canValues.Speed); }
  DataURLEncoded += "&ack=true";

  Serial.print("Data: ");
  Serial.println(URL + DataURLEncoded); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(3 * 1000); // 3 seconds timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode >= 200 && httpResponseCode <= 299) {
    StatusIndicatorTx = TFT_GREEN;
    result = true;

    canValues.SoCUp = false;
    canValues.BatteryUp = false;
    canValues.CurrentUp = false;
    canValues.Temp1Up = false;
    canValues.Temp2Up = false;
    canValues.VoltUp = false;
    canValues.HandbrakeUp = false;
    canValues.ODOUp = false;
    canValues.OBCRemainingMinutesUp = false;
    canValues.ReadyUp = false;
    canValues.RemainingDistanceUp = false;
    canValues.GearUp = false;
    canValues.SpeedUp = false;
  }
  else {
    StatusIndicatorTx = TFT_RED;
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return result; 
}

// TODO: does not work
void SendRemoteLogSimpleAPI(String message) {
  Serial.println("Send Remote Log:");
  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/set/0_userdata.0.topolino.LastLogEntry?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;
  String DataURLEncoded = "&value=";
  DataURLEncoded += urlEncode(message);
  DataURLEncoded += "&ack=true";
  Serial.print("Data: ");
  Serial.println(URL + DataURLEncoded); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(1 * 1000); // 1 second timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  http.end();
}

void SerialPrintValues() {
  Serial.println("Topolino Info Display - Values");
  Serial.println("Can Messages Processed: " + String(CanMessagesProcessed));
  Serial.println(" - ECU ODO: " + String(canValues.ODO / 10) + " km");
  Serial.println(" - ECU Speed: " + String(canValues.Speed) + " km/h");
  Serial.println(" - OBC Remaining Time: " + String(canValues.OBCRemainingMinutes) + " minutes");
  Serial.println(" - 12V Battery: " + String(canValues.Battery) + " V");
  Serial.println(" - Battery Temp1: " + String(canValues.Temp1) + " °C");
  Serial.println(" - Battery Temp2: " + String(canValues.Temp2) + " °C");
  Serial.println(" - Battery Volt: " + String(canValues.Volt) + " V");
  Serial.println(" - Battery Current: " + String(canValues.Current) + " A");
  Serial.println(" - Battery SoC: " + String(canValues.SoC) + " %");
  Serial.println(" - Display Gear: " + String(canValues.Gear) );
  Serial.println(" - Display Remaining Distance: " + String(canValues.RemainingDistance) + " km");
  Serial.println(" - Display Ready: " + String(canValues.Ready) + " (1=Ready, 0=Not Ready, -1=Unknown)");
  Serial.println(" - Status Handreake: " + String(canValues.Handbrake) + " (1=On, 0=Off, -1=Unknown)");
  Serial.println("=============================");
}

void TripRecording() {
  if ( Trip.maxSpeed < canValues.Speed) { Trip.maxSpeed = canValues.Speed; }
  if (Trip.startTime == 0) {
    Trip.startTime = millis();
    Trip.startKM = canValues.ODO;
  }
  Trip.endTime = millis();
  Trip.endKM = canValues.ODO;  
  if (Trip.endSoC == 0 || Trip.endSoC > canValues.SoC) { Trip.endSoC = canValues.SoC; }
}

void SleepLightStart() {
  IsSleeping = true;
  Serial.println("Going to light sleep...");
  StatusIndicatorCAN = TFT_DARKGREY;
  StatusIndicatorStatus = TFT_DARKCYAN;
  StatusIndicatorTx = TFT_DARKGREY;
  StatusIndicatorWIFI = TFT_DARKGREY;

  int i = 0;
  do {
    i++;
    WIFIConnect();
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TOPOLINO);
    tft.drawString("Sleeping...", 30, 110);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Sending data", 30, 160);
    
    // Status Indicator
    tft.drawRoundRect(63, 190, 55, 20, 8, COLOR_ALMOSTBLACK);
    tft.setTextColor(StatusIndicatorStatus);
    tft.setTextSize(1);
    tft.drawString("Status", 73, 197);

    // CAN Indicator
    tft.drawRoundRect(80, 215, 38, 20, 8, COLOR_ALMOSTBLACK);
    tft.setTextColor(StatusIndicatorCAN);
    tft.setTextSize(1);
    tft.drawString("CAN", 90, 222);

    // WIFI Indicator
    tft.drawRoundRect(122, 190, 55, 20, 8, COLOR_ALMOSTBLACK);
    tft.setTextColor(StatusIndicatorWIFI); 
    tft.setTextSize(1);
    tft.drawString("WIFI", 138, 197);

    // Tx Indicator
    tft.drawRoundRect(122, 215, 38, 20, 8, COLOR_ALMOSTBLACK);
    tft.setTextColor(StatusIndicatorTx);
    tft.setTextSize(1);
    tft.drawString("Tx", 135, 222);

    delay(1000);
  }
  while ( SendDataSimpleAPI() == false && i < 5);


  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.drawString("Sleeping...", 30, 120);

  // WIFI Indicator
  tft.drawRoundRect(122, 190, 55, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorWIFI); 
  tft.setTextSize(1);
  tft.drawString("WIFI", 138, 197);

  // Tx Indicator
  tft.drawRoundRect(122, 215, 38, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorTx);
  tft.setTextSize(1);
  tft.drawString("Tx", 135, 222);
  
}

void SleepDeepStart() {

  Serial.println("Going to Deep sleep...");

  // Turn off display power
  digitalWrite(DISPLAY_POWER_PIN, LOW);

  delay(500);
  // Go to deep sleep
  esp_deep_sleep_start();
}

float average( float newvalue, float &buffer, float factor) {
  float avg ;
  buffer = buffer - (buffer / factor);
  buffer = buffer + newvalue;
  avg = buffer / factor;
  return avg;
}

void DebugFakeValues() {
  canValues.Battery = random(0, 150) / 10;
  canValues.SoC = random(0, 100);
  canValues.Gear = "D";
  canValues.Temp1 = random(-90, 450) / 10;
  canValues.Temp2 = random(-90, 450) / 10;
  canValues.Volt = random(410, 580) / 10;
  canValues.Current = random(-1750, 750) / 10;
  canValues.ODO = random(0, 1000000); // km
  canValues.Speed = random(0, 50); // km/h
  canValues.OBCRemainingMinutes = random(0, 250); // minutes
  canValues.RemainingDistance = random(0, 80); // km
  canValues.Ready = 1;
  canValues.Handbrake = 1;
}

