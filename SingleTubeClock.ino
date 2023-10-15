#define BQ24392
#define I2C
#ifndef I2C
Print *debugPrint = &Serial;
#define DEBUG(...) { Serial.println(__VA_ARGS__); }
#else
#define DEBUG(...) {}
#endif

#include "Arduino.h"
#include <ConfigItem.h>					// https://github.com/judge2005/Configs
#include <EEPROMConfig.h>				// https://github.com/judge2005/Configs
#define OTA
#ifdef OTA
#include "ASyncOTAWebUpdate.h"			// https://github.com/judge2005/ASyncOTAWebUpdate
#endif
#include <EEPROM.h>
#include <SPIFFSEditor.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>				// https://github.com/me-no-dev/ESPAsyncTCP
#ifdef USE_EADNS
#include <ESPAsyncDNSServer.h>			// https://github.com/devyte/ESPAsyncDNSServer
#else
#include <DNSServer.h>
#endif
#include <ESPAsyncWebServer.h>			// https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncJson.h>					//                    "
#include <ESP8266HTTPClient.h>
#include <AsyncWiFiManager.h>			// https://github.com/judge2005/AsyncWiFiManager
#include <Wire.h>

#include "time.h"
#include <TimeLib.h>					// https://github.com/judge2005/Time

#ifdef I2C
#ifdef notdef
#include <MCP23017Button.h>				// https://github.com/judge2005/NixieMisc
#include <MCP23017RotaryEncoder.h>		//                    "
#endif
#endif

#include <SafeMCP23017.h>				// https://github.com/judge2005/UPS
#include <SafeLIS3DH.h>					//                    "
#ifdef notdef
#include <SafeOLED.h>					//                    "
#endif
#include <UPS.h>						//                    "

#include <HV5523NixieClockMultiplex.h>	// https://github.com/judge2005/NixieDriver
#include <HV5523InvNixieDriver.h>		//                    "
#include <HV5523Inv15SegNixieDriver.h>	//                    "
#ifdef SEVEN_SEG
#include <SN74HC595_7SegNixieDriver.h>	//                    "
#endif
#include <OneNixieClock.h>				// https://github.com/judge2005/OneNixieClock
#include <FourNixieClock.h>				//                    "
#include <LEDs.h>						// https://github.com/judge2005/NixieMisc
#include <LDR.h>						//                    "
#include <SoftMSTimer.h>				//                    "
#ifdef BQ24392
#include <BQ24392USBRating.h>			//                    "
#else
#include <CP2102NUSBRating.h>			//                    "
#endif
#include <MovementSensor.h>				//                    "
#include <Vadj8266.h>					//                    "
#include <PixelWave.h>					//                    "
#include <BlankTimeMonitor.h>			//                    "
#include "TimeServerTimeSync.h"			// https://github.com/judge2005/TimeSync

#include <WSHandler.h>					// https://github.com/judge2005/NixieMisc
#include <WSMenuHandler.h>				//                    "
#include <WSConfigHandler.h>			//                    "
#include <WSPresetValuesHandler.h>		//                    "
#include <WSPresetNamesHandler.h>		//                    "
#include <WSInfoHandler.h>				//                    "
#include <WSUPSHandler.h>				//                    "

// ESP8285 pins
const byte LEDpin = 0;	// D3, has built-in 10k pull up, but we don't care.
const byte CHRENpin = 5;
const byte CHR0pin = 10, CHG_ALpin = 10;
const byte CHR1pin = 9, CHG_DETpin = 9;
const byte VENpin = 2; // Add pullup
const byte VADJpin = 4;	//
const byte DIpin = 13;	// MOSI, D7
const byte CLpin = 14;	// SCK, D5
const byte LEpin = 12;	// MISO, D6
const byte MovPin = 16;	// PIR/Radar etc.

const byte INpin = A0;

// Expansion pins
#ifdef I2C
const byte SCLpin = 1;	// aka TXD
const byte SDApin = 3;	// aka RXD

const byte EXPANSION_ADDRESS = 0x20;
const byte ACCELEROMETER_ADDRESS = 0x18;
const byte OLED_ADDRESS = 0x78;

const byte MCP_BUTTONpin = 3;
const byte MCP_ENCODERApin = 4;
const byte MCP_ENCODERBpin = 5;
#endif

WiFiUDP syncBus;

#ifdef notdef
SafeOLED oled;
#endif
SafeLIS3DH lis;
SafeMCP23017 mcp;
UPS ups(mcp);
MovementSensor mov(MovPin);
BlankTimeMonitor blankingMonitor;
Vadj8266 vadj(VADJpin);

unsigned long nowMs = 0;

char *revision="$Rev: 636 $";

String chipId = String(ESP.getChipId(), HEX);
String ssid = "STC-";

StringConfigItem hostName("hostname", 63, "SingleTubeClock");

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
TimeSync *timeSync;
#ifdef USE_EADNS
AsyncDNSServer dns;
#else
DNSServer dns;
#endif
AsyncWiFiManager wifiManager(&server,&dns);
#ifdef OTA
ASyncOTAWebUpdate otaUpdater(Update, "update", "secretsauce");
#endif

