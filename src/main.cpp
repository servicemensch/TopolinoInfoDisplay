#include <Arduino.h>
#include <TFT_eSPI.h>
//#include <mcp2515.h>
//#include <SPI.h>

#define DISPLAY_POWER_PIN 15
#define DISPLAY_BACKLIGHT 38
#define DISPLAY_HIGHT 170
#define DISPLAY_WIDTH 320


TFT_eSPI tft = TFT_eSPI();
//MCP2515 can(13);     // Set CS of MCP2515_CAN module to Pin 13

unsigned long DisplayRefreshLastRun = 0;
const long DisplayRefreshInterval = 1000; //Milliseconds

// put function declarations here:
void DisplayRefresh();
void SetCANStatusInidcator(uint16_t color = TFT_RED){tft.drawCircle(188, 156, 8, color);}
void SetALIVEStatusInidcator(uint16_t color = TFT_RED){tft.drawCircle(85, 156, 8, color);}

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
  tft.drawString("-10,2 kW", 130, 110);

  // 12V Spannung
  // 45V Temp
  // 45V SoC

  //Defaults
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);

  // CAN Module
  /* can.reset();
  can.setBitrate(CAN_500KBPS);

  if ( can.getStatus() ) {
    Serial.println("CAN-Module Initialized Successfully!");
    can.setListenOnlyMode();
    SetCANStatusInidcator(TFT_DARKGREY);
    } 
  else {
    Serial.println("Error Initializing CAN-Module...");
    } */
}


void loop() {
  unsigned long currentMillis = millis();
  Serial.println("Still alive - " + currentMillis);    // Print a label
  if (currentMillis - DisplayRefreshLastRun >= DisplayRefreshInterval){
    DisplayRefreshLastRun = currentMillis;
    DisplayRefresh();
  }
  delay(25); //loop delay to 
}

// put function definitions here:
void DisplayRefresh(){
  Serial.println("Display Refresh - "); 
  //Statusleiste
  //if (tft.readPixel(85, 156) == TFT_WHITE) {SetALIVEStatusInidcator(TFT_GREEN);}
  //else {SetALIVEStatusInidcator(TFT_WHITE);}


}
