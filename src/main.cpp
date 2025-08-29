#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <spi.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h>
#include <UrlEncode.h>
#include <ACAN2515.h>
#include <TelnetStream.h>
#include <BluetoothSerial.h>
#include <img.h>

//#define DEBUG

const char VERSION[] = "0.94b";
#define ShowConsumptionAsKW true

#define DISPLAY_POWER_PIN 22
#define CAN_INTERRUPT 27
#define CAN_CS 33
#define CAN_MOSI 13
#define CAN_MISO 12
#define CAN_SCK 14
#define CAN_8MHz 8UL * 1000UL * 1000UL
#define CAN_500Kbs 500UL * 1000UL
#define ONBOARD_LED 2

#define COLOR_ALMOSTBLACK 0x436c
#define COLOR_BACKGROUND 0x2104
#define COLOR_BG_GREEN 0x0140
#define COLOR_BG_RED 0x3000
#define COLOR_TOPOLINO 0x05f5
#define COLOR_GREY 0xa554

struct trip {
  unsigned long startTime = 0;
  unsigned long endTime;
  float startKM = 0;
  float endKM;
  int maxSpeed = 0;
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
BluetoothSerial BT;

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
unsigned long ReversingLightLastRun = 0;
unsigned long BTConnectLastRun = 0;

// Deep Sleep Timeout
#define DEEP_SLEEP_TIMEOUT 15 * 60 * 1000 // 15 minutes in milliseconds1

// Bluetooth relais module
uint8_t BT_Slave_MAC[6] = {0x59, 0x95, 0xA4, 0x50, 0x71, 0x78}; // 59:95:A4:50:71:78
String BT_Slave_Name = "JDY-33-SPP";

// Globals
int CanError = 0;
unsigned long CanMessagesProcessed = 0;
unsigned long CanMessagesLastRecived = 0;
float Value_Battery_Current_Buffer = 0;
trip thisTrip; // Trip data structure
CANValues canValues; // CAN values structure
Charge thisCharge;
bool TripActive = false;
bool DataToSend = false;
bool TripDataToSend = false;
bool ChargeDataToSend = false;
unsigned long StatusIndicatorStatus = TFT_DARKGREY;
unsigned long StatusIndicatorCAN = TFT_DARKGREY;
unsigned long StatusIndicatorWIFI = TFT_DARKGREY;
unsigned long StatusIndicatorTx = TFT_DARKGREY;
unsigned long StatusIndicatorBT = TFT_DARKGREY;
bool IsSleeping = false;
bool IsCharging = false;
unsigned int BTReconnectCounter = 0;
bool BTisStarted = false;

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
bool SendChargeInfoSimpleAPI();
bool SendTripInfosSimpleAPI();
void SerialPrintValues();
void TripRecording();
void SleepLightStart();
void SleepDeepStart();
float average(float newvalue, float &buffer, float factor);
void DebugFakeValues();
void Log(String message, bool RemoteLog);
void Log(String message);
bool BTConnect(int timeout);
void BTDisconnect();
void BTSetRelais(int relais, bool state);

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

  // Connect WIFI to enable OTA update at boot
  WIFIConnect();

  // Start Telnet stream for remote logging
  TelnetStream.begin();
 
  // Over The Air update config
  ArduinoOTA.setHostname("TopolinoInfoDisplayOTA");
  ArduinoOTA.begin();

  // Minimal Time for Boot Screen
  while (millis() < 3000) { delay(100); }

  // Setup sequence finished
  Log("TopolinoInfoDisplay started - Version: " + String(VERSION), true);
  digitalWrite(ONBOARD_LED, LOW);
  tft.fillScreen(COLOR_BACKGROUND);

#ifdef DEBUG
  // Debug
#endif

}

