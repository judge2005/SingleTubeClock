#define I2C
#define ONE_TUBE

//#define DEBUG_ESP_WIFI
//#define DEBUG_ESP_PORT Serial

#ifndef I2C
Print *debugPrint = &Serial;
#define DEBUG(...) { Serial.println(__VA_ARGS__); }
#else
#define DEBUG(...) {}
#endif
//#define DEBUG(...) { debugPrint->println(__VA_ARGS__); }

#include "Arduino.h"
#include <ConfigItem.h>
#include <EEPROMConfig.h>
#define OTA
#ifdef OTA
#include <ArduinoOTA.h>
#endif
#include <EEPROM.h>
#include <SPIFFSEditor.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncHTTPClient.h>
#include <ESPAsyncWiFiManager.h>
#include <DNSServer.h>
#include <Wire.h>

#include <TimeLib.h>

//#include <SPI.h>

#ifdef I2C
#include <MCP23017Button.h>
#include <MCP23017RotaryEncoder.h>
#endif

#include <SafeMCP23017.h>
#include <SafeLIS3DH.h>
#include <SafeOLED.h>
#include <UPS.h>

#include <HV5523NixieClockMultiplex.h>
#include <HV5523InvNixieDriver.h>
#include <HV5523Inv15SegNixieDriver.h>
#include <OneNixieClock.h>
#include <FourNixieClock.h>
#include <LEDs.h>
#include <LDR.h>
#include <SoftMSTimer.h>
#include <CP2102NUSBRating.h>

#include <WSHandler.h>
#include <WSMenuHandler.h>
#include <WSConfigHandler.h>
#include <WSPresetValuesHandler.h>
#include <WSPresetNamesHandler.h>
#include <WSInfoHandler.h>
#include <WSUPSHandler.h>

// ESP8285 pins
const byte LEDpin = 0;	// D3, has built-in 10k pull up, but we don't care.
const byte CHRENpin = 5;
const byte CHR0pin = 10;
const byte CHR1pin = 9;
const byte VENpin = 2; // Add pullup
const byte VADJpin = 4;	//
const byte DIpin = 13;	// MOSI, D7
const byte CLpin = 14;	// SCK, D5
const byte LEpin = 12;	// MISO, D6

const byte ADCpin = A0;

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


SafeOLED oled;
SafeLIS3DH lis;
SafeMCP23017 mcp;
UPS ups(mcp);

unsigned long nowMs = 0;

String chipId = String(ESP.getChipId(), HEX);
String ssid = "STC-";

StringConfigItem hostName("hostname", 63, "SingleTubeClock");

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncHTTPClient httpClient;
DNSServer dns;
AsyncWiFiManager wifiManager(&server,&dns);

HV5523NixieClockMultiplex mulitplexDriver(LEpin, 4);
HV5523Inv15SegNixieDriver fifteenSegDriver(LEpin);
HV5523InvNixieDriver nixieDriver(LEpin);
NixieDriver *pDriver = &nixieDriver;
OneNixieClock oneNixieClock(pDriver);
FourNixieClock fourNixieClock(&mulitplexDriver);

NixieClock *pNixieClock = &oneNixieClock;

int clicks = 0;
int rotation = 0;

CP2102NUSBRating usbRating(CHRENpin, CHR0pin, CHR1pin);

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

namespace ConfigSet4 {
#include <ConfigSet4.h>
}	// End namespace

namespace ConfigSet5 {
#include <ConfigSet5.h>
}	// End namespace

StringConfigItem set1Name("set1_name", 12, "Hue Cycling");
StringConfigItem set2Name("set2_name", 12, "Fixed Backlight");
StringConfigItem set3Name("set3_name", 12, "Fast Clock");
StringConfigItem set4Name("set4_name", 12, "Conditioner");
StringConfigItem set5Name("set5_name", 12, "Manual");
BaseConfigItem *configSetPresetNames[] = {
	&set1Name,
	&set2Name,
	&set3Name,
	&set4Name,
	&set5Name,
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
	&ConfigSet4::config,
	&ConfigSet5::config,
	0
};

