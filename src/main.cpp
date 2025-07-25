#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ACAN2515.h>
#include <spi.h>

#define VERSION 0.20b
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
  int Speed = 0;
  int OBCRemainingMinutes = 65555;
  float Battery = 0.0;
  int Temp1 = -99;
  int Temp2 = -99;
  float Volt = 0;
  int SoC = 101;
  String Gear = "?";
  int RemainingDistance = 0;
  int Ready = -1; // 1=Ready, 0=Not Ready, -1=Unknown
  float Current = 0;
  int Handbrake = -1; // 1=On, 0=Off, -1=Unknown
};

TFT_eSPI tft = TFT_eSPI();
SPIClass hspi = SPIClass(HSPI);
ACAN2515 can((int)CAN_CS, hspi, (int)CAN_INTERRUPT);

#include "../config/config.h"

//Loops
unsigned long DisplayRefreshLastRun = 0;
const unsigned int DisplayRefreshInterval = 0.33 * 1000; 
unsigned long SendDataLastRun = 0;
const unsigned int SendDataInterval = 30 * 1000; 
unsigned long SerialOutputLastRun = 0;
const unsigned int TripRecordInterval = 1 * 1000;
unsigned long TripRecordingLastRun = 0;
unsigned long KeepAliveLastRun = 0;

// Deep Sleep Timeout
#define DEEP_SLEEP_TIMEOUT 5 * 60 * 1000 // 10 minutes in milliseconds

// Globals
int CanError = 0;
unsigned long CanMessagesProcessed = 0;
unsigned long CanMessagesLastRecived = 0;
float Value_Battery_Current_Buffer = 0;
trip Trip; // Trip data structure
CANValues canValues; // CAN values structure
bool TripActive = false;
bool DataToSend = false;
unsigned long StatusIndicatorStatus = TFT_DARKGREY;
unsigned long StatusIndicatorCAN = TFT_DARKGREY;
unsigned long StatusIndicatorWIFI = TFT_DARKGREY;
unsigned long StatusIndicatorTx = TFT_DARKGREY;

// put function declarations here:
void CANCheckMessage();
bool WIFIConnect();
bool WIFICheckConnection();
void WIFIDisconnect();
bool SendDataSimpleAPI();
void SerialPrintValues();
void TripRecording();
void DisplayTripResults();
void CanConnect();
void SleepStart();
float average(float newvalue, float &buffer, float factor);
void DisplayMainUI();

// Make everything ready ===============================================================================
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
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIMEOUT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)CAN_INTERRUPT, 0); // Wake up on CAN interrupt

  digitalWrite(ONBOARD_LED, HIGH);

  // Turn on display power
  digitalWrite(DISPLAY_POWER_PIN, HIGH);
  delay(500);

  // Initialize display
  tft.init();
  tft.fillScreen(COLOR_BACKGROUND);
  
  delay(1000);

  // Initiate CAN
  CanConnect(); 
  delay(500);

  digitalWrite(ONBOARD_LED, LOW);
  // Debug
  //DebugFakeValues();
  //DisplayTripResults();
  //delay(60000);
  //tft.fillScreen(COLOR_BACKGROUND);
}