// =====================================================================================================
// Main Loop ===========================================================================================
// =====================================================================================================
void loop() {
  unsigned long currentMillis = millis();
  //Log(" - Tick: " + String(currentMillis));

  // Be Alive status
  if (currentMillis - KeepAliveLastRun >= 1000) {
    KeepAliveLastRun = currentMillis;
    digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));

    if (StatusIndicatorStatus == TFT_WHITE) {
      if (TripActive) { StatusIndicatorStatus = TFT_YELLOW; }
      else { StatusIndicatorStatus = TFT_GREEN; }
    }
    else {
      StatusIndicatorStatus = TFT_WHITE;
    }
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
    Log("CAN Connect...");
    CanConnect();
  }
  else if ( CanMessagesLastRecived - currentMillis > 5000) { // If no CAN messages received for 5 seconds, set indicator to grey
    StatusIndicatorCAN =  TFT_DARKGREY;
  }

  // Check BT connection to Relais box
  if (currentMillis - BTConnectLastRun > 30000 && canValues.Ready == 1) { // Try to reconnect every 30 seconds
    BTConnectLastRun = currentMillis;
    if (BTisStarted) {
      if (!BT.connected()) {
        Log("BT Relais not connected! Reconnect Count: " + String(BTReconnectCounter), true);
        BTReconnectCounter++;
        if (BTReconnectCounter <= 2) {if ( BTConnect(1000)) {BTReconnectCounter = 0;} }
      }
      else if (BT.connected() && canValues.Ready != 1)
      {
        BTDisconnect();
      }
    }
    else {
      BTConnect(10000);
    }
  }

  // Reversing Light
  if (currentMillis - ReversingLightLastRun >= 500 && BTisStarted) {
    ReversingLightLastRun = currentMillis;
    if (BT.connected()) {
      if (canValues.Gear == "R") {
        BTSetRelais(1, true); // Turn on reversing light
        StatusIndicatorBT = TFT_YELLOW;

        #ifdef DEBUG
          Log("Reversing Light ON");
        #endif
      }
      else {
        BTSetRelais(1, false); // Turn off reversing light
        StatusIndicatorBT = TFT_GREEN;

        #ifdef DEBUG
          Log("Reversing Light OFF");
        #endif
      }
    }
  }

  // Check for new trip recording
  if (canValues.Ready == 1 && canValues.Speed > 0 && !TripActive) {
    TripActive = true;
    Log("Trip started at " + String(thisTrip.startTime) + " with ODO: " + String(thisTrip.startKM));
    // Reset Trip data
    thisTrip.startTime = 0;
    thisTrip.endTime = 0;
    thisTrip.startKM = 0;
    thisTrip.endKM = 0;
    thisTrip.maxSpeed = 0;
    thisTrip.startSoC = canValues.SoC;
    thisTrip.endSoC = canValues.SoC;
  } 
  if (currentMillis - TripRecordingLastRun >= TripRecordInterval && TripActive) {
    TripRecordingLastRun = currentMillis;
    TripRecording();
  }

  // Check current Trip has ended
  if (canValues.Ready == 0 && TripActive) { //was: || (canValues.Gear == "N" && canValues.Handbrake && canValues.Speed == 0)
    TripActive = false;
    if (((thisTrip.endTime - thisTrip.startTime) / 1000 / 60) > 2) { // Datenübertragung nur Touren +ber 2 Minuten
      TripDataToSend = true;
    }
    
    // Show Trip results
    DisplayTripResults();
    //Send Data while showing Results
    ConnectWIFIAndSendData();
    
    delay(20000); // Show Trip results for 20 seconds
    tft.fillScreen(COLOR_BACKGROUND);
    DisplayMainUI();
  }

  // Check if new charge
  if (canValues.OBCRemainingMinutes >= 0 && canValues.Current > 0 && !IsCharging) {
    IsCharging = true;
    thisCharge.startSoC = canValues.SoC;
    thisCharge.startTime = currentMillis;
    tft.fillScreen(COLOR_BACKGROUND);

    Log(" Charging started", true);
  }
  // Chekc if chargeing has ended
  if (IsCharging && canValues.OBCRemainingMinutes == -1) {
    IsCharging = false;
    thisCharge.endSoC = canValues.SoC;
    thisCharge.endTime = currentMillis;
    ChargeDataToSend = true;
    BTReconnectCounter = 0;
    Log("Charging has ended", true);

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
    Log("WIFI Stratus: " + String(WiFi.status()));
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
    BTReconnectCounter = 0;
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
  StatusIndicatorCAN = TFT_CYAN;
  hspi.begin(CAN_SCK, CAN_MISO, CAN_MOSI);
  ACAN2515Settings CanSettings (CAN_8MHz, CAN_500Kbs);
  CanSettings.mRequestedMode = ACAN2515Settings::ListenOnlyMode ;
  
  CanError = can.begin(CanSettings, [] { can.isr () ; });

  if ( CanError == 0) {
    Log("CAN-Module Initialized Successfully!");
    StatusIndicatorCAN = TFT_LIGHTGREY;
    } 
  else {
    Log("ERROR: Initializing CAN-Module failed! ErrCode: " + String(CanError));
    StatusIndicatorCAN = TFT_RED;
    }
  CanMessagesLastRecived = millis();
}