CompositeConfigItem rootConfig("root", 0, configSetRoot);

EEPROMConfig config(rootConfig);

namespace CurrentConfig {
	String name("set1");
	CompositeConfigItem *config = &ConfigSet1::config;

	// Clock config values
	BooleanConfigItem *time_or_date = &ConfigSet1::time_or_date;
	ByteConfigItem *date_format = &ConfigSet1::date_format;
	BooleanConfigItem *time_format = &ConfigSet1::time_format;
	BooleanConfigItem *hour_format = &ConfigSet1::hour_format;
	ByteConfigItem *fading = &ConfigSet1::fading;
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

	// Extra config values
	BooleanConfigItem *dimming = &ConfigSet1::dimming;
	BooleanConfigItem *display = &ConfigSet1::display;
	BooleanConfigItem *hv = &ConfigSet1::hv;
	ByteConfigItem *voltage = &ConfigSet1::voltage;
	IntConfigItem *digit = &ConfigSet1::digit;
	ByteConfigItem *count_speed = &ConfigSet1::count_speed;

	// UPS config values
	ByteConfigItem *charge_rate = &ConfigSet1::charge_rate;
	BooleanConfigItem *lpm = &ConfigSet1::lpm;
	ByteConfigItem *wakeup_time = &ConfigSet1::wakeup_time;
	ByteConfigItem *sensitivity = &ConfigSet1::sensitivity;

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

			// Extra config values
			dimming = static_cast<BooleanConfigItem*>(config->get("dimming"));
			display = static_cast<BooleanConfigItem*>(config->get("display"));
			hv = static_cast<BooleanConfigItem*>(config->get("hv"));
			voltage = static_cast<ByteConfigItem*>(config->get("voltage"));
			digit = static_cast<IntConfigItem*>(config->get("digit"));
			count_speed = static_cast<ByteConfigItem*>(config->get("count_speed"));

			// UPS config values
			charge_rate = static_cast<ByteConfigItem*>(config->get("charge_rate"));
			lpm = static_cast<BooleanConfigItem*>(config->get("lpm"));
			wakeup_time = static_cast<ByteConfigItem*>(config->get("wakeup_time"));
			sensitivity = static_cast<ByteConfigItem*>(config->get("sensitivity"));

			BaseConfigItem *currentSetName = rootConfig.get("current_set");
			currentSetName->fromString(name);
			currentSetName->put();
		}
	}
}

#ifdef USE_NTP
const String GOOGLE_API_URL = "https://maps.googleapis.com/maps/api/timezone/json?location=[loc]&timestamp=[ts]";
long ntpTimeOffset = 0;
unsigned long ntpTime = 0;

void setTZInfo() {
	String body = httpClient.getBody();
	DEBUG(String("Got response") + body);
	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.parseObject(body);
	if (json.success()) {
		const char* rcstatus = json["status"];
		const long rawOffset = json["rawOffset"];
		const long dstOffset = json["dstOffset"];
		if (strcmp(rcstatus, "OK") == 0) {
			ntpTimeOffset = rawOffset + dstOffset;
			DEBUG("Time offset:");
			DEBUG(ntpTimeOffset);
			setTime(ntpTime + ntpTimeOffset);
		}
	} else {
		DEBUG("JSON parse failed");
	}
}

void readTZInfoFailed(String err) {
	DEBUG(String("Failed to retrieve time offset: ") + err);
}

void getTZInfo() {
	if (WiFi.status() == WL_CONNECTED) {
		char buf[64];
		String url = GOOGLE_API_URL;
		url.replace("[loc]", "42.6148781,-71.5968927");
		snprintf(buf, sizeof(buf), "%u", ntpTime);
		url.replace("[ts]", buf);

		httpClient.initialize(url);
		DEBUG(httpClient.protocol);
		DEBUG(httpClient.port);
		DEBUG(httpClient.host);
		DEBUG(httpClient.request);
		DEBUG(httpClient.uri);
		httpClient.makeRequest(setTZInfo, readTZInfoFailed);
	}
}

