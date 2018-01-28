//#define I2C

#ifdef I2C
const byte SDApin = 1;	// aka TXD
const byte SCLpin = 3;	// aka RXD
#endif

#ifdef I2C
#include <U8x8lib.h>

U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE, SCLpin, SDApin);

Print *debugPrint = &oled;
#else
Print *debugPrint = &Serial;
#define DEBUG_ESP_WIFI
#define DEBUG_ESP_PORT Serial
#endif

//#define DEBUG(...) { Serial.println(__VA_ARGS__); }
//#define DEBUG(...) { debugPrint->println(__VA_ARGS__); }
#define DEBUG(...) {}

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

unsigned long nowMs = 0;

String chipId = String(ESP.getChipId(), HEX);
String ssid = "STC-";

StringConfigItem hostName("hostname", 63, "SingleTubeClock");

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncHTTPClient httpClient;
DNSServer dns;
AsyncWiFiManager wifiManager(&server,&dns);
HV5523InvNixieDriver nixieDriver(LEpin);
OneNixieClock nixieClock(nixieDriver);

#ifdef I2C
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_MCP23017 mcp;
#endif

class SoftMSTimer {
public:
	typedef void (*callback_t)(void);

	struct TimerInfo {
		unsigned long interval;
		unsigned long lastCallTick;
		bool enabled;
		callback_t callback;
	};

	SoftMSTimer(TimerInfo **timers) : timers(timers) {

	}

	void loop() {
		unsigned long nowMs = millis();
		for (int i=0; timers[i] != 0; i++) {
			TimerInfo *info = timers[i];
			if (info->enabled && (info->lastCallTick == 0 || nowMs > info->lastCallTick + info->interval)) {
				info->lastCallTick = nowMs;
				info->callback();
			}
		}
	}
private:
	TimerInfo **timers;
};

struct test {
	int anInt;
	byte aByte;
	bool aBool;
	char char23[23];
};

test aTest = {
		10,
		20,
		true,
		{ 'a', 'b', 0 }
};


const byte MCPButtonPin = 7;
const byte MCPEncoderA = 8;
const byte MCPEncoderB = 9;
int clicks = 0;
int rotation = 0;

volatile bool chren = false;
volatile bool chr0 = false;
volatile bool chr1 = false;

#define PIN_RESET 255  //
#define DC_JUMPER 0  // I2C Address: 0 - 0x3C, 1 - 0x3D

struct Button {
	Adafruit_MCP23017 *mcp;
	byte buttonPin;

	int lastButtonValue = 0;
	unsigned long changeTime = 0;

	Button(Adafruit_MCP23017 *mcp, byte pin) : mcp(mcp), buttonPin(pin) {
	}

	bool state() {
		int buttonValue = mcp->digitalRead(buttonPin);

		unsigned long now = millis();

		// Has to stay at that value for 50ms
		if (buttonValue != lastButtonValue) {
			if (now - changeTime > 50)
				lastButtonValue = buttonValue;

			changeTime = now;
		}

		return lastButtonValue;
	}

	bool wasPressed = false;

	bool clicked() {
		bool nowPressed = state();

		if (!wasPressed && nowPressed) {
			wasPressed = true;
			return true;
		}

		wasPressed = nowPressed;

		return false;
	}
};

struct RotaryEncoder {
	Adafruit_MCP23017 *mcp;

	byte pinA;
	byte pinB;
	int lastModeA;
	int lastModeB;
	int curModeA;
	int curModeB;
	int curPos;
	int lastPos;

	RotaryEncoder(Adafruit_MCP23017 *mcp, byte aPin, byte bPin) :
		mcp(mcp),
		pinA(aPin),
		pinB(bPin),
		lastModeA(LOW),
		lastModeB(LOW),
		curModeA(LOW),
		curModeB(LOW),
		curPos(0),
		lastPos(0)
	{
	}