void CANCheckMessage(){
  //Log("CAN check message"); 

  CANMessage canMsg;
  while (can.receive(canMsg)) {
    StatusIndicatorCAN = TFT_GREEN;

    //can.receive(canMsg);
    //Log("CAN Message recived - ID: " + String(canMsg.id, HEX)); 
    //Log("CAN Message recived - DLC: " + String(canMsg.len));
    //Log("CAN Message recived - RTR:" + String(canMsg.rtr));
    //Log("CAN Message recived - Data: " + String(canMsg.data[0], HEX) + " " + String(canMsg.data[1], HEX) + " " + String(canMsg.data[2], HEX) + " " + String(canMsg.data[3], HEX) + " " + String(canMsg.data[4], HEX) + " " + String(canMsg.data[5], HEX) + " " + String(canMsg.data[6], HEX) + " " + String(canMsg.data[7], HEX) + " " + String(canMsg.data[8], HEX));

    if (!canMsg.rtr) {
      CanMessagesProcessed++;
      
      if (IsSleeping) {
        IsSleeping = false;
        tft.fillScreen(COLOR_BACKGROUND);
      }
      
      switch (canMsg.id) {
        // Electronic control Unit (ECU)
        case 0x581: { 
          DataToSend = true;
          if (!canMsg.len == 8) {Log("CAN: Wrong Lenght"); break;}
          // Odo
          unsigned int value1 = canMsg.data[6] << 16 | canMsg.data[5] << 8 | canMsg.data[4];
          //Log("- CAN Value ODO: " + String(value1));
          canValues.ODO = (float)value1;
          canValues.ODOUp = true;

          // Speed 
          unsigned int value = canMsg.data[7];
          //Log("- CAN Value Speed: " + String(value));          
          canValues.Speed = value;
          canValues.SpeedUp = true;
          }
          break;

        // Onboard Charger
        case 0x582: { 
          DataToSend = true;
          if (!canMsg.len == 8) {Log("CAN: Wrong Lenght"); break;}
          
          // Remaining Time
          unsigned int value = canMsg.data[1] << 8 | canMsg.data[0];
          //Log("- CAN Value Remaining Time: " + String(value)); 
          canValues.OBCRemainingMinutes = value / 60;
          if (canValues.OBCRemainingMinutes > 600) { canValues.OBCRemainingMinutes = -1; } // not charging
          canValues.OBCRemainingMinutesUp = true;
          }
          break;

        // 12V Battery
        case 0x593: {
          DataToSend = true;
          if (!canMsg.len == 8) {Log("CAN: Wrong Lenght"); break;}
          unsigned int value = (canMsg.data[1] << 8) | canMsg.data[0];
          //Log("- CAN Value 12V: " + String(value));
          canValues.Battery = (float)value / 100;
          canValues.BatteryUp = true;
          }
          break;

        // Main Battery Temperature
        case 0x594: {
          DataToSend = true;
          if (!canMsg.len == 8) {Log("CAN: Wrong Lenght"); break;}
          
          // Temp 1
          int value1 = canMsg.data[0];
          //Log("- CAN Value Temp1: " + String(value1));
          canValues.Temp1 = value1;
          canValues.Temp1Up = true;

          //Temp 2
          int value2 = canMsg.data[3];
          //Log("- CAN Value Temp2: " + String(value2));
          canValues.Temp2 = value2;
          canValues.Temp2Up = true;
          }          
          break;
        
        // Main Battery Voltage and Current
        case 0x580: {
          DataToSend = true;StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Log("CAN: Wrong Lenght"); break;}

          // Current
          int value1 = (canMsg.data[1] << 8 | canMsg.data[0]);
          if (value1 > 32767) {
            value1 = value1 - 65536; // Convert to signed int
            }
          //Log("- CAN Value Current: " + String(value1));
         canValues.Current = average((float)value1 / 10, Value_Battery_Current_Buffer, 3); // Average over 3 values 
         canValues.CurrentUp = true;

          //Voltage
          int value2 = (canMsg.data[3] << 8) | canMsg.data[2];
          //Log("- CAN Value Voltage: " + String(value2));
          if (value2 > 4200 && value2 < 6500) { // Check is value is in valid range
            canValues.Volt = (float)value2 / 100;
            canValues.VoltUp = true;
          }
        
          // SoC
          unsigned int value3 = canMsg.data[5];
          //Log("- CAN Value SoC: " + String(value3));
          canValues.SoC = value3;
          canValues.SoCUp = true;
          }
          break;

        // Display
        case 0x713: {
          DataToSend = true;
          if (!canMsg.len == 7) {Log("CAN: Wrong Lenght"); break;}
          //Log("CAN: Display Message Data:" + String(canMsg.data[0],HEX) + " " + String(canMsg.data[1],HEX) + " " + String(canMsg.data[2],HEX) + " " + String(canMsg.data[3],HEX) + " " + String(canMsg.data[4],HEX) + " " + String(canMsg.data[5],HEX) + " " + String(canMsg.data[6],HEX));

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
          //Log("- CAN Value Gear Selected Bits: " + String(value1, BIN));

          // Remaining Distance
          unsigned int value2 = canMsg.data[0] & 0x3F; // Mask to get the lower 6 bits
          //Log("- CAN Value Remaining Distance2: " + String(value2));
          canValues.RemainingDistance = value2;
          canValues.RemainingDistanceUp = true; 
          
          //Ready indicator
          switch ((canMsg.data[2] & 0b00000111)) { // Check the last 3 bits
            case 0b101:
              if (canValues.Ready != 1) { BTReconnectCounter = 0 ; } // Reset BT Reconnect tries when car becomes ready to fore recoennection
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
          //Log("- CAN Value Ready: " + String(canMsg.data[2], BIN)); //ready 0b101  not 0b11
          }
          break;

        // Handbreak
        case 0x714: {
          DataToSend = true;
          //Log("CAN:  Handbreak");
          if (!canMsg.len == 2) {Log("CAN: Wrong Lenght"); break;}

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
          //Log("- CAN Value Brake: " + String(canMsg.data[0], HEX));

          break;
        }
        if (DataToSend == true) {StatusIndicatorTx = TFT_YELLOW; } // knonw CAN message has changed values
      }
    else {
      // unknow CAN Message
      StatusIndicatorCAN = TFT_BROWN;
    }
    
  }
}

bool WIFIConnect(){
  Log("WIFI Connect to SSID: " + String(YourWIFI_SSID));
  
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
    Log("WIFI Connecting...." + String(WiFi.status()));
    i++;
    if ( i > 5) { break; }
  }

  return WIFICheckConnection();
}