const unsigned int NTP_LOCAL_PORT = 2390;
const char* NTP_SERVER_NAME = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
const int NTP_CHECK_INTERVAL = 90; // How often to ping NTP server (seconds)
const int CLOCK_DIFF_RANGE = 5; // If diff between clock and NTP time <= value, we consider them synchronized
WiFiUDP ntpRequest;
byte ntpRequestBuffer[NTP_PACKET_SIZE];
byte ntpResponseBuffer[NTP_PACKET_SIZE];

void sendNTPRequest() {
	// Dump any outstanding requests
	while (ntpRequest.parsePacket() > 0) ;

	// Prepare NTP packet
	memset(ntpRequestBuffer, 0, NTP_PACKET_SIZE);
	ntpRequestBuffer[0] = 0b11100011;   // LI, Version, Mode
	ntpRequestBuffer[1] = 0;            // Stratum, or type of clock
	ntpRequestBuffer[2] = 6;            // Polling Interval
	ntpRequestBuffer[3] = 0xEC;         // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	ntpRequestBuffer[12] = 49;
	ntpRequestBuffer[13] = 0x4E;
	ntpRequestBuffer[14] = 49;
	ntpRequestBuffer[15] = 52;

	// Send packet
	IPAddress ntpIP;
	int errCode = WiFi.hostByName(NTP_SERVER_NAME, ntpIP);
	if (errCode == 1) {
		ntpRequest.beginPacket(ntpIP, 123);  // NTP requests are to port 123
		ntpRequest.write(ntpRequestBuffer, NTP_PACKET_SIZE);
		ntpRequest.endPacket();
		DEBUG("Sent NTP request");
	} else {
		DEBUG("Couldn't find NTP server IP: ");
		DEBUG(errCode);
	}
}

void readNTPResponse() {
	const unsigned long seventyYears = 2208988800UL;

	if (ntpRequest.parsePacket()) {
		// Read the packet into the buffer
		ntpRequest.read(ntpResponseBuffer, NTP_PACKET_SIZE);

		// The timestamp starts at byte 40 of the received packet and is four bytes,
		// or two words, long. First, extract the two words:
		unsigned long highWord = word(ntpResponseBuffer[40], ntpResponseBuffer[41]);
		unsigned long lowWord = word(ntpResponseBuffer[42], ntpResponseBuffer[43]);

		// Combine the four bytes (two words) into a long integer
		// this is NTP time (seconds since Jan 1 1900):
		ntpTime = (highWord << 16 | lowWord) - seventyYears;

		DEBUG("Got NTP Response:");
		DEBUG(ntpTime);

	    setTime(ntpTime + ntpTimeOffset);
	}
}
#endif

void initClock() {
	switch (tube_type) {
	case 1:
		pNixieClock = &oneNixieClock;
		pDriver = &fifteenSegDriver;
		break;
	case 2:
		pNixieClock = &fourNixieClock;
		pDriver = &mulitplexDriver;
		break;
	default:
		pNixieClock = &oneNixieClock;
		pDriver = &nixieDriver;
		break;
	}

	pDriver->init();
	pNixieClock->setNixieDriver(pDriver);
	pNixieClock->init();
}

void StartOTA() {
#ifdef OTA
	// Port defaults to 8266
	ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(((String)hostName).c_str());

	// No authentication by default
//	ArduinoOTA.setPassword("in14");

	ArduinoOTA.onStart([]() {DEBUG("Start");});
	ArduinoOTA.onEnd([]() {DEBUG("\nEnd");});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUG("Progress: ");DEBUG(progress / (total / 100));DEBUG("\r");
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUG("Error")
		switch (error) {
			case OTA_AUTH_ERROR: DEBUG("Auth Failed"); break;
			case OTA_BEGIN_ERROR: DEBUG("Begin Failed"); break;
			case OTA_CONNECT_ERROR: DEBUG("Connect Failed"); break;
			case OTA_RECEIVE_ERROR: DEBUG("Receive Failed"); break;
			case OTA_END_ERROR: DEBUG("End Failed"); break;
		}
	});

	ArduinoOTA.begin();
