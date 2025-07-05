# Topolino Info Display
![default screen](info\default_screen.jpg)

## Hardware:
- [LilyGo T-Display S3](https://lilygo.cc/products/t-display-s3?variant=42284559827125) (ESP32 + 1,9" 8Bit LCD Screen)
- [MCP 2515 Can Bus module](https://www.az-delivery.de/products/mcp2515-can-bus-modul-1)
- 3D printed OBD2 plug

### Wireing:
![fizing diagram](info/Wireing.png)

|  | T-Display S3 | MCP2515_CAN |
|-|-|-|
| Power 5V | 5V | VCC |
| Ground | GND | GND |
| Chip select | PIN 10 | CS|
| SPI MISO | PIN 13 | SO |
| SPI MOSI | PIN 11 | SI |
| Serial clock | PIN 12 | SCK |
| Interrupt | PIN 3 | INT |

## Software:
Build as PlatformIO project in Visual Studio Code.

### Libraries:
- [TFT_sSPI](https://doc-tft-espi.readthedocs.io/) - Take care to select the right display un User_Setup_Select.h
- [ACAN2515](https://github.com/pierremolinaro/acan2515/tree/master)

## Confuguration
Rename ./config/config_example.h to config.h and fill the values