bool WIFICheckConnection() { 
  if (WiFi.status () == WL_CONNECTED) {
    Log("WIFI Connected! IP: " + (WiFi.localIP().toString()));
    StatusIndicatorWIFI = TFT_GREEN;
    return true;
  }
  else {
    Log("WIFI NOT connected! Status: " + String(WiFi.status()));
    StatusIndicatorWIFI = TFT_RED;
    return false;
  }
}

void WIFIDisconnect(){
#ifdef DEBUG
  Log("WIFI disconnect");
#endif
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  StatusIndicatorWIFI = TFT_DARKGREY;
}

void DisplayBoot() {
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, img1_width, img1_height, img1);
  delay(500);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(3);
  tft.drawCentreString("Topolino",120, 32, 1);
  tft.setTextColor(TFT_BLACK, COLOR_TOPOLINO, true);
  delay(500);
  tft.drawString("Info",40,100);
  delay(500);
  tft.drawString("Display",70,130);
  delay(500);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_ALMOSTBLACK);
  tft.drawCentreString("Version: " + String(VERSION), 120, 185, 1);
  #ifdef DEBUG
    tft.drawCentreString("DEBUG", 120, 220, 1);
  #endif
}

void DisplayMainUI() {
  //tft.fillScreen(COLOR_BACKGROUND);
  int valuecolor;
  
  //Consumption Background
  //tft.drawSmoothArc(120,120, 121, 105, 150, 240, COLOR_BG_RED, COLOR_BACKGROUND, true);
  //tft.drawSmoothArc(120,120, 121, 105, 120, 150, COLOR_BG_GREEN, COLOR_BACKGROUND, true);
  tft.drawSmoothArc(120, 120, 121, 105, 120, 240, TFT_DARKGREY, COLOR_BACKGROUND, true);

  //Consumption Arc
  if (canValues.Current < -2) {
    //Consumption negative = driving
    int arcLenght = map(canValues.Current, -150, 0, 240, 150);
    if (arcLenght > 240) {arcLenght = 240;}
    if (arcLenght < 151) {arcLenght = 151;}
    if (canValues.Current < -75) {valuecolor = TFT_RED;} else {valuecolor = TFT_YELLOW;}
    tft.drawSmoothArc(120,120, 120, 105, 150, arcLenght, valuecolor, COLOR_BACKGROUND, true);
  }
  else if (canValues.Current >02) {
    //Consumption positive  = charging
    int arcLenght = map(canValues.Current, 0, 75, 0, 30);
    if (arcLenght > 30) {arcLenght = 30;}
    if (arcLenght < 1) {arcLenght = 1;}
    tft.drawSmoothArc(120,120, 120, 105, 150 - arcLenght, 150, TFT_GREEN, COLOR_BACKGROUND, true);
  }
  else {
    // No Consumption (Dead zone 0 bis -2)
    tft.drawSpot(61, 24, 6, COLOR_TOPOLINO, COLOR_BACKGROUND);
  }
  
  // Consumption value  
  tft.setTextColor(TFT_WHITE, COLOR_ALMOSTBLACK, true);
  tft.setTextSize(3);
  String consumptionString;
  float currentConsumption = 0;
  if (ShowConsumptionAsKW) {
    currentConsumption = (float)(canValues.Current * canValues.Volt / 1000) * -1;         // -#.##
    consumptionString = String(currentConsumption, 2) + " kW";
  }
  else {
    currentConsumption = canValues.Current * -1;
    consumptionString = String(currentConsumption, 1) + " A";
  }
  tft.fillSmoothRoundRect(40, 65, 160, 60, 10, COLOR_ALMOSTBLACK, COLOR_BACKGROUND); // Reset backgroubnd
  tft.drawRightString(consumptionString, 185, 85, 1);

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

  String rightArcString;
  if ( TripActive ) {
    // consumption per 100km
    float drivenKM = (thisTrip.endKM - thisTrip.startKM) / 10;
    int drivenSoC = thisTrip.startSoC - thisTrip.endSoC;
    float cunsumption100km = (drivenSoC * 0.06) / drivenKM * 100;
    rightArcString = String(cunsumption100km, 1) + "kW";
    arcLenght = map(cunsumption100km, 6, 12, 0, 45);
    if ( cunsumption100km > 12) { valuecolor = TFT_RED; }
    else if ( cunsumption100km > 10) { valuecolor = TFT_ORANGE; }
    else if ( cunsumption100km > 9) { valuecolor = TFT_YELLOW; }
    else if ( cunsumption100km < 7.5) { valuecolor = TFT_CYAN; }
    else { valuecolor = 0x2520; }
  }
  else {
  // 12Volt Battery
    rightArcString = String(canValues.Battery, 1) + "V";
    arcLenght = map(canValues.Battery, 11.5, 14.5, 0, 45); 
    if (canValues.Battery < 11.5) { valuecolor = TFT_RED; }
    else if (canValues.Battery < 12.0) { valuecolor = TFT_YELLOW; }
    else { valuecolor = 0x2520; }
  }
  if (arcLenght < 1) {arcLenght = 1;}
  if (arcLenght > 45) {arcLenght = 45;}
  tft.setTextSize(1);
  tft.setTextColor(valuecolor);
  tft.drawSmoothArc(120,120, 121, 110, 270, 315, COLOR_GREY, COLOR_BACKGROUND, true); // reset
  tft.drawSmoothArc(120,120, 121, 110, 315 - arcLenght, 315, valuecolor, COLOR_BACKGROUND, true); // value
  tft.fillRect(172, 142, 50, 18, COLOR_BACKGROUND); //Reset text background
  tft.drawRightString(rightArcString, 220, 143, 2); 

  //Akku Voltage
  tft.fillSmoothRoundRect(85, 130, 70, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextSize(2);
  if (canValues.Volt < 51.2 || canValues.Volt > 58) { tft.setTextColor(TFT_ORANGE); } 
  else {tft.setTextColor(TFT_WHITE);}
  tft.drawCentreString(String(canValues.Volt, 1) + "V", 120, 143, 1);

  // SoC
  tft.setTextSize(2);
  if (canValues.SoC < 15) { tft.setTextColor(TFT_RED); }
  else if (canValues.SoC < 30) { tft.setTextColor(TFT_YELLOW); }
  else if (canValues.SoC > 90) { tft.setTextColor(TFT_DARKCYAN); }
  else { tft.setTextColor(COLOR_GREY); }
  tft.fillRect(90, 39, 70, 20, COLOR_BACKGROUND); // Reset text background
  tft.drawCentreString(String(canValues.SoC) + "%", 120, 40, 1);

  // Status Indicator
  tft.fillRoundRect(63, 188, 55, 20, 8, StatusIndicatorStatus);
  tft.setTextColor(COLOR_ALMOSTBLACK);
  tft.setTextSize(1);
  tft.drawString("Status", 73, 195);

  // WIFI Indicator
  tft.drawRoundRect(122, 188, 55, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorWIFI); 
  tft.setTextSize(1);
  tft.drawString("WIFI", 139, 195);

  // BT Indicator
  unsigned long BTBackground;
  unsigned long BTText;
  if (StatusIndicatorBT == TFT_YELLOW) {
    BTBackground = TFT_YELLOW;
    BTText = TFT_BLACK;
  }
  else {
    BTBackground = COLOR_ALMOSTBLACK;
    BTText = StatusIndicatorBT;
  }
  tft.drawRoundRect(76, 213, 25, 20, 8, BTBackground);
  tft.setTextColor(BTText);
  tft.setTextSize(1);
  tft.drawString("BT", 83, 219);

  // CAN Indicator
  tft.drawRoundRect(105, 213, 30, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorCAN);
  tft.setTextSize(1);
  tft.drawString("CAN", 111, 219);

  // Tx Indicator
  tft.drawRoundRect(139, 213, 25, 20, 8, COLOR_ALMOSTBLACK);
  tft.setTextColor(StatusIndicatorTx);
  tft.setTextSize(1);
  tft.drawString("Tx", 146, 219);
}

void DisplayTripResults() {
  float drivenKM = (thisTrip.endKM - thisTrip.startKM) / 10;
  int drivenMin = (thisTrip.endTime - thisTrip.startTime) / 1000 / 60;
  int drivenSoC = thisTrip.startSoC - thisTrip.endSoC;

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
  tft.drawString(String((drivenKM / drivenMin) * 60, 1) + "   | " + String(thisTrip.maxSpeed), positionX +10, positionY +19);
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
  tft.drawSmoothCircle(positionX +175, positionY +25, 7, TFT_WHITE, COLOR_BACKGROUND);
  tft.drawLine(positionX +167, positionY +33, positionX +183, positionY +18, TFT_WHITE);
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
    if (DataToSend) { if (SendDataSimpleAPI()) { DataToSend = false; } }
    if (TripDataToSend) { if (SendTripInfosSimpleAPI()) { TripDataToSend = false; }}
    if (ChargeDataToSend) { if ( SendChargeInfoSimpleAPI()) { ChargeDataToSend = false; } }   
  }
  else if (WIFIConnect()) {
    if (DataToSend) { if (SendDataSimpleAPI()) { DataToSend = false; } }
    if (TripDataToSend) { if (SendTripInfosSimpleAPI()) { TripDataToSend = false; }}
    if (ChargeDataToSend) { if ( SendChargeInfoSimpleAPI()) { ChargeDataToSend = false; } }   
  }
}