#endif
}

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got request")
	request->send(SPIFFS, "/index.html");
}

void sendFavicon(AsyncWebServerRequest *request) {
	DEBUG("Got favicon request")
	request->send(SPIFFS, "/assets/favicon-32x32.png", "image/png");
}

void broadcastUpdate(const BaseConfigItem& item) {
	const size_t bufferSize = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(1);
	DynamicJsonBuffer jsonBuffer(bufferSize);

	JsonObject& root = jsonBuffer.createObject();
	root["type"] = "sv.update";

	JsonObject& value = root.createNestedObject("value");
	String rawJSON = item.toJSON();	// This object needs to hang around until we are done serializing.
	value[item.name] = ArduinoJson::RawJson(rawJSON.c_str());

//	root.printTo(*debugPrint);

    size_t len = root.measureLength();
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
        root.printTo((char *)buffer->get(), len + 1);
    	ws.textAll(buffer);
    }
}

WSMenuHandler wsMenuHandler(ups);
WSConfigHandler wsClockHandler(rootConfig, "clock");
WSConfigHandler wsLEDHandler(rootConfig, "leds");
WSConfigHandler wsExtraHandler(rootConfig, "extra");
WSPresetValuesHandler wsPresetValuesHandler(rootConfig);
WSInfoHandler wsInfoHandler(ssid);
WSPresetNamesHandler wsPresetNamesHandler(rootConfig);
WSUPSHandler wsUPSHandler(rootConfig, "ups", ups, usbRating);