	int getRotation() {
		// read the current state of the current encoder's pins
		curModeA = mcp->digitalRead(pinA);
		curModeB = mcp->digitalRead(pinB);

		if ((lastModeA == LOW) && (curModeA == HIGH)) {
			if (curModeB == LOW) {
				curPos--;
			} else {
				curPos++;
			}
		}
		lastModeA = curModeA;
		int rotation = curPos - lastPos;
		lastPos = curPos;

		return rotation;
	}
};

namespace ConfigSet1 {
	// Clock config values
	BooleanConfigItem time_or_date("time_or_date", true);
	ByteConfigItem date_format("date_format", 1);
	BooleanConfigItem time_format("time_format", false);
	BooleanConfigItem hour_format("hour_format", false);
	ByteConfigItem fading("fading", 1);
	BooleanConfigItem scrollback("scrollback", true);
	IntConfigItem digits_on("digits_on", 1500);
	ByteConfigItem display_on("display_on", 6);
	ByteConfigItem display_off("display_off", 22);
	StringConfigItem time_url("time_url", 80, String("http://time.niobo.us/getTime/America/New_York"));

	// LED config values
	ByteConfigItem hue("hue", 0);
	ByteConfigItem saturation("saturation", 215);
	BooleanConfigItem backlight("backlight", true);
	BooleanConfigItem hue_cycling("hue_cycling", true);
	ByteConfigItem led_scale("led_scale", 127);
	IntConfigItem cycle_time("cycle_time", 120);

	// Extra config values
	BooleanConfigItem dimming("dimming", true);
	BooleanConfigItem display("display", true);	// true == clock
	BooleanConfigItem hv("hv", true);
	ByteConfigItem voltage("voltage", 176);
	ByteConfigItem digit("digit", 0);
	ByteConfigItem count_speed("count_speed", 60);	// ticks per minute 0 to 60

	BaseConfigItem *clockSet[] = {
		// Clock
		&time_or_date,
		&date_format,
		&time_format,
		&hour_format,
		&fading,
		&scrollback,
		&digits_on,
		&display_on,
		&display_off,
		&time_url,
		0
	};

	BaseConfigItem *ledSet[] = {
		// LEDs
		&cycle_time,
		&hue,
		&saturation,
		&led_scale,
		&backlight,
		&hue_cycling,
		0
	};

	BaseConfigItem *extraSet[] = {
		// Extra
		&dimming,
		&display,
		&hv,
		&voltage,
		&digit,
		&count_speed,
		0
	};

	CompositeConfigItem clockConfig("clock", 0, clockSet);
	CompositeConfigItem ledConfig("led", 0, ledSet);
	CompositeConfigItem extraConfig("extra", 0, extraSet);

	BaseConfigItem *configSet[] = {
		// Clock
		&clockConfig,
		&ledConfig,
		&extraConfig,
		0
	};

	CompositeConfigItem config("set1", 0, configSet);
}

namespace ConfigSet2 {
	// Clock config values
	BooleanConfigItem time_or_date("time_or_date", true);
	ByteConfigItem date_format("date_format", 1);
	BooleanConfigItem time_format("time_format", false);
	BooleanConfigItem hour_format("hour_format", false);
	ByteConfigItem fading("fading", 3);
	BooleanConfigItem scrollback("scrollback", false);
	IntConfigItem digits_on("digits_on", 1500);
	ByteConfigItem display_on("display_on", 6);
	ByteConfigItem display_off("display_off", 22);
	StringConfigItem time_url("time_url", 80, String("http://time.niobo.us/getTime/America/New_York"));

	// LED config values
	ByteConfigItem hue("hue", 100);
	ByteConfigItem saturation("saturation", 255);
	BooleanConfigItem backlight("backlight", true);
	BooleanConfigItem hue_cycling("hue_cycling", false);
	ByteConfigItem led_scale("led_scale", 255);
	IntConfigItem cycle_time("cycle_time", 60);