HV5523NixieClockMultiplex multiplexDriver(LEpin, 4);
HV5523Inv15SegNixieDriver fifteenSegDriver(LEpin);
#ifdef SEVEN_SEG
SN74HC595_7SegNixieDriver sevenSegDriver(LEpin);
#endif
HV5523InvNixieDriver nixieDriver(LEpin);
NixieDriver *pDriver = &nixieDriver;
OneNixieClock oneNixieClock(pDriver);
FourNixieClock fourNixieClock(pDriver);

NixieClock *pNixieClock = &oneNixieClock;

//const int numLEDs = 8;
const int numLEDs = 18;

int clicks = 0;
int rotation = 0;

#ifndef BQ24392
CP2102NUSBRating usbRating(CHRENpin, CHR0pin, CHR1pin);
#else
BQ24392USBRating usbRating(CHG_ALpin, CHG_DETpin);
#endif

#define PIN_RESET 255  //
#define DC_JUMPER 0  // I2C Address: 0 - 0x3C, 1 - 0x3D

namespace ConfigSet1 {
#include <ConfigSet1.h>
}

namespace ConfigSet2 {
#include <ConfigSet2.h>
}	// End namespace

namespace ConfigSet3 {
#include <ConfigSet3.h>
}	// End namespace

//namespace ConfigSet4 {
//#include <ConfigSet4.h>
//}	// End namespace
//
//namespace ConfigSet5 {
//#include <ConfigSet5.h>
//}	// End namespace

StringConfigItem set1Name("set1_name", 12, "Hue Cycling");
StringConfigItem set2Name("set2_name", 12, "Fixed Backlight");
StringConfigItem set3Name("set3_name", 12, "Fast Clock");
//StringConfigItem set4Name("set4_name", 12, "Conditioner");
//StringConfigItem set5Name("set5_name", 12, "Manual");
BaseConfigItem *configSetPresetNames[] = {
	&set1Name,
	&set2Name,
	&set3Name,
//	&set4Name,
//	&set5Name,
	0
};

CompositeConfigItem presetNamesConfig("preset_names", 0, configSetPresetNames);

StringConfigItem currentSet("current_set", 4, "set1");

ByteConfigItem tube_type("tube_type", 0);

BaseConfigItem *configSetGlobal[] = {
	&hostName,
	&currentSet,
	&tube_type,
	0
};

CompositeConfigItem globalConfig("global", 0, configSetGlobal);

BaseConfigItem *configSetRoot[] = {
	&globalConfig,
	&presetNamesConfig,
	&ConfigSet1::config,
	&ConfigSet2::config,
	&ConfigSet3::config,
//	&ConfigSet4::config,
//	&ConfigSet5::config,
	0
};

CompositeConfigItem rootConfig("root", 0, configSetRoot);

EEPROMConfig config(rootConfig);

void infoCallback();
String wifiCallback();

namespace CurrentConfig {
	String name("set1");
	CompositeConfigItem *config = &ConfigSet1::config;

	// Clock config values
	BooleanConfigItem *time_or_date = &ConfigSet1::time_or_date;
	ByteConfigItem *date_format = &ConfigSet1::date_format;
	BooleanConfigItem *time_format = &ConfigSet1::time_format;
	BooleanConfigItem *hour_format = &ConfigSet1::hour_format;
	ByteConfigItem *fading = &ConfigSet1::fading;
	ByteConfigItem *indicator = &ConfigSet1::indicator;
	BooleanConfigItem *scrollback = &ConfigSet1::scrollback;
	IntConfigItem *digits_on = &ConfigSet1::digits_on;
	ByteConfigItem *display_on = &ConfigSet1::display_on;
	ByteConfigItem *display_off = &ConfigSet1::display_off;
	StringConfigItem *time_url = &ConfigSet1::time_url;

	// LED config values
	ByteConfigItem *hue = &ConfigSet1::hue;
	ByteConfigItem *saturation = &ConfigSet1::saturation;
	BooleanConfigItem *backlight = &ConfigSet1::backlight;
	BooleanConfigItem *hue_cycling = &ConfigSet1::hue_cycling;
	ByteConfigItem *led_scale = &ConfigSet1::led_scale;
	IntConfigItem *cycle_time = &ConfigSet1::cycle_time;
	BooleanConfigItem *sec_hue = &ConfigSet1::sec_hue;
	BooleanConfigItem *sec_sat = &ConfigSet1::sec_sat;
	BooleanConfigItem *sec_val = &ConfigSet1::sec_val;

	// Extra config values
	BooleanConfigItem *dimming = &ConfigSet1::dimming;
	BooleanConfigItem *display = &ConfigSet1::display;
	BooleanConfigItem *hv = &ConfigSet1::hv;
	ByteConfigItem *voltage = &ConfigSet1::voltage;
	IntConfigItem *digit = &ConfigSet1::digit;
	ByteConfigItem *count_speed = &ConfigSet1::count_speed;
	StringConfigItem *pin_order = &ConfigSet1::pin_order;
	IntConfigItem *pwm_freq = &ConfigSet1::pwm_freq;
	ByteConfigItem *mov_delay = &ConfigSet1::mov_delay;
	ByteConfigItem *mov_src = &ConfigSet1::mov_src;

	// UPS config values
	ByteConfigItem *charge_rate = &ConfigSet1::charge_rate;
	BooleanConfigItem *lpm = &ConfigSet1::lpm;
	ByteConfigItem *wakeup_time = &ConfigSet1::wakeup_time;
	ByteConfigItem *sensitivity = &ConfigSet1::sensitivity;