bool SendDataSimpleAPI() {
  Log("Send Data via REST");
  StatusIndicatorTx = TFT_BLUE;

  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/setBulk?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;

  String DataURLEncoded = "";
  if (canValues.SoCUp) {DataURLEncoded += "&0_userdata.0.topolino.SoC=" + String(canValues.SoC); }
  if (canValues.BatteryUp) {DataURLEncoded += "&0_userdata.0.topolino.12VBatt=" + String(canValues.Battery); }
  if (canValues.CurrentUp) { DataURLEncoded += "&0_userdata.0.topolino.BattA=" + String(canValues.Current); }
  if (canValues.Temp1Up) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp1=" + String(canValues.Temp1); }
  if (canValues.Temp2Up) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp2=" + String(canValues.Temp2); }
  if (canValues.VoltUp) { DataURLEncoded += "&0_userdata.0.topolino.BattV=" + String(canValues.Volt); }
  if (canValues.HandbrakeUp) { DataURLEncoded += "&0_userdata.0.topolino.Handbreake=" + String(canValues.Handbrake); }
  if (canValues.ODOUp) { DataURLEncoded += "&0_userdata.0.topolino.ODO=" + String(canValues.ODO / 10); }
  if (canValues.OBCRemainingMinutesUp) { DataURLEncoded += "&0_userdata.0.topolino.OnBoardChargerRemaining=" + String(canValues.OBCRemainingMinutes); }
  if (canValues.ReadyUp) { DataURLEncoded += "&0_userdata.0.topolino.Ready=" + String(canValues.Ready); }
  if (canValues.RemainingDistanceUp) { DataURLEncoded += "&0_userdata.0.topolino.RemainingKM=" + String(canValues.SoC * 0.75); }
  if (canValues.GearUp) { DataURLEncoded += "&0_userdata.0.topolino.gear=" + String(canValues.Gear); }
  if (canValues.SpeedUp) { DataURLEncoded += "&0_userdata.0.topolino.speed=" + String(canValues.Speed); }
  DataURLEncoded += "&ack=true";

  Log(" URL: " + String(URL) + String(DataURLEncoded)); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(3 * 1000); // 3 seconds timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Log(" HTTP Response Code: " + String(httpResponseCode));

  if (httpResponseCode >= 200 && httpResponseCode <= 299) {
    StatusIndicatorTx = TFT_GREEN;

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

    http.end();
    Log("SendDataSimpleAPI OK", true);
    return true; 
  }
  else {
    StatusIndicatorTx = TFT_RED;
    Log("HTTP Response Code: " + String(httpResponseCode));
    
    http.end();
    Log("SendDataSimpleAPI FAILED - Response Code: " + String(httpResponseCode) + " - " + DataURLEncoded, true);
    return false; 
  }
}

