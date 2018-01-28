#!/usr/bin/env node

/*
 * A test server
 */
'use strict';

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

app.use(express.static('web'));

var sendValues = function(conn, screen) {
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

var state = {
	"1": {
		'time_or_date':  true, 
		'date_format':  1, 
		'time_format':  true, 
		'fading':  2, 
		'scrollback':  true, 
		'digits_on':  1750, 
		'display_on':  10, 
		'display_off':  20, 
		'time_server':  'http://niobo.us/blah' 
	},
	"2": {
		'backlight': true, 
		'hue_cycling': false, 
		'cycle_time': 100, 
		'hue': 180, 
		'saturation': 190, 
		'brightness': 200
	},
	"3": {
		'dimming': true, 
		'hv': true, 
		'display': true, 
		'voltage': 176
	},
	"4": {
		'set3': true
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

	if (screen == 4) {
		state[screen] = { key : value }
	} else {
		state[screen][key] = value;
	}
	broadcastUpdate(conn, key, value);
}

var updateHue = function(conn) {
	var hue = state['2']['hue'];
	hue = (hue + 1) % 256;
	updateValue(conn, 2, "hue:" + hue);
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