	// Sync config values
	IntConfigItem *sync_port = &ConfigSet1::sync_port;
	ByteConfigItem *sync_role = &ConfigSet1::sync_role;

	void setCurrent(const String &name) {
		if (CurrentConfig::name == name) {
			return;	// Already set to this
		}

		BaseConfigItem *newConfig = rootConfig.get(name.c_str());

		if (newConfig) {
			DEBUG("Changing preset to:");
			DEBUG(name);
			CurrentConfig::name = name;
			config = static_cast<CompositeConfigItem*>(newConfig);

			/*
			 * I hate doing this.
			 */

			// Clock config values
			time_or_date = static_cast<BooleanConfigItem*>(config->get("time_or_date"));
			date_format = static_cast<ByteConfigItem*>(config->get("date_format"));
			time_format = static_cast<BooleanConfigItem*>(config->get("time_format"));
			hour_format = static_cast<BooleanConfigItem*>(config->get("hour_format"));
			fading = static_cast<ByteConfigItem*>(config->get("fading"));
			indicator = static_cast<ByteConfigItem*>(config->get("indicator"));
			scrollback = static_cast<BooleanConfigItem*>(config->get("scrollback"));
			digits_on = static_cast<IntConfigItem*>(config->get("digits_on"));
			display_on = static_cast<ByteConfigItem*>(config->get("display_on"));
			display_off = static_cast<ByteConfigItem*>(config->get("display_off"));
			time_url = static_cast<StringConfigItem*>(config->get("time_url"));

			// LED config values
			hue = static_cast<ByteConfigItem*>(config->get("hue"));
			saturation = static_cast<ByteConfigItem*>(config->get("saturation"));
			backlight = static_cast<BooleanConfigItem*>(config->get("backlight"));
			hue_cycling = static_cast<BooleanConfigItem*>(config->get("hue_cycling"));
			led_scale = static_cast<ByteConfigItem*>(config->get("led_scale"));
			cycle_time = static_cast<IntConfigItem*>(config->get("cycle_time"));
			sec_hue = static_cast<BooleanConfigItem*>(config->get("sec_hue"));
			sec_sat = static_cast<BooleanConfigItem*>(config->get("sec_sat"));
			sec_val = static_cast<BooleanConfigItem*>(config->get("sec_val"));

			// Extra config values
			dimming = static_cast<BooleanConfigItem*>(config->get("dimming"));
			display = static_cast<BooleanConfigItem*>(config->get("display"));
			hv = static_cast<BooleanConfigItem*>(config->get("hv"));
			voltage = static_cast<ByteConfigItem*>(config->get("voltage"));
			digit = static_cast<IntConfigItem*>(config->get("digit"));
			count_speed = static_cast<ByteConfigItem*>(config->get("count_speed"));
			pin_order = static_cast<StringConfigItem*>(config->get("pin_order"));
			pwm_freq = static_cast<IntConfigItem*>(config->get("pwm_freq"));
			mov_delay = static_cast<ByteConfigItem*>(config->get("mov_delay"));
			mov_src = static_cast<ByteConfigItem*>(config->get("mov_src"));

			// UPS config values
			charge_rate = static_cast<ByteConfigItem*>(config->get("charge_rate"));
			lpm = static_cast<BooleanConfigItem*>(config->get("lpm"));
			wakeup_time = static_cast<ByteConfigItem*>(config->get("wakeup_time"));
			sensitivity = static_cast<ByteConfigItem*>(config->get("sensitivity"));

			// Sync config values
			sync_port = static_cast<IntConfigItem*>(config->get("sync_port"));
			sync_role = static_cast<ByteConfigItem*>(config->get("sync_role"));

			BaseConfigItem *currentSetName = rootConfig.get("current_set");
			currentSetName->fromString(name);
			currentSetName->put();
		}
	}
}

void writeSyncBus(char msg[]) {
	IPAddress broadcastIP(~((uint32_t)WiFi.subnetMask()) | ((uint32_t)WiFi.gatewayIP()));
	syncBus.beginPacket(broadcastIP, *CurrentConfig::sync_port);
	syncBus.write(msg);
	syncBus.endPacket();
}

void sendSyncMsg() {
	static char syncMsg[10] = "sync:";
	if (*CurrentConfig::sync_role == 1) {
		itoa(*CurrentConfig::hue, &syncMsg[5], 10);
		writeSyncBus(syncMsg);
		pNixieClock->syncDisplay();
		*CurrentConfig::digit = pNixieClock->getNixieDigit();
	}
}

void sendMovMsg() {
	static char syncMsg[10] = "mov";
	static unsigned long lastSend = 0;

	// send at most 10 notifications/sec
	if (*CurrentConfig::sync_role == 1 && (nowMs - lastSend >= 100)) {
		lastSend = nowMs;

		writeSyncBus(syncMsg);
	}
}

void announceSlave() {
	static char syncMsg[] = "slave";
	if (*CurrentConfig::sync_role == 2) {
		writeSyncBus(syncMsg);
	}
}