// Main Loop ===========================================================================================
void loop() {
  unsigned long currentMillis = millis();
//Serial.println(" - Tick: " + String(currentMillis));

  // Be Alive  status
  if (currentMillis - KeepAliveLastRun >= 1000) {
    KeepAliveLastRun = currentMillis;
    digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));
    if (StatusIndicatorStatus == TFT_WHITE) {
      if (TripActive) { StatusIndicatorStatus = TFT_BLUE; }
      else { StatusIndicatorStatus = TFT_GREEN; }
    }
    else {StatusIndicatorStatus = TFT_WHITE;}
  }

  //DisplayRefresh
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval){
    DisplayRefreshLastRun = currentMillis;
    DisplayMainUI();
    //DebugFakeValues();
  }

  //Check CAN Messages
  if (can.available()) {
    StatusIndicatorCAN = TFT_YELLOW;
    CanMessagesLastRecived = currentMillis;
    CANCheckMessage();
  }
  else if (CanError) {
    CanConnect();
  }
  else if ( CanMessagesLastRecived - currentMillis > 5000) { // If no CAN messages received for 5 seconds, set indicator to grey
    StatusIndicatorCAN =  TFT_DARKGREY;
  }
  else if (CanMessagesLastRecived - currentMillis > 3 * 60000) {  
    //SleepStart();
  }

  //Check for new trip recording
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

  // check if trip ends
  if ((canValues.Ready == 0 || canValues.Gear == "-") && TripActive) {
    TripActive = false;
    
    Serial.println("Trip Duration: " + String((Trip.endTime - Trip.startTime) / 1000 / 60) + " minutes");
    Serial.println("Trip Distance: " + String(Trip.endKM - Trip.startKM / 10, 1) + " km");
    Serial.println("Trip Max Speed: " + String(Trip.maxSpeed) + " km/h");
    Serial.println("Trip average Speed: " + String((float)(Trip.endKM - Trip.startKM) / ((Trip.endTime - Trip.startTime) / 1000 / 60)) + " km/h");
    Serial.println("Trip average Energy: " + String((float)(Trip.startSoC - Trip.endSoC) * 0,056 ) + " kWh");
    
    // Show Trip results
    DisplayTripResults();
    delay(15000); // Show Trip results for 15 seconds

    DisplayMainUI();
  }

  //Send Data  
  if (DataToSend && currentMillis - SendDataLastRun >= SendDataInterval && (canValues.Ready < 1 || canValues.Gear == "-")) //send in interval if ignition is off
  {
    SendDataLastRun = currentMillis;
    if (WIFICheckConnection()) {
      SendDataSimpleAPI();
    }
    else if (WIFIConnect()) {
        if (SendDataSimpleAPI()) { DataToSend = false; }
      }
  }
  else if (canValues.Speed > 1) { // deactivate tranismitting when driving
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
      switch (canMsg.id) {
        // Electronic control Unit (ECU)
        case 0x581: { 
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          // Odo
          unsigned int value1 = canMsg.data[6] << 16 | canMsg.data[5] << 8 | canMsg.data[4];
          //Serial.println("- CAN Value ODO: " + String(value1));
          canValues.ODO = (float)value1;

          // Speed 
          unsigned int value = canMsg.data[7];
          //Serial.println("- CAN Value Speed: " + String(value));          
          canValues.Speed = value;
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
          }
          break;

        // 12V Battery
        case 0x593: {
          StatusIndicatorCAN = TFT_BLUE;
          if (!canMsg.len == 8) {Serial.println("CAN: Wrong Lenght"); break;}
          unsigned int value = (canMsg.data[1] << 8) | canMsg.data[0];
          //Serial.println("- CAN Value 12V: " + String(value));
          canValues.Battery = (float)value / 100;
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

          //Temp 2
          int value2 = canMsg.data[3];
          //Serial.println("- CAN Value Temp2: " + String(value2));
          canValues.Temp2 = value2;
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

          //Voltage
          int value2 = (canMsg.data[3] << 8) | canMsg.data[2];
          //Serial.println("- CAN Value Voltage: " + String(value2));
          if (value2 > 4200 && value2 < 6500) { // Check is value is in valid range
            canValues.Volt = (float)value2 / 100;
          }
        
          // SoC
          unsigned int value3 = canMsg.data[5];
          //Serial.println("- CAN Value SoC: " + String(value3));
          canValues.SoC = value3;
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
          //Serial.println("- CAN Value Gear Selected Bits: " + String(value1, BIN));

          // Remaining Distance
          unsigned int value2 = canMsg.data[0] & 0x3F; // Mask to get the lower 6 bits
          //Serial.println("- CAN Value Remaining Distance2: " + String(value2));
          canValues.RemainingDistance = value2;
          
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
  Serial.println("WIFI disconnect");
  WiFi.disconnect();
  StatusIndicatorWIFI = TFT_DARKGREY;
}

void DisplayMainUI() {
  //tft.fillScreen(COLOR_BACKGROUND);
  
  //Consumption Background
  tft.drawSmoothArc(120,120, 121, 105, 150, 240, COLOR_BG_RED,COLOR_BACKGROUND, true);
  tft.drawSmoothArc(120,120, 121, 105, 120, 150, COLOR_BG_GREEN,COLOR_BACKGROUND, true);
  
  if (canValues.Current <0) {
    //Consumption negative
    int arcLenght = map(canValues.Current, -150, 0, 240, 150);
    tft.drawSmoothArc(120,120, 118, 107, 150, arcLenght, TFT_RED,COLOR_BACKGROUND, true);
  }
  else if (canValues.Current > 0) {
    //Consumption positive
    int arcLenght = map(canValues.Current, 0, 75, 0, 30);
    tft.drawSmoothArc(120,120, 118, 107, 150 - arcLenght, 150, TFT_DARKGREEN,COLOR_BACKGROUND, true);
  }
  else {
    // No Consumption
    tft.drawSmoothArc(120,120, 118, 107, 149, 150, TFT_DARKGREY,COLOR_BACKGROUND, true);
  }
 
  // Battery Temperature
  tft.drawSmoothArc(120,120, 121, 110, 45, 90, COLOR_ALMOSTBLACK,COLOR_BACKGROUND, true);
  float tempAverage = (canValues.Temp1 + canValues.Temp2) / 2.0;
  int arcLenght = map(tempAverage, -10, 50, 45, 90);
  if (arcLenght < 46) { arcLenght = 46;}
  tft.drawSmoothArc(120,120, 121, 110, 45, arcLenght, TFT_ORANGE,COLOR_BACKGROUND, true);
  tft.setTextSize(1);
  if (tempAverage > 45) { tft.setTextColor(TFT_RED); }
  else if (tempAverage < 5) { tft.setTextColor(TFT_BLUE); }
  else if (tempAverage < 20) { tft.setTextColor(TFT_YELLOW); }
  else { tft.setTextColor(TFT_DARKGREY); }
  tft.drawString(String(tempAverage,1)+ "C", 25, 143, 2); 

  // 12Volt Battery
  tft.drawSmoothArc(120,120, 121, 110, 270, 315, COLOR_ALMOSTBLACK, COLOR_BACKGROUND, true);
  arcLenght = map(canValues.Battery, 11.5, 14.5, 0, 45); 
  if (arcLenght < 1) {arcLenght = 1;}
  tft.drawSmoothArc(120,120, 121, 110, 315 - arcLenght, 315, TFT_BLUE, COLOR_BACKGROUND, true);
  tft.setTextSize(1);
  if (canValues.Battery < 11.5) { tft.setTextColor(TFT_RED); }
  else if (canValues.Battery < 12.0) { tft.setTextColor(TFT_YELLOW); }
  else { tft.setTextColor(TFT_DARKGREY); }
  tft.drawString(String(canValues.Battery, 1) + "V", 180, 143, 2); 

  // Consumption value  
  tft.fillSmoothRoundRect(40, 65, 160, 60, 10, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  int textstart = 55;
  if (canValues.Current >=10) { textstart = 75;}
  else if (canValues.Current >= 0) { textstart = 95;}
  else if (canValues.Current < -100 ) { textstart = 55;}
  else if (canValues.Current < -10) { textstart = 75;}
  else if (canValues.Current < 0) { textstart = 95;}
  tft.drawString(String(canValues.Current,1) + "A", textstart, 85);

  //Battery Voltage
  tft.fillSmoothRoundRect(85, 130, 70, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextSize(2);
  if (canValues.Volt < 51.2 || canValues.Volt > 58) { tft.setTextColor(TFT_ORANGE); } 
  else {tft.setTextColor(TFT_WHITE);}
  tft.drawString(String(canValues.Volt, 1) + "V", 90, 143);

  // SoC
  tft.setTextSize(2);
  if (canValues.SoC < 15) { tft.setTextColor(TFT_RED); }
  else if (canValues.SoC < 30) { tft.setTextColor(TFT_YELLOW); }
  else { tft.setTextColor(TFT_DARKGREY); }
  tft.drawString(String(canValues.SoC) + "%", 95, 40);

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

  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(2);
  tft.drawString("Diese Fahrt:",50, 23, 2);
  int positionX = 25;
  int positionY = 55;
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("Dauer", positionX +10 , positionY +2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  if (((Trip.endTime - Trip.startTime) / 1000 / 60) >= 10) {
    tft.drawString(String((Trip.endTime - Trip.startTime) / 1000 / 60) + " Min | " + String((Trip.endKM - Trip.startKM) / 10, 1) + "km", positionX +10, positionY +19);
  }
  else {
    tft.drawString(" " + String((Trip.endTime - Trip.startTime) / 1000 / 60) + " Min | " + String((Trip.endKM - Trip.startKM) / 10, 1) + "km", positionX +10, positionY +19);
  }
  positionY += 43;
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("km/h", positionX +10 , positionY +1, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString(String((((Trip.endKM - Trip.startKM) / 10) / (Trip.endTime - Trip.startTime) / 1000 / 60) * 60,1) + "   | " + String(Trip.maxSpeed, 0), positionX +10, positionY +19);
  tft.drawSmoothCircle(positionX +75, positionY +25, 7, TFT_WHITE, COLOR_BACKGROUND);
  tft.drawLine(positionX +67, positionY +32, positionX +83, positionY +18, TFT_WHITE);
  tft.fillTriangle(positionX +155, positionY +33, positionX +175, positionY +33, positionX +175, positionY +22, TFT_WHITE);
  positionY += 43;
  tft.fillSmoothRoundRect(positionX, positionY, 190, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.setTextSize(1);
  tft.drawString("kW/h", positionX +10 , positionY +0, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("  " + String(((Trip.startSoC - Trip.endSoC) * -1) *0.056, 1) + "  | " + String(((Trip.startSoC - Trip.endSoC) * -1) *0.056 / (Trip.endKM - Trip.startKM) / 10,1), positionX +10, positionY +19);
  tft.drawSmoothCircle(positionX +170, positionY +25, 7, TFT_WHITE, COLOR_BACKGROUND);
  tft.drawLine(positionX +162, positionY +33, positionX +178, positionY +18, TFT_WHITE);
  positionY += 43;
  tft.fillSmoothRoundRect(65, positionY, 110, 40, 5, COLOR_ALMOSTBLACK, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TOPOLINO);
  tft.drawString("Akku:", 72, positionY +13);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String((Trip.startSoC - Trip.endSoC) * -1) + "%", 132, positionY +13);
}

bool SendDataSimpleAPI() {
  Serial.println("Send Data via REST");
  StatusIndicatorTx = TFT_BLUE;
  bool result = false;

  HTTPClient http;
  // SimpleAPI set values bulk
  String URL = "http://10.0.1.51:8087/setBulk?user=" + String(YourSimpleAPI_User) + "&pass=" + YourSimpleAPI_Password;

  String DataURLEncoded = "";
  if (canValues.SoC >= 0 && canValues.SoC <= 100) { DataURLEncoded += "&0_userdata.0.topolino.SoC=" + String(canValues.SoC);}
  if (canValues.Battery >= 0 && canValues.Battery <= 15) { DataURLEncoded += "&0_userdata.0.topolino.12VBatt=" + String(canValues.Battery); }
  if (canValues.Current >= -100 && canValues.Current <= 200) { DataURLEncoded += "&0_userdata.0.topolino.BattA=" + String(canValues.Current); }
  if (canValues.Temp1 >= -20 && canValues.Temp1 <= 70) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp1=" + String(canValues.Temp1); }
  if (canValues.Temp2 >= -20 && canValues.Temp2 <= 70) { DataURLEncoded += "&0_userdata.0.topolino.BattTemp2=" + String(canValues.Temp2); }
  if (canValues.Volt >= 40 && canValues.Volt <= 60) { DataURLEncoded += "&0_userdata.0.topolino.BattV=" + String(canValues.Volt); }
  if (canValues.Handbrake == 0 || canValues.Handbrake == 1) { DataURLEncoded += "&0_userdata.0.topolino.Handbrake=" + String(canValues.Handbrake); }
  if (canValues.ODO >= 0 && canValues.ODO <= 1000000) { DataURLEncoded += "&0_userdata.0.topolino.ODO=" + String(canValues.ODO / 10); }
  if (canValues.OBCRemainingMinutes >= 0 ) { DataURLEncoded += "&0_userdata.0.topolino.OnBoardChargerRemaining=" + String(canValues.OBCRemainingMinutes); }
  if (canValues.Ready == 0 || canValues.Ready == 1) { DataURLEncoded += "&0_userdata.0.topolino.Ready=" + String(canValues.Ready); }
  if (canValues.RemainingDistance >= 0 && canValues.RemainingDistance <= 80) { DataURLEncoded += "&0_userdata.0.topolino.RemainingKM=" + String(canValues.RemainingDistance); }
  if (canValues.Gear != "") { DataURLEncoded += "&0_userdata.0.topolino.gear=" + String(canValues.Gear); }
  if (canValues.Speed >= 0 && canValues.Speed <= 50) { DataURLEncoded += "&0_userdata.0.topolino.speed=" + String(canValues.Speed); }
  DataURLEncoded += "&ack=true";

  Serial.print("Data: ");
  Serial.println(URL + DataURLEncoded); 
  http.begin(URL + DataURLEncoded);
  http.setTimeout(3 * 1000); // 3 seconds timeout
  http.setUserAgent("TopolinoInfoDisplay/1.0");
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    StatusIndicatorTx = TFT_GREEN;
    result = true;
  }
  else {
    StatusIndicatorTx = TFT_RED;
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return result; 
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

void SleepStart() {

  Serial.println("Going to sleep...");
  StatusIndicatorCAN = TFT_DARKGREY;
  StatusIndicatorStatus = TFT_DARKGREY;
  StatusIndicatorTx = TFT_DARKGREY;
  StatusIndicatorWIFI = TFT_DARKGREY;
  
  // Turn off display power
  digitalWrite(DISPLAY_POWER_PIN, LOW);

  // Go to deep sleep
  esp_deep_sleep_start();
}

float average( float newvalue, float &buffer, float factor)
{
  float avg ;
  buffer = buffer - (buffer / factor);
  buffer = buffer + newvalue;
  avg = buffer / factor;
  return avg;
}