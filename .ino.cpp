#ifdef __IN_ECLIPSE__
//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2018-01-27 23:25:44

#include "Arduino.h"
#include <U8x8lib.h>
#define DEBUG_ESP_WIFI
#define DEBUG_ESP_PORT Serial
#define DEBUG(...) {}
#include "Arduino.h"
#include <ConfigItem.h>
#include <EEPROMConfig.h>
#define OTA
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <SPIFFSEditor.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncHTTPClient.h>
#include <ESPAsyncWiFiManager.h>
#include <Wire.h>
#include <TimeLib.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_MCP23017.h>
#include <HV5523InvNixieDriver.h>
#include <OneNixieClock.h>
byte getNormalizedBrightness() ;
void StartOTA() ;
void mainHandler(AsyncWebServerRequest *request) ;
void sendFavicon(AsyncWebServerRequest *request) ;
void broadcastUpdate(const BaseConfigItem& item) ;
void sendInfoValues(AsyncWebSocketClient *client) ;
void sendClockValues(AsyncWebSocketClient *client) ;
void sendLEDValues(AsyncWebSocketClient *client) ;
void sendExtraValues(AsyncWebSocketClient *client) ;
void sendPresetValues(AsyncWebSocketClient *client) ;
void sendPresetNames(AsyncWebSocketClient *client) ;
void updateValue(int screen, String pair) ;
void handleWSMsg(AsyncWebSocketClient *client, char *data) ;
void wsHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) ;
void grabInts(String s, int *dest, String sep) ;
void grabBytes(String s, byte *dest, String sep) ;
void readTimeFailed(String msg) ;
void setTimeFromInternet() ;
void setLedColorHSV(byte h, byte s, byte v) ;
void ledTimerHandler() ;
int getHVDutyCycle(byte vout) ;
void chrenInterrupt() ;
void chr0Interrupt() ;
void chr1Interrupt() ;
void initFromEEPROM() ;
void SetupServer() ;
void eepromUpdate() ;
void getTime() ;
void setup() ;
void loop() ;

#include "SingleTubeClock.ino"


#endif