void readSyncBus() {
	static char incomingMsg[10];

	int size = syncBus.parsePacket();

	if (size) {
		int len = syncBus.read(incomingMsg, 9);
		if (len > 0 && len < 10) {
			incomingMsg[len] = 0;

			if (strncmp("sync", incomingMsg, 4) == 0) {
				if (strlen(incomingMsg) > 5) {
					byte hue = atoi(&incomingMsg[5]);
					*CurrentConfig::hue = hue;
				}
				pNixieClock->syncDisplay();
				*CurrentConfig::digit = pNixieClock->getNixieDigit();
				return;
			}

			if (strncmp("mov", incomingMsg, 3) == 0) {
				mov.trigger();
			}

			if (*CurrentConfig::sync_role == 1 && strcmp("slave", incomingMsg) == 0) {
				// A new slave just joined, broadcast a sync message
				sendSyncMsg();
			}
		}
	}
}

void syncBusLoop() {
	static byte currentRole = 255;

	if (*CurrentConfig::sync_role != 0) {
		// We are a master or slave
		// If port has changed, set new port and announce status
		if (syncBus.localPort() != *CurrentConfig::sync_port) {
			syncBus.begin(*CurrentConfig::sync_port);
			currentRole = 255;
		}
	} else {
		// We aren't in a sync group
		currentRole = 0;
		syncBus.stop();
	}

	if (currentRole != *CurrentConfig::sync_role) {
		currentRole = *CurrentConfig::sync_role;
		if (currentRole == 1) {
			sendSyncMsg();
		} else if (currentRole == 2) {
			announceSlave();
		}
	}

	readSyncBus();
}

void initClock() {
	switch (tube_type) {
	case 1:
		pNixieClock = &oneNixieClock;
		pDriver = &fifteenSegDriver;
		oneNixieClock.setScrollBackDelay(50);
		break;
	case 2:
		pNixieClock = &fourNixieClock;
		pDriver = &multiplexDriver;
		break;
#ifdef SEVEN_SEG
	case 3:
		pNixieClock = &fourNixieClock;
		pDriver = &sevenSegDriver;
		break;
#endif
	default:
		pNixieClock = &oneNixieClock;
		pDriver = &nixieDriver;
		oneNixieClock.setScrollBackDelay(40);
		break;
	}

	pDriver->init();
	pNixieClock->setNixieDriver(pDriver);
	pNixieClock->init();
}

SoftMSTimer::TimerInfo ledTimer = {
		60000,
		0,
		true,
		ledTimerHandler
};

SoftMSTimer::TimerInfo ledSecTimer = {
		200,
		0,
		true,
		ledSecTimerHandler
};

SoftMSTimer::TimerInfo eepromUpdateTimer = {
		60000,
		0,
		true,
		eepromUpdate
};

SoftMSTimer::TimerInfo snoozeTimer = {
		20000,
		0,
		false,
		snoozeUpdate
};

SoftMSTimer::TimerInfo syncTimer = {
		3600000L,
		0,
		true,
		sendSyncMsg
};

SoftMSTimer::TimerInfo syncTimeTimer = {
		3600000,	// 1 hour between syncs
		0,
		true,
		getTime
};

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got request")
	request->send(SPIFFS, "/index.html");
}

void sendFavicon(AsyncWebServerRequest *request) {
	DEBUG("Got favicon request")
	request->send(SPIFFS, "/assets/favicon-32x32.png", "image/png");
}

void broadcastUpdate(const BaseConfigItem& item) {
	const size_t bufferSize = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(20);	// 20 should be plenty
#ifdef JSON5
	DynamicJsonBuffer jsonBuffer(bufferSize);
	JsonObject& root = jsonBuffer.createObject();

	root["type"] = "sv.update";

	JsonObject &value = root.createNestedObject("value");
	String rawJSON = item.toJSON();	// This object needs to hang around until we are done serializing.
	value[item.name] = ArduinoJson::RawJson(rawJSON.c_str());

    size_t len = root.measureLength();
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
        root.printTo((char *)buffer->get(), len + 1);
    	ws.textAll(buffer);
    }

#else
	DynamicJsonDocument doc(bufferSize);
	JsonObject root = doc.to<JsonObject>();

	root["type"] = "sv.update";

	JsonVariant value = root.createNestedObject("value");
	String rawJSON = item.toJSON();	// This object needs to hang around until we are done serializing.
	value[item.name] = serialized(rawJSON.c_str());

    size_t len = measureJson(root);
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
    	serializeJson(root, (char *)buffer->get(), len + 1);
    	ws.textAll(buffer);
    }

#endif
}

WSConfigHandler wsClockHandler(rootConfig, "clock");
WSConfigHandler wsLEDHandler(rootConfig, "leds");
WSConfigHandler wsExtraHandler(rootConfig, "extra");
WSConfigHandler wsSyncHandler(rootConfig, "sync", wifiCallback);
WSPresetValuesHandler wsPresetValuesHandler(rootConfig);
WSInfoHandler wsInfoHandler(infoCallback);
WSPresetNamesHandler wsPresetNamesHandler(rootConfig);
WSUPSHandler wsUPSHandler(rootConfig, "ups", ups, usbRating);

String wifiCallback() {
	String wifiStatus = "\"wifi_ap\":";

	if (wifiManager.isAP()) {
		wifiStatus += "true";
	} else {
		wifiStatus += "false";
	}

	return wifiStatus;
}

void infoCallback() {
	wsInfoHandler.setSsid(ssid);
	wsInfoHandler.setBlankingMonitor(&blankingMonitor);
	wsInfoHandler.setRevision(revision);
	TimeSync::SyncStats &syncStats = timeSync->getStats();

	wsInfoHandler.setFailedCount(syncStats.failedCount);
	wsInfoHandler.setLastFailedMessage(syncStats.lastFailedMessage);
	wsInfoHandler.setLastUpdateTime(syncStats.lastUpdateTime);
}