	// Extra config values
	BooleanConfigItem dimming("dimming", true);
	BooleanConfigItem display("display", true);	// true == clock
	BooleanConfigItem hv("hv", true);
	ByteConfigItem voltage("voltage", 176);
	ByteConfigItem digit("digit", 0);
	ByteConfigItem count_speed("count_speed", 60);	// ticks per minute 0 to 60

	BaseConfigItem *clockSet[] = {
		// Clock
		&time_or_date,
		&date_format,
		&time_format,
		&hour_format,
		&fading,
		&scrollback,
		&digits_on,
		&display_on,
		&display_off,
		&time_url,
		0
	};

	BaseConfigItem *ledSet[] = {
		// LEDs
		&cycle_time,
		&hue,
		&saturation,
		&led_scale,
		&backlight,
		&hue_cycling,
		0
	};

	BaseConfigItem *extraSet[] = {
		// Extra
		&dimming,
		&display,
		&hv,
		&voltage,
		&digit,
		&count_speed,
		0
	};

	CompositeConfigItem clockConfig("clock", 0, clockSet);
	CompositeConfigItem ledConfig("led", 0, ledSet);
	CompositeConfigItem extraConfig("extra", 0, extraSet);

	BaseConfigItem *configSet[] = {
		// Clock
		&clockConfig,
		&ledConfig,
		&extraConfig,
		0
	};

	CompositeConfigItem config("set2", 0, configSet);
}	// End namespace

namespace ConfigSet3 {
	// Clock config values
	BooleanConfigItem time_or_date("time_or_date", true);
	ByteConfigItem date_format("date_format", 1);
	BooleanConfigItem time_format("time_format", false);
	BooleanConfigItem hour_format("hour_format", false);
	ByteConfigItem fading("fading", 0);
	BooleanConfigItem scrollback("scrollback", false);
	IntConfigItem digits_on("digits_on", 500);
	ByteConfigItem display_on("display_on", 6);
	ByteConfigItem display_off("display_off", 22);
	StringConfigItem time_url("time_url", 80, String("http://time.niobo.us/getTime/America/New_York"));

	// LED config values
	ByteConfigItem hue("hue", 100);
	ByteConfigItem saturation("saturation", 255);
	BooleanConfigItem backlight("backlight", true);
	BooleanConfigItem hue_cycling("hue_cycling", false);
	ByteConfigItem led_scale("led_scale", 255);
	IntConfigItem cycle_time("cycle_time", 60);

	// Extra config values
	BooleanConfigItem dimming("dimming", true);
	BooleanConfigItem display("display", true);	// true == clock
	BooleanConfigItem hv("hv", true);
	ByteConfigItem voltage("voltage", 176);
	ByteConfigItem digit("digit", 0);
	ByteConfigItem count_speed("count_speed", 60);	// ticks per minute 0 to 60

	BaseConfigItem *clockSet[] = {
		// Clock
		&time_or_date,
		&date_format,
		&time_format,
		&hour_format,
		&fading,
		&scrollback,
		&digits_on,
		&display_on,
		&display_off,
		&time_url,
		0
	};

	BaseConfigItem *ledSet[] = {
		// LEDs
		&cycle_time,
		&hue,
		&saturation,
		&led_scale,
		&backlight,
		&hue_cycling,
		0
	};

	BaseConfigItem *extraSet[] = {
		// Extra
		&dimming,
		&display,
		&hv,
		&voltage,
		&digit,
		&count_speed,
		0
	};

	CompositeConfigItem clockConfig("clock", 0, clockSet);
	CompositeConfigItem ledConfig("led", 0, ledSet);
	CompositeConfigItem extraConfig("extra", 0, extraSet);

	BaseConfigItem *configSet[] = {
		// Clock
		&clockConfig,
		&ledConfig,
		&extraConfig,
		0
	};

	CompositeConfigItem config("set3", 0, configSet);
}	// End namespace