WSHandler *wsHandlers[] = {
	&wsMenuHandler,
	&wsClockHandler,
	&wsLEDHandler,
	&wsExtraHandler,
	&wsPresetValuesHandler,
	&wsInfoHandler,
	&wsPresetNamesHandler,
	&wsUPSHandler
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
#ifndef USE_NTP
				if (strcmp(key, CurrentConfig::time_url->name) == 0) {
					httpClient.initialize(value);
					getTime();
				}
#endif
				broadcastUpdate(*item);
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

void grabInts(String s, int *dest, String sep) {
	int end = 0;
	for (int start = 0; end != -1; start = end + 1) {
		end = s.indexOf(sep, start);
		if (end > 0) {
			*dest++ = s.substring(start, end).toInt();
		} else {
			*dest++ = s.substring(start).toInt();
		}
	}
}

void grabBytes(String s, byte *dest, String sep) {
	int end = 0;
	for (int start = 0; end != -1; start = end + 1) {
		end = s.indexOf(sep, start);
		if (end > 0) {
			*dest++ = s.substring(start, end).toInt();
		} else {
			*dest++ = s.substring(start).toInt();
		}
	}
}

void readTimeFailed(String msg) {
	DEBUG(msg);
}

#define SYNC_HOURS 3
#define SYNC_MINS 4
#define SYNC_SECS 5
#define SYNC_DAY 2
#define SYNC_MONTH 1
#define SYNC_YEAR 0

bool timeInitialized = false;

void setTimeFromInternet() {
	String body = httpClient.getBody();
	DEBUG(String("Got response") + body);
	int intValues[6];
	grabInts(body, &intValues[0], ",");

	timeInitialized = true;
    setTime(intValues[SYNC_HOURS], intValues[SYNC_MINS], intValues[SYNC_SECS], intValues[SYNC_DAY], intValues[SYNC_MONTH], intValues[SYNC_YEAR]);
}

const byte numLEDs = 4;

LEDRGB leds(numLEDs, LEDpin);
LDR ldr(ADCpin, 50);

void ledDisplay(bool on=true) {
	// Scale normalized brightness to range 0..255
	byte brightness = ldr.getAdjustedBrightness(*CurrentConfig::dimming, *CurrentConfig::led_scale, on);
	leds.ledDisplay(*CurrentConfig::hue, *CurrentConfig::saturation, brightness);
}

void ledTimerHandler() {
	ledDisplay();
	if (*CurrentConfig::hue_cycling) {
		broadcastUpdate(*CurrentConfig::hue);
		*CurrentConfig::hue = (*CurrentConfig::hue + 1) % 256;
	}
}

int getHVDutyCycle(byte vout) {
	static const float vcc = 3.15;
	static const float radj = 0.0;

	float vadj = 1.26+(radj+30.0)*((1.26/10.2)-((vout-1.26)/1208.0));
	int duty = roundf(vadj*100/vcc);

	return constrain(duty, 0, 100);
}

#ifdef I2C
Button *pButton = NULL;
RotaryEncoder *pEncoder = NULL;
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

void SetupServer() {
	DEBUG("SetupServer()");
	hostName = String(hostnameParam->getValue());
	hostName.put();
	config.commit();
	DEBUG(hostName.value);
	MDNS.begin(hostName.value.c_str());
    MDNS.addService("http", "tcp", 80);
	StartOTA();

	server.serveStatic("/", SPIFFS, "/");
	server.on("/", HTTP_GET, mainHandler).setFilter(ON_STA_FILTER);
	server.on("/assets/favicon-32x32.png", HTTP_GET, sendFavicon);
	server.serveStatic("/assets", SPIFFS, "/assets");

	// attach AsyncWebSocket
	ws.onEvent(wsHandler);
	server.addHandler(&ws);
	server.begin();
	ws.enable(true);
}

void eepromUpdate() {
	config.commit();
}

void snoozeUpdate();

void getTime() {
	if (WiFi.status() == WL_CONNECTED) {
		httpClient.makeRequest(setTimeFromInternet, readTimeFailed);
	}
}

SoftMSTimer::TimerInfo ledTimer = {
		60000,
		0,
		true,
		ledTimerHandler
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

#ifdef USE_NTP
SoftMSTimer::TimerInfo ntpRequestTimer = {
		NTP_CHECK_INTERVAL * 1000,
		0,
		true,
		sendNTPRequest
};

SoftMSTimer::TimerInfo tzInfoTimer = {
		NTP_CHECK_INTERVAL * 1000,
		0,
		true,
		getTZInfo
};

SoftMSTimer::TimerInfo ntpResponseTimer = {
		1500,
		0,
		true,
		readNTPResponse
};
#else
SoftMSTimer::TimerInfo syncTimeTimer = {
		60000,
		0,
		true,
		getTime
};
#endif

boolean woke = false;
void snoozeUpdate() {
	woke = false;
	snoozeTimer.enabled = false;
}


SoftMSTimer::TimerInfo *infos[] = {
		&ledTimer,
		&eepromUpdateTimer,
		&snoozeTimer,
#ifdef USE_NTP
		&ntpRequestTimer,
		&tzInfoTimer,
		&ntpResponseTimer,
#else
//		&syncTimeTimer,
#endif
		0
};

SoftMSTimer timedFunctions(infos);
byte oldVoltage = 176;
bool oldHV = true;
void setup()
{
	chipId.toUpperCase();
#ifndef I2C
//	Serial.begin(921600);
	Serial.begin(115200);
#endif
	EEPROM.begin(1024);
	SPIFFS.begin();
#ifdef USE_NTP
	ntpRequest.begin(NTP_LOCAL_PORT);
#endif

	initFromEEPROM();

	initClock();

	createSSID();

	DEBUG("Set wifiManager")
	wifiManager.setDebugOutput(false);
	wifiManager.setConnectTimeout(10);
	wifiManager.addParameter(hostnameParam);
	wifiManager.setSaveConfigCallback(SetupServer);
    wifiManager.startConfigPortalModeless(ssid.c_str(), "secretsauce");

#ifdef USE_NTP
	sendNTPRequest();
#else
	httpClient.initialize(*CurrentConfig::time_url);
	getTime();
#endif

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
	pinMode(VADJpin, OUTPUT);
	analogWriteFreq(4000);
	analogWriteRange(100);
	analogWrite(VADJpin, getHVDutyCycle(oldVoltage));

	// Enable LEDs
	leds.begin();
	ledDisplay();

	nowMs = millis();

	DEBUG("Voltage on")
	oldHV = *CurrentConfig::hv;
	digitalWrite(VENpin, oldHV ? LOW : HIGH);

#ifdef I2C
	Wire.pins(SDApin, SCLpin);
	Wire.begin();

	if (oled.begin(OLED_ADDRESS, SDApin, SCLpin)) {
		oled.setFont(u8x8_font_chroma48medium8_r);
		oled.setCursor(0, 0);
		oled.print("Got OLED");
	}

	delay(5);

	// Need to do this BEFORE ups.begin()
	if (mcp.begin(EXPANSION_ADDRESS)) {
		oled.setCursor(0, 1);
		oled.print("Got Exp");
	}

	// Set up UPS shield interface
	ups.begin();

	delay(5);

#ifdef notdef
	pButton = new MCP23017Button(mcp, MCP_BUTTONpin);
	pEncoder = new MCP23017RotaryEncoder(mcp, MCP_ENCODERApin, MCP_ENCODERBpin);
#endif

	if (lis.begin(ACCELEROMETER_ADDRESS)) {
		oled.setCursor(0, 2);
		oled.print("Got LIS");

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
	oled.clearDisplay();
#endif
}

void loop()
{
	wifiManager.loop();
#ifdef OTA
	ArduinoOTA.handle();
#endif

	nowMs = millis();

#ifdef I2C
	if (pButton != NULL && pButton->clicked()) {
		clicks++;
	}

	if (pEncoder != NULL) {
		rotation += pEncoder->getRotation();
	}
#endif

	if (oldVoltage != *CurrentConfig::voltage) {
		oldVoltage = *CurrentConfig::voltage;
		analogWrite(VADJpin, getHVDutyCycle(oldVoltage));
	}

	pDriver->setBrightness(ldr.getNormalizedBrightness(*CurrentConfig::dimming));
	if (timeInitialized || !*CurrentConfig::display) {
		pNixieClock->setClockMode(*CurrentConfig::display);
		pNixieClock->setCountSpeed(*CurrentConfig::count_speed);
	} else {
		pNixieClock->setClockMode(false);
		pNixieClock->setCountSpeed(60);
	}
	pNixieClock->setFadeMode(*CurrentConfig::fading);
	pNixieClock->setTimeMode(*CurrentConfig::time_or_date);
	pNixieClock->setDateFormat(*CurrentConfig::date_format);
	pNixieClock->setShowSeconds(*CurrentConfig::time_format);
	pNixieClock->set12hour(*CurrentConfig::hour_format);
	pNixieClock->setDigit(*CurrentConfig::digit);
	pNixieClock->setOnOff(*CurrentConfig::display_on, *CurrentConfig::display_off);
	pNixieClock->setDigitsOn(*CurrentConfig::digits_on);
	pNixieClock->setScrollback(*CurrentConfig::scrollback);
	bool clockOn = woke || ((ups.getVBus() || (*CurrentConfig::lpm == false)) && pNixieClock->isOn());

	bool newHV = *CurrentConfig::hv && clockOn;

	if (oldHV != newHV) {
		oldHV = newHV;
		digitalWrite(VENpin, oldHV ? LOW : HIGH);
	}

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
	} else {
		if (ledTimer.enabled) {
			ledTimer.enabled = false;
			ledDisplay(false);
		}
	}

	timedFunctions.loop();

	if (usbRating.ratingChanged()) {
		StringConfigItem temp ("usb_rating", 10, usbRating.getString());
		broadcastUpdate(temp);
	}

#ifdef I2C
	usbRating.debug(oled);

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

	if (oled) {
		oled.setCursor(0, 4);
		oled.print("V,W,O:");
		oled.print(ups.getVBus());
		oled.print(",");
		oled.print(woke);
		oled.print(",");
		oled.print(pNixieClock->isOn());
	}

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
		lis.debug(oled);
	}
#endif
}
