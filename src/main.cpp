#include <Arduino.h>
#include <TFT_eSPI.h>

#define DISPLAY_POWER_PIN 15
#define DISPLAY_BACKLIGHT 38
#define DISPLAY_HIGHT 170
#define DISPLAY_WIDTH 320

TFT_eSPI tft = TFT_eSPI();
int i = 1;
bool alive = 0;

// put function declarations here:
void AliveIcon();

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
  tft.drawString("Topolino Info Display v0.0", 0, 0);
  tft.fillRectHGradient(0, 20, 320, 3, TFT_WHITE, TFT_RED);
  // Statusleiste
  tft.fillRectHGradient(0, 142, 320, 1, TFT_WHITE, TFT_RED);
  tft.drawString("Alive:", 0, 150);
  tft.drawCircle(85, 156, 8, TFT_RED);
  tft.drawString("CAN:", 130, 150);
  tft.drawCircle(188, 156, 8, TFT_RED);
  
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

}


void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("Still alive.");    // Print a label
  // tft.fillRect(0,30,100,20,TFT_BLACK);
  // tft.drawString(String(i), 0, 30);
  // i++;
  AliveIcon();
  delay(1000);
}

// put function definitions here:
void AliveIcon(){
  if (alive) {
    tft.fillCircle(85, 156, 8,  TFT_GREEN);
    alive = false;
    }
  else {
    tft.fillCircle(85, 156, 8, TFT_WHITE);
    alive = true;
    }

}