namespace ConfigSet4 {
	// Clock config values
	BooleanConfigItem time_or_date("time_or_date", true);
	ByteConfigItem date_format("date_format", 1);
	BooleanConfigItem time_format("time_format", false);
	BooleanConfigItem hour_format("hour_format", false);
	ByteConfigItem fading("fading", 3);
	BooleanConfigItem scrollback("scrollback", true);
	IntConfigItem digits_on("digits_on", 1500);
	ByteConfigItem display_on("display_on", 6);
	ByteConfigItem display_off("display_off", 22);
	StringConfigItem time_url("time_url", 80, String("http://time.niobo.us/getTime/America/New_York"));

	// LED config values
	ByteConfigItem hue("hue", 100);
	ByteConfigItem saturation("saturation", 255);
	BooleanConfigItem backlight("backlight", true);
	BooleanConfigItem hue_cycling("hue_cycling", false);
	ByteConfigItem led_scale("led_scale", 255);
	IntConfigItem cycle_time("cycle_time", 60);

	// Extra config values
	BooleanConfigItem dimming("dimming", false);
	BooleanConfigItem display("display", false);	// true == clock
	BooleanConfigItem hv("hv", true);
	ByteConfigItem voltage("voltage", 200);
	ByteConfigItem digit("digit", 0);
	ByteConfigItem count_speed("count_speed", 1);	// ticks per minute 0 to 60

	BaseConfigItem *clockSet[] = {
		// Clock
		&time_or_date,
		&date_format,
		&time_format,
		&hour_format,
		&fading,
		&scrollback,
		&digits_on,
		&display_on,
		&display_off,
		&time_url,
		0
	};

	BaseConfigItem *ledSet[] = {
		// LEDs
		&cycle_time,
		&hue,
		&saturation,
		&led_scale,
		&backlight,
		&hue_cycling,
		0
	};

	BaseConfigItem *extraSet[] = {
		// Extra
		&dimming,
		&display,
		&hv,
		&voltage,
		&digit,
		&count_speed,
		0
	};

	CompositeConfigItem clockConfig("clock", 0, clockSet);
	CompositeConfigItem ledConfig("led", 0, ledSet);
	CompositeConfigItem extraConfig("extra", 0, extraSet);

	BaseConfigItem *configSet[] = {
		// Clock
		&clockConfig,
		&ledConfig,
		&extraConfig,
		0
	};

	CompositeConfigItem config("set4", 0, configSet);
}	// End namespace

namespace ConfigSet5 {
	// Clock config values
	BooleanConfigItem time_or_date("time_or_date", true);
	ByteConfigItem date_format("date_format", 1);
	BooleanConfigItem time_format("time_format", false);
	BooleanConfigItem hour_format("hour_format", false);
	ByteConfigItem fading("fading", 3);
	BooleanConfigItem scrollback("scrollback", true);
	IntConfigItem digits_on("digits_on", 1500);
	ByteConfigItem display_on("display_on", 6);
	ByteConfigItem display_off("display_off", 22);
	StringConfigItem time_url("time_url", 80, String("http://time.niobo.us/getTime/America/New_York"));

	// LED config values
	ByteConfigItem hue("hue", 100);
	ByteConfigItem saturation("saturation", 255);
	BooleanConfigItem backlight("backlight", true);
	BooleanConfigItem hue_cycling("hue_cycling", false);
	ByteConfigItem led_scale("led_scale", 255);
	IntConfigItem cycle_time("cycle_time", 60);

	// Extra config values
	BooleanConfigItem dimming("dimming", true);
	BooleanConfigItem display("display", false);	// true == clock
	BooleanConfigItem hv("hv", true);
	ByteConfigItem voltage("voltage", 176);
	ByteConfigItem digit("digit", 0);
	ByteConfigItem count_speed("count_speed", 60);	// ticks per minute 0 to 60

	BaseConfigItem *clockSet[] = {
		// Clock
		&time_or_date,
		&date_format,
		&time_format,
		&hour_format,
		&fading,
		&scrollback,
		&digits_on,
		&display_on,
		&display_off,
		&time_url,
		0
	};