void SendRemoteLogSimpleAPI(String message) {
  Log("Send Remote Log:");
  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/set/0_userdata.0.topolino.LastLogEntry?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;
  String DataURLEncoded = "&value=";
  DataURLEncoded += urlEncode(message);
  DataURLEncoded += "&ack=true";
  Log(" URL: " + String(URL) + String(DataURLEncoded)); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(1 * 1000); // 1 second timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Log(" HTTP Response Code: " + String(httpResponseCode));
  http.end();
}

bool SendChargeInfoSimpleAPI() {
  Log("Send Charge Info Log:");
  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/setBulk?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;
  String DataURLEncoded = "";
  DataURLEncoded += "&0_userdata.0.topolino.charge.dauer=" + String((thisCharge.endTime - thisCharge.startTime) / 1000 / 60);
  DataURLEncoded += "&0_userdata.0.topolino.charge.ladung=" + String((thisCharge.endSoC - thisCharge.startSoC) * 0.06, 1);
  DataURLEncoded += "&0_userdata.0.topolino.charge.startSoC=" + String(thisCharge.startSoC);
  DataURLEncoded += "&0_userdata.0.topolino.charge.endSoC=" + String(thisCharge.endSoC);
  DataURLEncoded += "&ack=true";

  Log(" URL: " + String(URL) + String(DataURLEncoded)); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(3 * 3000); // 3 second timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Log(" HTTP Response Code: " + String(httpResponseCode));

  if (httpResponseCode >= 200 && httpResponseCode <= 299) {
    Log("SendChargeInfosSimpleAPI OK", true);
    http.end();
    return true;
  }
  else {
    Log("SendChargeInfosSimpleAPI FAILED - Response Code: " + String(httpResponseCode) + " - " + DataURLEncoded, true);
    http.end();
    return false;
  }
}

