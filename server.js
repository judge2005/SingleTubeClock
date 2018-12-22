#!/usr/bin/env node

/*
 * A test server
 */
'use strict';

var expressStaticGzip = require("express-static-gzip");
var express = require('express');
var http = require('http');
var ws = require('ws');

var app = new express();

var server = http.createServer(app);

var wss = new ws.Server({ server });

app.use(function(req, res, next) {
    console.log(req.originalUrl);
    next();
});

app.use(expressStaticGzip("web"));

var pages = {
		"type":"sv.init.menu",
		"value": [
			{"1": { "url" : "clock.html", "title" : "Clock" }},
			{"2": { "url" : "leds.html", "title" : "LEDs" }},
			{"3": { "url" : "extra.html", "title" : "Extra" }},
			{"7": { "url" : "ups.html", "title" : "UPS" }},
			{"8": { "url" : "sync.html", "title" : "Sync" }},
			{"4": { "url" : "presets.html", "title" : "Presets" }},
			{"5": { "url" : "info.html", "title" : "Info" }},
			{"6": { "url" : "preset_names.html", "title" : "Preset Names", "noNav" : true}}		
		]
	}


var sendValues = function(conn, screen) {
}

var sendPages = function(conn) {
	var json = JSON.stringify(pages);
	conn.send(json);	
	console.log(json);
}

var sendClockValues = function(conn) {
	var json = '{"type":"sv.init.clock","value":';
	json += JSON.stringify(state[1]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendLEDValues = function(conn) {
	var json = '{"type":"sv.init.leds","value":';
	json += JSON.stringify(state[2]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendExtraValues = function(conn) {
	var json = '{"type":"sv.init.extra","value":';
	json += JSON.stringify(state[3]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendPresetValues = function(conn) {
	var json = '{"type":"sv.init.presets","value":';
	json += JSON.stringify(state[4]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendInfoValues = function(conn) {
	var json = '{"type":"sv.init.info","value":';
	json += JSON.stringify(state[5]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendPresetNames = function(conn) {
	var json = '{"type":"sv.init.preset_names","value":';
	json += JSON.stringify(state[6]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendUPS = function(conn) {
	var json = '{"type":"sv.init.ups","value":';
	json += JSON.stringify(state[7]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var sendSync = function(conn) {
	var json = '{"type":"sv.init.sync","value":';
	json += JSON.stringify(state[8]);
	json += '}';
	console.log(json);
	conn.send(json);	
}

var state = {
	"1": {
		'time_or_date':  true, 
		'date_format':  1, 
		'time_format':  true, 
		'fading':  2,
		'indicator': 1,
		'scrollback':  true, 
		'digits_on':  1750, 
		'display_on':  10, 
		'display_off':  20,
		'tube_type': 1,
		'time_server':  'http://niobo.us/blah',
		'set_icon_clock': 'Foo'
	},
	"2": {
		'backlight': true, 
		'hue_cycling': false, 
		'cycle_time': 100, 
		'tube_type': 1,
		'hue': 180, 
		'saturation': 190, 
		'brightness': 200,
		'set_icon_leds': 'Bar'
	},
	"3": {
		'dimming': true, 
		'hv': true, 
		'display': true, 
		'tube_type': 1,
		'voltage': 176,
		'set_icon_extra': 'Bletch'
	},
	"4": {
		'preset' : 'set3'
	},
	"5": {
		'esp_boot_version' : "1234",
		'esp_free_heap' : "5678",
		'esp_sketch_size' : "90123",
		'esp_sketch_space' : "4567",
		'esp_flash_size' : "8901",
		'esp_chip_id' : "chip id",
		'wifi_ip_address' : "192.168.1.1",
		'wifi_mac_address' : "0E:12:34:56:78",
		'wifi_ssid' : "STC-Wonderful"
	},
	"6": {
		'set1_name' : 'Clock 1',
		'set2_name' : 'Clock 2',
		'set3_name' : 'Clock 3',
		'set4_name' : 'Conditioner',
		'set5_name' : 'Manual'
	},
	"7": {
		'charge_rate' : '1',
		'lpm' : 'off',
		'wakeup_time' : '30',
		'sensitivity' : '45',
		'power' : 'Battery',
		'charge_rate_txt' : '500mA',
		'usb_rating' : '>500mA',
		'set_icon_ups' : 'Blag'
	},
	"8": {
		'sync_port' : '12345',
		'sync_role' : '0',
		'set_icon_sync' : 'burble'
	}
}

var broadcastUpdate = function(conn, field, value) {
	var json = '{"type":"sv.update","value":{' + '"' + field + '":' + value + '}}';
	console.log(json);
	try {
		conn.send(json);
	} catch (e) {
		
	}
}

var updateValue = function(conn, screen, pair) {
	console.log(pair);
	var index = pair.indexOf(':');

	var key = pair.substring(0, index);
	var value = pair.substring(index+1);
	try {
		value = JSON.parse(value);		
	} catch (e) {
		
	}

	if (screen == 4 || screen == 6) {
		state[screen][key] = '"' + value + '"';
	} else {
		state[screen][key] = value;
	}
	broadcastUpdate(conn, key, state[screen][key]);
}

var updateHue = function(conn) {
	var hue = state['2']['hue'];
	hue = (hue + 1) % 256;
//	updateValue(conn, 2, "hue:" + hue);
}

wss.on('connection', function(conn) {
    console.log('connected');
	var hueTimer = setInterval(updateHue, 500, conn);
	
    //connection is up, let's add a simple simple event
	conn.on('message', function(message) {

        //log the received message and send it back to the client
        console.log('received: %s', message);
    	var code = parseInt(message.substring(0, message.indexOf(':')));
 
    	switch (code) {
    	case 0:
    		sendPages(conn);
    		break;
    	case 1:
    		sendClockValues(conn);
    		break;
    	case 2:
    		sendLEDValues(conn);
    		break;
    	case 3:
    		sendExtraValues(conn);
    		break;
    	case 4:
    		sendPresetValues(conn);
    		break;
    	case 5:
    		sendInfoValues(conn);
    		break;
    	case 6:
    		sendPresetNames(conn);
    		break;
    	case 7:
    		sendUPS(conn);
    		break;
    	case 8:
    		sendSync(conn);
    		break;
    	case 9:
    		message = message.substring(message.indexOf(':')+1);
    		var screen = message.substring(0, message.indexOf(':'));
    		var pair = message.substring(message.indexOf(':')+1);
    		updateValue(conn, screen, pair);
    		break;
    	}
    });
	
	conn.on('close', function() {
		clearInterval(hueTimer);
	});
});

//start our server
server.listen(process.env.PORT || 8080, function() {
    console.log('Server started on port' + server.address().port + ':)');
});