	BaseConfigItem *ledSet[] = {
		// LEDs
		&cycle_time,
		&hue,
		&saturation,
		&led_scale,
		&backlight,
		&hue_cycling,
		0
	};

	BaseConfigItem *extraSet[] = {
		// Extra
		&dimming,
		&display,
		&hv,
		&voltage,
		&digit,
		&count_speed,
		0
	};

	CompositeConfigItem clockConfig("clock", 0, clockSet);
	CompositeConfigItem ledConfig("led", 0, ledSet);
	CompositeConfigItem extraConfig("extra", 0, extraSet);

	BaseConfigItem *configSet[] = {
		// Clock
		&clockConfig,
		&ledConfig,
		&extraConfig,
		0
	};

	CompositeConfigItem config("set5", 0, configSet);
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
BaseConfigItem *configSetGlobal[] = {
	&hostName,
	&currentSet,
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
	ByteConfigItem *digit = &ConfigSet1::digit;
	ByteConfigItem *count_speed = &ConfigSet1::count_speed;

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
			digit = static_cast<ByteConfigItem*>(config->get("digit"));
			count_speed = static_cast<ByteConfigItem*>(config->get("count_speed"));

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

byte getNormalizedBrightness() {
	static const double sensorSmoothCountLDR = 40;
	static const double minLDR = 0;
	static const double maxLDR = 1023;
	static double sensorLDRSmoothed = (maxLDR - minLDR) / 2;
	static unsigned long lastUpdate = 0;

	if (!(*CurrentConfig::dimming)) {
		return 100;
	}

	if (nowMs - lastUpdate > 50) {
		lastUpdate = nowMs;

		int adc = analogRead(ADCpin);
		double sensorDiff = adc - sensorLDRSmoothed;
		sensorLDRSmoothed += (sensorDiff / sensorSmoothCountLDR);
		sensorLDRSmoothed = constrain(sensorLDRSmoothed, minLDR, maxLDR);
	}

	return map(sensorLDRSmoothed, minLDR, maxLDR, 5, 100);
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

void sendInfoValues(AsyncWebSocketClient *client) {
	const size_t bufferSize = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(10);
	DynamicJsonBuffer jsonBuffer(bufferSize);

	JsonObject& root = jsonBuffer.createObject();
	root["type"] = "sv.init.clock";

	JsonObject& value = root.createNestedObject("value");
	value["esp_boot_version"] = ESP.getBootVersion();
	value["esp_free_heap"] = ESP.getFreeHeap();
	value["esp_sketch_size"] = ESP.getSketchSize();
	value["esp_sketch_space"] = ESP.getFreeSketchSpace();
	value["esp_flash_size"] = ESP.getFlashChipRealSize();
	value["esp_chip_id"] = chipId;
	value["wifi_ip_address"] = WiFi.localIP().toString();
	value["wifi_mac_address"] = WiFi.macAddress();
	value["wifi_ssid"] = WiFi.SSID();
	value["wifi_ap_ssid"] = ssid;

    size_t len = root.measureLength();
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
        root.printTo((char *)buffer->get(), len + 1);
        client->text(buffer);
    }
}

void sendClockValues(AsyncWebSocketClient *client) {
	String json("{\"type\":\"sv.init.clock\", \"value\":{");
	BaseConfigItem *items = rootConfig.get((CurrentConfig::name + "_name").c_str());
	char *sep = "";
	if (items != 0) {
		json.concat("\"set_icon_1\":");
		json.concat(items->toJSON());
		sep=",";
	}
	items = CurrentConfig::config->get("clock");
	if (items != 0) {
		json.concat(sep);
		json.concat(items->toJSON(true));
	}
	json.concat("}}");
	client->text(json);
}

void sendLEDValues(AsyncWebSocketClient *client) {
	String json("{\"type\":\"sv.init.leds\", \"value\":{");
	BaseConfigItem *items = rootConfig.get((CurrentConfig::name + "_name").c_str());
	char *sep = "";
	if (items != 0) {
		json.concat("\"set_icon_2\":");
		json.concat(items->toJSON());
		sep=",";
	}
	items = CurrentConfig::config->get("led");
	if (items != 0) {
		json.concat(sep);
		json.concat(items->toJSON(true));
	}
	json.concat("}}");
	client->text(json);
}

void sendExtraValues(AsyncWebSocketClient *client) {
	String json("{\"type\":\"sv.init.extra\", \"value\":{");
	BaseConfigItem *items = rootConfig.get((CurrentConfig::name + "_name").c_str());
	char *sep = "";
	if (items != 0) {
		json.concat("\"set_icon_3\":");
		json.concat(items->toJSON());
		sep=",";
	}
	items = CurrentConfig::config->get("extra");
	if (items != 0) {
		json.concat(sep);
		json.concat(items->toJSON(true));
	}
	json.concat("}}");
	client->text(json);
}

void sendPresetValues(AsyncWebSocketClient *client) {
	String json("{\"type\":\"sv.init.presets\", \"value\": {\"");
	json.concat(CurrentConfig::name);
	json.concat("\":1");
	BaseConfigItem *items = rootConfig.get("preset_names");
	if (items != 0) {
		json.concat(",");
		// Without the {}
		json.concat(items->toJSON(true));
		// Change set1_name to set1_label
		json.replace("name", "label");
	}
	json.concat("}}");
	client->text(json);
}

void sendPresetNames(AsyncWebSocketClient *client) {
	String json("{\"type\":\"sv.init.preset_names\", \"value\":");
	BaseConfigItem *items = rootConfig.get("preset_names");
	if (items != 0) {
		json.concat(items->toJSON());
	} else {
		json.concat("}");
	}
	json.concat("}");
	client->text(json);
}

void updateValue(int screen, String pair) {
	int index = pair.indexOf(':');
	DEBUG(pair)
	// _key has to hang around because key points to an internal data structure
	String _key = pair.substring(0, index);
	const char* key = _key.c_str();
	String value = pair.substring(index+1);
	if (screen == 4) {
		CurrentConfig::setCurrent(key);
		BooleanConfigItem temp(CurrentConfig::name.c_str(), true);
		broadcastUpdate(temp);
	} else if (screen == 6) {
		BaseConfigItem *item = rootConfig.get(key);
		if (item != 0) {
			item->fromString(value);
			item->put();
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

/*
 * Handle application protocol
 */
void handleWSMsg(AsyncWebSocketClient *client, char *data) {
	String wholeMsg(data);
	int code = wholeMsg.substring(0, wholeMsg.indexOf(':')).toInt();

	switch (code) {
	case 1:
		sendClockValues(client);
		break;
	case 2:
		sendLEDValues(client);
		break;
	case 3:
		sendExtraValues(client);
		break;
	case 4:
		sendPresetValues(client);
		break;
	case 5:
		sendInfoValues(client);
		break;
	case 6:
		sendPresetNames(client);
		break;
	case 9:
		String message = wholeMsg.substring(wholeMsg.indexOf(':')+1);
		int screen = message.substring(0, message.indexOf(':')).toInt();
		String pair = message.substring(message.indexOf(':')+1);
		updateValue(screen, pair);
		break;
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

const byte numLEDs = 2;

Adafruit_NeoPixel leds = Adafruit_NeoPixel(numLEDs, LEDpin, NEO_GRB + NEO_KHZ800);
byte redVal;
byte greenVal;
byte blueVal;

void setLedColorHSV(byte h, byte s, byte v) {
  // this is the algorithm to convert from RGB to HSV
  h = (h * 192) / 256;  // 0..191
  unsigned int  i = h / 32;   // We want a value of 0 thru 5
  unsigned int f = (h % 32) * 8;   // 'fractional' part of 'i' 0..248 in jumps

  unsigned int sInv = 255 - s;  // 0 -> 0xff, 0xff -> 0
  unsigned int fInv = 255 - f;  // 0 -> 0xff, 0xff -> 0
  byte pv = v * sInv / 256;  // pv will be in range 0 - 255
  byte qv = v * (256 - s * f / 256) / 256;
  byte tv = v * (256 - s * fInv / 256) / 256;

  switch (i) {
  case 0:
    redVal = v;
    greenVal = tv;
    blueVal = pv;
    break;
  case 1:
    redVal = qv;
    greenVal = v;
    blueVal = pv;
    break;
  case 2:
    redVal = pv;
    greenVal = v;
    blueVal = tv;
    break;
  case 3:
    redVal = pv;
    greenVal = qv;
    blueVal = v;
    break;
  case 4:
    redVal = tv;
    greenVal = pv;
    blueVal = v;
    break;
  case 5:
    redVal = v;
    greenVal = pv;
    blueVal = qv;
    break;
  }
}

void ledDisplay(bool on=true) {
	// Scale normalized brightness to range 0..255
	byte brightness = ((int)getNormalizedBrightness()) * 255 / 100;

	// Now use a square
	brightness = (long)brightness * (long)brightness / 255L;

	// Scale brightness by ledScale
	brightness = ((int)brightness) * *CurrentConfig::led_scale / 255;

	brightness = map(brightness, 0, 255, 5, 255);

	if (!on) {
		brightness = 0;
	}

	setLedColorHSV(*CurrentConfig::hue, *CurrentConfig::saturation, brightness);

	for (int n = 0; n < numLEDs; n++) {
//		setLedColorHSV((hue + n * 256 / numLEDs) % 256, saturation, brightness);
		leds.setPixelColor(n, redVal, greenVal, blueVal);
	}

	leds.show();
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

void chrenInterrupt() {
	chren = digitalRead(CHRENpin) > LOW;
}

void chr0Interrupt() {
	chr0 = digitalRead(CHR0pin) > LOW;
}

void chr1Interrupt() {
	chr1 = digitalRead(CHR1pin) > LOW;
}

#ifdef I2C
Button button(&mcp, MCPButtonPin);
RotaryEncoder encoder(&mcp, MCPEncoderA, MCPEncoderB);
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

void SetupServer() {
	DEBUG("SetupServer()");
	hostName = String(hostnameParam->getValue());
	hostName.put();
	config.commit();
	DEBUG(hostName.value);
	MDNS.begin(hostName.value.c_str());
    MDNS.addService("http", "tcp", 80);
	StartOTA();

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

SoftMSTimer::TimerInfo *infos[] = {
		&ledTimer,
		&eepromUpdateTimer,
#ifdef USE_NTP
		&ntpRequestTimer,
		&tzInfoTimer,
		&ntpResponseTimer,
#else
		&syncTimeTimer,
#endif
		0
};

SoftMSTimer timedFunctions(infos);
byte oldVoltage = 176;
bool oldHV = true;

void setup()
{
	chipId.toUpperCase();
	ssid = "STC-" + chipId;

#ifdef I2C
	Wire.pins(SDApin, SCLpin);

	oled.begin();
	oled.setFont(u8x8_font_chroma48medium8_r);
#endif

//	Serial.begin(921600);
	Serial.begin(115200);
	EEPROM.begin(1024);
	SPIFFS.begin();
#ifdef USE_NTP
	ntpRequest.begin(NTP_LOCAL_PORT);
#endif

	initFromEEPROM();

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

	// Attach interrupts in case they rise after this point
	attachInterrupt(CHRENpin, chrenInterrupt, RISING);
	attachInterrupt(CHR0pin, chr0Interrupt, RISING);
	attachInterrupt(CHR1pin, chr1Interrupt, RISING);

	// Read the current state, in case they are already high
	chren = digitalRead(CHRENpin) > LOW;
	chr0 = digitalRead(CHR0pin) > LOW;
	chr1 = digitalRead(CHR1pin) > LOW;

	// Initialize voltage level
	oldVoltage = *CurrentConfig::voltage;
	pinMode(VADJpin, OUTPUT);
	analogWriteFreq(4000);
	analogWriteRange(100);
	analogWrite(VADJpin, getHVDutyCycle(oldVoltage));

#ifdef I2C
	lis.begin();
	lis.setRange(LIS3DH_RANGE_4_G);   // 2, 4, 8 or 16 G!

	// Initialize expansion ports, default address: 0x20
	mcp.begin();						// Yet another Wire.begin...
	mcp.pinMode(MCPButtonPin, INPUT);	// Regular switch
	mcp.pullUp(MCPButtonPin, HIGH);		// turn on a 100K pullup internally, so switch closed = low.
	mcp.pinMode(MCPEncoderA, INPUT);	// Rotary encoder A
	mcp.pullUp(MCPEncoderA, HIGH);		// turn on a 100K pullup internally
	mcp.pinMode(MCPEncoderB, INPUT);	// Rotary encoder B
	mcp.pullUp(MCPEncoderB, HIGH);		// turn on a 100K pullup internally
#endif

	// Enable LEDs
	leds.begin();
	ledDisplay();

	nowMs = millis();
	nixieDriver.init();
	nixieClock.init();

	DEBUG("Voltage on")
	oldHV = *CurrentConfig::hv;
	digitalWrite(VENpin, oldHV ? LOW : HIGH);

	DEBUG("Set wifiManager")
	wifiManager.setDebugOutput(false);
	wifiManager.setConnectTimeout(10);
	wifiManager.addParameter(hostnameParam);
	wifiManager.setSaveConfigCallback(SetupServer);
    wifiManager.startConfigPortalModeless(ssid.c_str(), "secretsauce");
	DEBUG("Exit setup")
}

void loop()
{
	wifiManager.loop();
#ifdef OTA
	ArduinoOTA.handle();
#endif

	nowMs = millis();

#ifdef I2C
	if (button.clicked()) {
		clicks++;
	}

	rotation += encoder.getRotation();
#endif

	if (oldVoltage != *CurrentConfig::voltage) {
		oldVoltage = *CurrentConfig::voltage;
		analogWrite(VADJpin, getHVDutyCycle(oldVoltage));
	}

	nixieDriver.setBrightness(getNormalizedBrightness());
	if (timeInitialized || !*CurrentConfig::display) {
		nixieClock.setClockMode(*CurrentConfig::display);
		nixieClock.setCountSpeed(*CurrentConfig::count_speed);
	} else {
		nixieClock.setClockMode(false);
		nixieClock.setCountSpeed(60);
	}
	nixieClock.setFadeMode(*CurrentConfig::fading);
	nixieClock.setTimeMode(*CurrentConfig::time_or_date);
	nixieClock.setDateFormat(*CurrentConfig::date_format);
	nixieClock.setShowSeconds(*CurrentConfig::time_format);
	nixieClock.set12hour(*CurrentConfig::hour_format);
	nixieClock.setDigit(*CurrentConfig::digit);
	nixieClock.setOnOff(*CurrentConfig::display_on, *CurrentConfig::display_off);
	nixieClock.setDigitsOn(*CurrentConfig::digits_on);
	nixieClock.setScrollback(*CurrentConfig::scrollback);
	bool clockOn = nixieClock.isOn();

	bool newHV = *CurrentConfig::hv && clockOn;

	if (oldHV != newHV) {
		oldHV = newHV;
		digitalWrite(VENpin, oldHV ? LOW : HIGH);
	}

	nixieClock.loop(nowMs);
	if (nixieClock.getNixieDigit() != *CurrentConfig::digit) {
		*CurrentConfig::digit = nixieClock.getNixieDigit();
		if (nixieClock.getACPCount() == 0) {
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

#ifdef I2C
	lis.read();      // get X Y and Z data at once
#endif
}