String *ups_items[] = {
	&WSMenuHandler::clockMenu,
	&WSMenuHandler::ledsMenu,
	&WSMenuHandler::extraMenu,
	&WSMenuHandler::syncMenu,
	&WSMenuHandler::upsMenu,
	&WSMenuHandler::presetsMenu,
	&WSMenuHandler::infoMenu,
	&WSMenuHandler::presetNamesMenu,
	0
};

String *items[] = {
	&WSMenuHandler::clockMenu,
	&WSMenuHandler::ledsMenu,
	&WSMenuHandler::extraMenu,
	&WSMenuHandler::syncMenu,
	&WSMenuHandler::presetsMenu,
	&WSMenuHandler::infoMenu,
	&WSMenuHandler::presetNamesMenu,
	0
};

WSMenuHandler wsMenuHandler(items);

// Order of this needs to match the numbers in WSMenuHandler.cpp
WSHandler *wsHandlers[] = {
	&wsMenuHandler,
	&wsClockHandler,
	&wsLEDHandler,
	&wsExtraHandler,
	&wsPresetValuesHandler,
	&wsInfoHandler,
	&wsPresetNamesHandler,
	&wsUPSHandler,
	&wsSyncHandler
};

void updateValue(int screen, String pair) {
	int index = pair.indexOf(':');
	DEBUG(pair)
	// _key has to hang around because key points to an internal data structure
	String _key = pair.substring(0, index);
	const char* key = _key.c_str();
	String value = pair.substring(index+1);
	if (screen == 4) {
		CurrentConfig::setCurrent(value);
		StringConfigItem temp(key, 10, value);
		broadcastUpdate(temp);
	} else if (screen == 6) {
		BaseConfigItem *item = rootConfig.get(key);
		if (item != 0) {
			item->fromString(value);
			item->put();
			broadcastUpdate(*item);
		}
	} else {
		if (strcmp(key, "tube_type") == 0) {
			BaseConfigItem *item = rootConfig.get(key);
			if (item != 0) {
				item->fromString(value);
				item->put();
				initClock();
				broadcastUpdate(*item);
			}
		} else {
			BaseConfigItem *item = CurrentConfig::config->get(key);
			if (item != 0) {
				item->fromString(value);
				item->put();
				// Shouldn't special case this stuff. Should attach listeners to the config value!
				// TODO: This won't work if we just switch change sets instead!
				if (strcmp(key, CurrentConfig::time_url->name) == 0) {
					timeSync->setTz(value);
					timeSync->sync();
				}
				broadcastUpdate(*item);
			} else if (_key == "sync_do") {
				sendSyncMsg();
				announceSlave();
			} else if (_key == "wifi_ap") {
				setWiFiAP(value == "true" ? true : false);
			}
		}
	}
}

/*
 * Handle application protocol
 */
void handleWSMsg(AsyncWebSocketClient *client, char *data) {
	String wholeMsg(data);
	int code = wholeMsg.substring(0, wholeMsg.indexOf(':')).toInt();

	if (code < 9) {
		wsHandlers[code]->handle(client, data);
	} else {
		String message = wholeMsg.substring(wholeMsg.indexOf(':')+1);
		int screen = message.substring(0, message.indexOf(':')).toInt();
		String pair = message.substring(message.indexOf(':')+1);
		updateValue(screen, pair);
	}
}

/*
 * Handle transport protocol
 */
void wsHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
	//Handle WebSocket event
	switch (type) {
	case WS_EVT_CONNECT:
		DEBUG("WS connected")
		;
		break;
	case WS_EVT_DISCONNECT:
		DEBUG("WS disconnected")
		;
		break;
	case WS_EVT_ERROR:
		DEBUG("WS error")
		;
		DEBUG((char* )data)
		;
		break;
	case WS_EVT_PONG:
		DEBUG("WS pong")
		;
		break;
	case WS_EVT_DATA:	// Yay we got something!
		DEBUG("WS data")
		;
		AwsFrameInfo * info = (AwsFrameInfo*) arg;
		if (info->final && info->index == 0 && info->len == len) {
			//the whole message is in a single frame and we got all of it's data
			if (info->opcode == WS_TEXT) {
				DEBUG("WS text data");
				data[len] = 0;
				handleWSMsg(client, (char *) data);
			} else {
				DEBUG("WS binary data");
			}
		} else {
			DEBUG("WS data was split up!");
		}
		break;
	}
}

#ifdef OTA
void sendUpdateForm(AsyncWebServerRequest *request) {
	request->send(SPIFFS, "/update.html");
}

void sendUpdatingInfo(AsyncResponseStream *response, boolean hasError) {
    response->print("<html><head><meta http-equiv=\"refresh\" content=\"10; url=/\"></head><body>");

    hasError ?
    		response->print("Update failed: please wait while the device reboots") :
    		response->print("Update OK: please wait while the device reboots");

    response->print("</body></html>");
}
#endif

void eepromUpdate() {
	config.commit();
}

void snoozeUpdate();

void getTime(bool force) {
    DEBUG(*CurrentConfig::time_url);
	syncTimeTimer.lastCallTick = millis();	// Sometimes we aren't called from the timer
	if (WiFi.status() == WL_CONNECTED) {
		DEBUG("getTime() Connected");
		if (force || !timeSync->initialized()) {
			timeSync->init();
		}
	}
}