bool SendTripInfosSimpleAPI() {
  Log("Send Charge Info Log:");
  HTTPClient http;

  float drivenKM = (thisTrip.endKM - thisTrip.startKM) / 10;
  int drivenMin = (thisTrip.endTime - thisTrip.startTime) / 1000 / 60;
  int drivenSoC = thisTrip.startSoC - thisTrip.endSoC;
  // SimpleAPI set values bulk
  String URL = "http://" + String(YourSimpleAPI_IP) + ":" + String(YourSimpleAPI_Port) + "/setBulk?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;
  String DataURLEncoded = "";
  DataURLEncoded += "&0_userdata.0.topolino.trip.consumption=" + String((float)((thisTrip.endSoC - thisTrip.startSoC) * 0.06) * -1, 1);
  DataURLEncoded += "&0_userdata.0.topolino.trip.dauer=" + String((thisTrip.endTime - thisTrip.startTime) / 1000 / 60);
  DataURLEncoded += "&0_userdata.0.topolino.trip.km=" + String((thisTrip.endKM - thisTrip.startKM) / 10, 1);
  DataURLEncoded += "&0_userdata.0.topolino.trip.maxSpeed=" + String(thisTrip.maxSpeed);
  DataURLEncoded += "&0_userdata.0.topolino.trip.SpeedAvg=" + String((drivenKM / drivenMin) * 60, 1);
  DataURLEncoded += "&0_userdata.0.topolino.trip.consumptionAvg=" + String((drivenSoC * 0.06) / drivenKM * 100,1);
  DataURLEncoded += "&ack=true";

  Log(" URL: " + String(URL) + String(DataURLEncoded)); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(3 * 3000); // 3 second timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();
  Log(" HTTP Response Code: " + String(httpResponseCode));

  if (httpResponseCode >= 200 && httpResponseCode <= 299) {
    Log("SendTripInfosSimpleAPI OK", true);
    http.end();
    return true;
  }
  else {
    Log("SendTripInfosSimpleAPI FAILED - Response Code: " + String(httpResponseCode) + " - " + DataURLEncoded, true);
    http.end();
    return false;
  }
}

void SerialPrintValues() {
  Log("Topolino Info Display - Values");
  Log("Can Messages Processed: " + String(CanMessagesProcessed));
  Log(" - ECU ODO: " + String(canValues.ODO / 10) + " km");
  Log(" - ECU Speed: " + String(canValues.Speed) + " km/h");
  Log(" - OBC Remaining Time: " + String(canValues.OBCRemainingMinutes) + " minutes");
  Log(" - 12V Battery: " + String(canValues.Battery) + " V");
  Log(" - Battery Temp1: " + String(canValues.Temp1) + " °C");
  Log(" - Battery Temp2: " + String(canValues.Temp2) + " °C");
  Log(" - Battery Volt: " + String(canValues.Volt) + " V");
  Log(" - Battery Current: " + String(canValues.Current) + " A");
  Log(" - Battery SoC: " + String(canValues.SoC) + " %");
  Log(" - Display Gear: " + String(canValues.Gear) );
  Log(" - Display Remaining Distance: " + String(canValues.RemainingDistance) + " km");
  Log(" - Display Ready: " + String(canValues.Ready) + " (1=Ready, 0=Not Ready, -1=Unknown)");
  Log(" - Status Handreake: " + String(canValues.Handbrake) + " (1=On, 0=Off, -1=Unknown)");
  Log("=============================");
}

