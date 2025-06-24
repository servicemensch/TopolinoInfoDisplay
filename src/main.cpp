#include <Arduino.h>
#include <TFT_eSPI.h>
#include <mcp2515.h>
#include <SPI.h>

#define DISPLAY_POWER_PIN 15
#define DISPLAY_BACKLIGHT 38
#define DISPLAY_HIGHT 170
#define DISPLAY_WIDTH 320
#define CAN_INTERRUPT 3


TFT_eSPI tft = TFT_eSPI();
MCP2515 can(SS);     // Set CS of MCP2515_CAN module to Pin 10

unsigned long DisplayRefreshLastRun = 0;
const long DisplayRefreshInterval = 1000; //Milliseconds

// put function declarations here:
void DisplayRefresh();
bool CheckCanMessage();
void SetALIVEStatusInidcator(uint16_t color = TFT_RED){tft.fillCircle(85, 156, 8, color);}
void SetCANStatusInidcator(uint16_t color = TFT_RED) { tft.fillCircle(188, 156, 8, color); }

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

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

  tft.setTextColor(TFT_DARKCYAN);
  tft.setTextSize(2);
  // Titel
  tft.drawString("Topolino Info Display vDEV", 0, 0);
  tft.fillRectHGradient(0, 20, 320, 3, TFT_WHITE, TFT_RED);
  // Statusleiste
  tft.fillRectHGradient(0, 142, 320, 1, TFT_WHITE, TFT_RED);
  tft.drawString("Alive:", 0, 150);
  SetALIVEStatusInidcator();
  tft.drawString("CAN:", 130, 150);
  SetCANStatusInidcator();
  
  //Ganganzeige
  tft.fillRect(265, 23, 1, 77, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.drawString("Gear:", 270, 25);
  tft.setTextSize(7);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("N", 275, 42);

  // Stromabnahme
  tft.fillRect(0, 100, 320, 1, TFT_DARKGREY);
  tft.fillRect(110, 100, 2, 42, TFT_WHITE);
  tft.fillRectHGradient(10, 106, 100, 30, TFT_DARKGREEN, TFT_GREEN);
  tft.fillRectHGradient(112, 106, 200, 30, TFT_YELLOW, TFT_RED);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("-10,2 kW", 130, 110);

  // 12V Spannung
  // 45V Temp
  // 45V SoC

  // Text Defaults
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);

  // CAN Module
  can.reset();
  can.setBitrate(CAN_500KBPS);

  if ( can.getStatus() ) {
    Serial.println("CAN-Module Initialized Successfully!");
    can.setListenOnlyMode();
    SetCANStatusInidcator(TFT_DARKGREY);
    } 
  else {
    Serial.println("Error Initializing CAN-Module...");
    }
}


void loop() {
  unsigned long currentMillis = millis();
  Serial.println(" - Tick: " + String(currentMillis));
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval){
    DisplayRefreshLastRun = currentMillis;
    DisplayRefresh();
  }

  CheckCanMessage();

  //Debug
  tft.fillRect(0, 30, 260, 60, TFT_DARKGREY);
  tft.drawString(String(currentMillis), 0, 30);
  tft.drawString(String(can.getStatus()), 0, 50);
 
  delay(250); //loop delay to 
}

// put function definitions here:
void DisplayRefresh(){
  Serial.println("Display Refresh"); 
  //Statusleiste
  if (tft.readPixel(85, 156) == 58623) {SetALIVEStatusInidcator(TFT_GREEN);} else {SetALIVEStatusInidcator(TFT_WHITE);}
}

bool CheckCanMessage(){
  Serial.println("Check CAN message"); 
  struct can_frame canMsg;
  if ( can.readMessage(&canMsg) == MCP2515::ERROR_OK ){
    Serial.println("CAN Message recived - ID: " + String(canMsg.can_id)); 
    Serial.println("CAN Message recived - DLC: " + String(canMsg.can_dlc));
    Serial.println("CAN Message recived - Data: " + String(canMsg.data[0]) + " " + String(canMsg.data[1]) + " " + String(canMsg.data[2]) + " " + String(canMsg.data[3]) + " " + String(canMsg.data[4]) + " " + String(canMsg.data[5]) + " " + String(canMsg.data[6]) + " " + String(canMsg.data[7]) + " " + String(canMsg.data[8]));
    tft.drawString(String(canMsg.can_id), 0, 70);
    tft.drawString(String(canMsg.can_dlc), 100, 70);
    
    switch (canMsg.can_id) {
      case 0x581:
        Serial.println("CAN: ODO");
        break;
      case 0x582:
        Serial.println("CAN: Reamining");
        break;
      case 0x593:
        Serial.println("CAN: 12V Batt Voltage");
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

    //Statusleiste
    if (tft.readPixel(188, 156) == 58623) {SetCANStatusInidcator(TFT_GREEN);} else {SetCANStatusInidcator(TFT_WHITE);}
    return true;
  }
  else {
    // Serial.println("no CAN Message found");
    return false;
    }
}