void getTime() {
	getTime(false);
}

void timeHandler(AsyncWebServerRequest *request) {
	DEBUG("Got time request")
	String wifiTime = request->getParam("time", true, false)->value();

	DEBUG(String("Setting time from wifi manager") + wifiTime);

	timeSync->setTime(wifiTime);

	request->send(SPIFFS, "/time.html");
}

PixelWave pixelWave(3, numLEDs);

int ledSecPos = 0; // position of the "fraction-based bar" in 16ths of a pixel

LEDRGB leds(numLEDs, LEDpin);
LDR ldr(INpin, 50, false);

void ledDisplay(bool on=true) {
	// Scale normalized brightness to range 0..255
	byte brightness = ldr.getAdjustedBrightness(*CurrentConfig::dimming, LEDRGB::gamma8(*CurrentConfig::led_scale), on);

	int attribute = 0;
	if (*CurrentConfig::sec_hue) {
		pixelWave.calculateValues(ledSecPos, 2, 0, 0);
	} else {
		pixelWave.calculateValues(ledSecPos, -1, 0, 0);
	}

	if (*CurrentConfig::sec_sat) {
		pixelWave.calculateValues(ledSecPos, 2, 1, 255 - *CurrentConfig::saturation);
	} else {
		pixelWave.calculateValues(ledSecPos, -1, 1, 255 - *CurrentConfig::saturation);
	}

	if (*CurrentConfig::sec_val) {
		pixelWave.calculateValues(ledSecPos, 2, 2, brightness);
	} else {
		pixelWave.calculateValues(ledSecPos, -1, 2, brightness);
	}

	for (int i=0; i<numLEDs; i++) {
		leds.setLedColorHSV(*CurrentConfig::hue + (pixelWave.getValue(0, i) / 6), 255 - pixelWave.getValue(1, i), on ? pixelWave.getValue(2, i) : 0);
		leds.ledDisplay(i);
	}
	leds.show();
//	leds.ledDisplay(*CurrentConfig::hue, *CurrentConfig::saturation, brightness);
}

void ledSecTimerHandler() {
	static u32_t prevMillis = 0;
	static int prevSec = -1;
	static u32_t subSec = 0;
	static int prevLedSecPos = -1;

	int sec = second();
	if (sec != prevSec) {
		prevSec = sec;
		prevMillis = millis();
	}
	subSec = millis() - prevMillis;
	u32_t milliSec = sec * 1000L + subSec;
	if (milliSec > 59999) {
		milliSec = 59999;
	}

	ledSecPos = map(milliSec, 0, 59999, 0, (numLEDs * 16));
	if (ledSecPos != prevLedSecPos) {
		prevLedSecPos = ledSecPos;
		ledDisplay();
	}
}

void ledTimerHandler() {
	ledDisplay();
	if (*CurrentConfig::hue_cycling) {
		broadcastUpdate(*CurrentConfig::hue);
		*CurrentConfig::hue = (*CurrentConfig::hue + 1) % 256;
	}
}

#ifdef I2C
#ifdef notdef
Button *pButton = NULL;
RotaryEncoder *pEncoder = NULL;
#endif
#endif

AsyncWiFiManagerParameter *hostnameParam;

void initFromEEPROM() {
//	config.setDebugPrint(debugPrint);
	config.init();
//	rootConfig.debug(debugPrint);
	DEBUG(hostName);
	rootConfig.get();	// Read all of the config values from EEPROM
	String currentSetName = currentSet;
	CurrentConfig::setCurrent(currentSetName);
	DEBUG(hostName);

	hostnameParam = new AsyncWiFiManagerParameter("Hostname", "clock host name", hostName.value.c_str(), 63);
}

void createSSID() {
	// Create a unique SSID that includes the hostname. Max SSID length is 32!
	ssid = (chipId + hostName).substring(0, 31);
}

void setWiFiAP(bool on) {
	if (on) {
		wifiManager.startConfigPortal();
	} else {
		wifiManager.stopConfigPortal();
	}
}

void SetupServer() {
	DEBUG("SetupServer()");
	hostName = String(hostnameParam->getValue());
	hostName.put();
	config.commit();
	createSSID();
	wifiManager.setAPCredentials(ssid.c_str(), "secretsauce");
	DEBUG(hostName.value);
	MDNS.begin(hostName.value.c_str());
    MDNS.addService("http", "tcp", 80);

	getTime(true);
}

boolean woke = false;
void snoozeUpdate() {
	woke = false;
	snoozeTimer.enabled = false;
}


SoftMSTimer::TimerInfo *infos[] = {
		&ledTimer,
		&ledSecTimer,
		&eepromUpdateTimer,
		&snoozeTimer,
		&syncTimer,
		&syncTimeTimer,
		0
};

SoftMSTimer timedFunctions(infos);

byte oldVoltage = 176;
bool oldHV = true;

void connectedHandler() {
	// MDNS listens for an IP address from WiFi
	DEBUG("WiFi connected");
	MDNS.begin(hostName.value.c_str());
	MDNS.addService("http", "tcp", 80);
	getTime(true);
}

void apChange(AsyncWiFiManager *wifiManager) {
	DEBUG("apChange()");
	DEBUG(wifiManager->isAP());
//	broadcastUpdate(wifiCallback());
}