void TripRecording() {
  if ( thisTrip.maxSpeed < canValues.Speed) { thisTrip.maxSpeed = canValues.Speed; }
  if (thisTrip.startTime == 0) {
    thisTrip.startTime = millis();
    thisTrip.startKM = canValues.ODO;
  }
  thisTrip.endTime = millis();
  thisTrip.endKM = canValues.ODO;  
  if (thisTrip.endSoC == 0 || thisTrip.endSoC > canValues.SoC) { thisTrip.endSoC = canValues.SoC; }
}

void SleepLightStart() {
  IsSleeping = true;
  Log("Going to light sleep...");
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
  tft.drawCentreString("Topolino is", 120, 100, 1);
  tft.drawCentreString("sleeping...", 120, 125, 1);

  // WIFI Indicator
  tft.drawRoundRect(122, 188, 55, 20, 8, StatusIndicatorWIFI);
  tft.setTextColor(TFT_BLACK); 
  tft.setTextSize(1);
  tft.drawString("WIFI", 139, 195);

  // Tx Indicator
  tft.drawRoundRect(139, 213, 25, 20, 8, StatusIndicatorTx);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("Tx", 146, 219);
  
  Log("Light Sleep", true);
}

void SleepDeepStart() {

  Log("Going to Deep sleep...");
  Log("Deep Sleep", true);
  TelnetStream.stop();
  BTDisconnect();
  
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

void Log(String message, bool RemoteLog) {
  Serial.println(message);
  TelnetStream.println(message);
  if (RemoteLog) {
    SendRemoteLogSimpleAPI(message);
  }
}

void Log(String message) { Log(message, false); }

bool BTConnect(int timeout) {
  BTisStarted = false;
  BT.disconnect();
  BT.clearWriteError();
  BT.flush();
  BT.unpairDevice(BT_Slave_MAC);
  BT.begin("TopolinoInfoDisplayBT", true);
  BTisStarted = true;
  BT.setPin("1234");
  BT.setTimeout(timeout); 
  String msg = "BT Connect - Status --> Connected: " + String(BT.connected()) + " Timeout: " + String(BT.getTimeout()) + " isClosed: " + String(BT.isClosed()) + " isReady: " + String(BT.isReady());
  Log(msg, true);
  Log("Bluetooth started - Timeout: " + String(timeout) + " ms", true);
  #ifdef DEBUG
    BTScanResults* BTSCan = BT.discover(30 * 1280); // Discover devices for 30 seconds
    Log("Bluetooth discoverd devices: " + String(BTSCan->getCount()), true);
    for (int i = 0; i < BTSCan->getCount(); i++) {
      BTAdvertisedDevice* device = BTSCan->getDevice(i);
      Log("Device " + String(i) + ": " + device->getName().c_str() + " - " + device->getAddress().toString().c_str() + " - RSSI: " + String(device->getRSSI()) + " dBm");
  }
  #endif

  if (BT.connect(BT_Slave_Name))
  {
    Log("Bluetooth connect OK", true);
    StatusIndicatorBT = TFT_GREEN;
    return true;
  }
  else {
    Log("Bluetooth connect FAILED!", true);
    StatusIndicatorBT = TFT_RED;
    return false;
  }
}

void BTDisconnect() {
  Log("Bluetooth disconnect");
  StatusIndicatorBT = TFT_DARKGREY;
  BTisStarted = false;
  BT.disconnect();
  BT.end(); 
}

void BTSetRelais(int relais, bool state) {
  //Check inpout
  if (relais != 1 && relais != 2) {
    Log("BTSetRelais: Invalid relais number: " + String(relais));
    return;
  }

  // Check BT connection
  if (!BT.connected()) {
    Log("BTSetRelais: Bluetooth not connected!", true);
    return;
  }

  // Send command to the right relais
  switch (relais) {
    case 1:
      if (state) {
        uint8_t command[] = {0xA0, 0x01, 0x01, 0xA2, 0x0D, 0x0A}; // 1 an
        BT.write(command, sizeof(command));
      } 
      else {
        uint8_t command[] = {0xA0, 0x01, 0x00, 0xA1, 0x0D, 0x0A}; // 1 aus
        BT.write(command, sizeof(command));
      }
      break;
    case 2:
      if (state) {
        uint8_t command[] = {0xA0, 0x02, 0x01, 0xA3, 0x0D, 0x0A}; // 2 an
        BT.write(command, sizeof(command));
      } 
      else {
        uint8_t command[] = {0xA0, 0x02, 0x00, 0xA2, 0x0D, 0x0A}; // 2 aus
        BT.write(command, sizeof(command));
      }
      break;
    default:
      break;
}

}