void setup()
{
	pinMode(0, FUNCTION_0);
	pinMode(1, FUNCTION_3);
	pinMode(3, FUNCTION_3);

	chipId.toUpperCase();
#ifndef I2C
//	Serial.begin(921600);
	Serial.begin(115200);
	config.setDebugPrint(debugPrint);
#endif
	EEPROM.begin(1024);
	SPIFFS.begin();

	initFromEEPROM();

	timeSync = new TimeServerTimeSync(*CurrentConfig::time_url, NULL, NULL);

	// Enable LEDs
	leds.begin();
	ledDisplay();

	initClock();

	createSSID();

	// Turn off HV
	DEBUG("Voltage off")
	pinMode(VENpin, OUTPUT);
	digitalWrite(VENpin, HIGH);

	// Initialize charge detect inputs
	pinMode(CHRENpin, INPUT);
	pinMode(CHR0pin, INPUT);
	pinMode(CHR1pin, INPUT);

	// Read the current state, in case they are already high
	usbRating.ratingChanged();

	// Initialize voltage level
	oldVoltage = *CurrentConfig::voltage;
	vadj.setVoltage(oldVoltage);

	nowMs = millis();

	pinMode(MovPin, INPUT);
	mov.setDelay(1);
	mov.setOnTime(nowMs);
	mov.setCallback(&sendMovMsg);

	DEBUG("Voltage on")
	oldHV = *CurrentConfig::hv;
	digitalWrite(VENpin, oldHV ? LOW : HIGH);

	DEBUG("Set wifiManager")
//	WiFi.setSleepMode(WIFI_NONE_SLEEP);
#ifndef I2C
	wifiManager.setDebugOutput(true);
#else
	wifiManager.setDebugOutput(false);
#endif
	wifiManager.setHostname(hostName.value.c_str());
	wifiManager.setCustomOptionsHTML("<br><form action='/t' name='time_form' method='post'><button name='time' onClick=\"{var now=new Date();this.value=now.getFullYear()+','+(now.getMonth()+1)+','+now.getDate()+','+now.getHours()+','+now.getMinutes()+','+now.getSeconds();} return true;\">Set Clock Time</button></form><br><form action=\"/app.html\" method=\"get\"><button>Configure Clock</button></form>");
	wifiManager.addParameter(hostnameParam);
	wifiManager.setSaveConfigCallback(SetupServer);
	wifiManager.setConnectedCallback(connectedHandler);
	wifiManager.setConnectTimeout(10000);	// milliseconds
	wifiManager.setAPCallback(apChange);
	wifiManager.setAPCredentials(ssid.c_str(), "secretsauce");
	wifiManager.start();

    // This has to be done AFTER the wifiManager setup, because the wifimanager resets
    // the server which causes a crash...
	server.serveStatic("/", SPIFFS, "/");
	server.on("/", HTTP_GET, mainHandler).setFilter(ON_STA_FILTER);
	server.on("/assets/favicon-32x32.png", HTTP_GET, sendFavicon);
	server.serveStatic("/assets", SPIFFS, "/assets");
	server.on("/t", HTTP_POST, timeHandler).setFilter(ON_AP_FILTER);
#ifdef OTA
	otaUpdater.init(server, "/update", sendUpdateForm, sendUpdatingInfo);
#endif

	// attach AsyncWebSocket
	ws.onEvent(wsHandler);
	server.addHandler(&ws);
	server.begin();
	ws.enable(true);

#ifdef I2C
	Wire.pins(SDApin, SCLpin);
	Wire.begin();

#ifdef notdef
	if (oled.begin(OLED_ADDRESS, SDApin, SCLpin)) {
		oled.setFont(u8x8_font_chroma48medium8_r);
		oled.setCursor(0, 0);
		oled.print("Got OLED");
	}
#endif

	delay(5);

	// Need to do this BEFORE ups.begin()
	if (mcp.begin(EXPANSION_ADDRESS)) {
#ifdef notdef
		oled.setCursor(0, 1);
		oled.print("Got Exp");
#endif
		wsMenuHandler.setItems(ups_items);
	}

	// Set up UPS shield interface
	ups.begin();

	delay(5);

#ifdef notdef
	pButton = new MCP23017Button(mcp, MCP_BUTTONpin);
	pEncoder = new MCP23017RotaryEncoder(mcp, MCP_ENCODERApin, MCP_ENCODERBpin);
#endif

	if (lis.begin(ACCELEROMETER_ADDRESS)) {
#ifdef notdef
		oled.setCursor(0, 2);
		oled.print("Got LIS");
#endif

		lis.setRange(LIS3DH_RANGE_4_G);   // 2, 4, 8 or 16 G!
		// 0 = turn off click detection & interrupt
		// 1 = single click only interrupt output
		// 2 = double click only interrupt output, detect single click
		// Adjust threshhold, higher numbers are less sensitive
		// timeLimit: 10 Max time in milliseconds the acceleration can spend above the threshold to be considered a click
		// latency: 20 Min time in milliseconds between the end of one click event and the start of another to be considered a LIS3DH_DOUBLE_CLICK
		// window: 255 Max time in milliseconds between the start of one click event and end of another to be considered a LIS3DH_DOUBLE_CLICK
		lis.setClick(2, 40, 5, 20, 255);
	}
#endif

	DEBUG("Exit setup")
#ifdef I2C
#ifdef notdef
	oled.clearDisplay();
#endif
#endif
}

void loop()
{
	if (WiFi.isConnected()) {
		MDNS.update();
	}
	wifiManager.loop();
	syncBusLoop();

#ifdef I2C
#ifdef notdef
	if (pButton != NULL && pButton->clicked()) {
		clicks++;
	}

	if (pEncoder != NULL) {
		rotation += pEncoder->getRotation();
	}
#endif
#endif

	if (oldVoltage != *CurrentConfig::voltage) {
		oldVoltage = *CurrentConfig::voltage;
		vadj.setVoltage(oldVoltage);
	}

	pDriver->setIndicator(*CurrentConfig::indicator);
	pDriver->setDigitMap(((String)(*CurrentConfig::pin_order)).c_str());
	NixieDriver::setPWMFreq(*CurrentConfig::pwm_freq);
	mov.setDelay(*CurrentConfig::mov_delay);
	mov.setSrc(*CurrentConfig::mov_src);

	pNixieClock->setTimeSync(timeSync);

	if (timeSync->initialized() || !*CurrentConfig::display) {
		pNixieClock->setClockMode(*CurrentConfig::display);
		pNixieClock->setCountSpeed(*CurrentConfig::count_speed);
	} else {
		pNixieClock->setClockMode(false);
		pNixieClock->setCountSpeed(60);
	}

	bool clockOn = woke || ((ups.getVBus() || (*CurrentConfig::lpm == false)) && pNixieClock->isOn() && mov.isOn());
	blankingMonitor.on(clockOn);
	bool newHV = *CurrentConfig::hv && clockOn;

	bool dimming = *CurrentConfig::dimming;
	if (oldHV != newHV) {
		oldHV = newHV;
		if (newHV) {
			ldr.reset();	// Briefly set to 50% brightness to cause neon to strike
		}
		digitalWrite(VENpin, oldHV ? LOW : HIGH);
	}

	pNixieClock->setBrightness(ldr.getNormalizedBrightness(dimming));
	pNixieClock->setFadeMode(*CurrentConfig::fading);
	pNixieClock->setTimeMode(*CurrentConfig::time_or_date);
	pNixieClock->setDateFormat(*CurrentConfig::date_format);
	pNixieClock->setShowSeconds(*CurrentConfig::time_format);
	pNixieClock->set12hour(*CurrentConfig::hour_format);
	pNixieClock->setDigit(*CurrentConfig::digit);
	pNixieClock->setOnOff(*CurrentConfig::display_on, *CurrentConfig::display_off);
	pNixieClock->setDigitsOn(*CurrentConfig::digits_on);
	pNixieClock->setScrollback(*CurrentConfig::scrollback);

	nowMs = millis();
	pNixieClock->loop(nowMs);
	if (pNixieClock->getNixieDigit() != *CurrentConfig::digit) {
		*CurrentConfig::digit = pNixieClock->getNixieDigit();
		if (pNixieClock->getACPCount() == 0) {
			broadcastUpdate(*CurrentConfig::digit);
		}
	}

	if (*CurrentConfig::backlight && clockOn) {
		ledTimer.interval = *CurrentConfig::cycle_time * 1000L / 256;
		ledTimer.enabled = true;
		ledSecTimer.enabled = true;
	} else {
		if (ledTimer.enabled) {
			ledTimer.enabled = false;
			ledSecTimer.enabled = false;
			ledDisplay(false);
		}
	}

	timedFunctions.loop();

	if (usbRating.ratingChanged()) {
		StringConfigItem temp ("usb_rating", 10, usbRating.getString());
		broadcastUpdate(temp);
	}

#ifdef I2C
#ifdef notdef
	usbRating.debug(oled);
#endif
	if (ups.vBusChanged()) {
		StringConfigItem temp("power", 10, ups.getPowerString());
		broadcastUpdate(temp);

		if (ups.getVBus() == 0) {
			// Vbus has changed to 0
			usbRating.reset();
			StringConfigItem temp ("usb_rating", 10, usbRating.getString());

			if (*CurrentConfig::lpm) {
				snoozeTimer.interval = (*CurrentConfig::wakeup_time * 1000);
				snoozeTimer.lastCallTick = nowMs;
				snoozeTimer.enabled = true;
				woke = true;
			}
		}
	}

	if (ups.rateChanged(*CurrentConfig::charge_rate, usbRating)) {
		broadcastUpdate(StringConfigItem("charge_rate_txt", 10, ups.getRateString()));
	}

#ifdef notdef
	if (oled) {
		oled.setCursor(0, 4);
		oled.print("V,W,O:");
		oled.print(ups.getVBus());
		oled.print(",");
		oled.print(woke);
		oled.print(",");
		oled.print(pNixieClock->isOn());
	}
#endif

	if (lis) {
		if (lis.isClicked()) {
			if (clockOn) {
				pNixieClock->toggleAlternateTime();
			}
			snoozeTimer.interval = (*CurrentConfig::wakeup_time * 1000);
			snoozeTimer.lastCallTick = nowMs;
			snoozeTimer.enabled = true;
			woke = true;
		} else if (!woke) {
			pNixieClock->setAlternateTime(false);
		}
		lis.read();      // get X Y and Z data at once
#ifdef notdef
		lis.debug(oled);
#endif
	}
#endif
}
