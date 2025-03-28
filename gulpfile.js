const gulp = require('gulp');
const plumber = require('gulp-plumber');
const htmlmin = require('gulp-htmlmin');
const cleancss = require('gulp-clean-css');
const uglify = require('gulp-uglify');
const gzip = require('gulp-gzip');
const del = require('del');
const useref = require('gulp-useref');
const gulpif = require('gulp-if');
const inline = require('gulp-inline');
const inlineImages = require('gulp-css-inline-images');
const favicon = require('gulp-base64-favicon');
const fs = require("fs");
const readLine = require("readline");
const log = require("fancy-log");
const spawn = require('child_process').spawn;
const endOfLine = require('os').EOL;
const propertiesReader = require('properties-reader');

var web_dist = "data";
var spiffs_file = "spiffs.bin";
var web_src = "web";

var props = {
	eclipse_home: process.env.ECLIPSE_HOME,
	os: process.env.OS,
	project_loc: '.'
}

var sloeberProperties;
var arch;

function resolveProperty(props, key, level) {
	var value = props[key];

	if (value && level <= 5) {
		value = value.replace(/\$\{[^\}]+\}/g, function(match, offset, s) {
			var resolved = resolveProperty(props, match.substring(2, match.length-1), level+1);
			if (resolved) {
				return resolved;
			} else {
				return match;
			}
		});
	}

	return value;
}

function resolveProperties(props) {
	Object.keys(props).forEach(function(key, index) {
		props[key] = resolveProperty(props, key, 0);
		if (props[key]) {
			props[key] = props[key].replace(/\\\\/g, '\\').replace(/\\:/g, ':')
		}
	});
}

function getCSVPartitionData(partitionFile) {
	var csv = fs.readFileSync(partitionFile, 'utf-8').split('\n');
	var headers = csv[0].split(',');
	var entries = [];
	for (var line=1; line<csv.length; line++) {
		var columns = csv[line].split(',');
		var entry = [];

		for (var col=1; col<columns.length;col++) {
			var header = headers[col];
			var value = columns[col];
			if (header) header = header.trim();
			if (value) value = value.trim();
			entry[header] = value;
		}

		entries[columns[0].trim()] = entry;
	}
	log("partitionData: ", entries);

	props['A.BUILD.SPIFFS_SIZE'] = entries['spiffs']['Size'];
	props['A.BUILD.SPIFFS_START'] = entries['spiffs']['Offset'];
	props['A.BUILD.SPIFFS_PAGESIZE'] = 256;
	props['A.BUILD.SPIFFS_BLOCKSIZE'] = 4096;
}

function getPlatformPartitionData() {
	var platformFile = sloeberProperties.get('Config.Release.board.BOARD.TXT').replace('${SLOEBER_HOME}', props['eclipse_home']);
	var spiffKey = sloeberProperties.get('Config.Release.board.BOARD.MENU.FlashSize');
	var platformKeyRoot = '';
	if (spiffKey) {
		platformKeyRoot = sloeberProperties.get('Config.Release.board.BOARD.ID') + '.menu.FlashSize.' + spiffKey;
	} else {
		spiffKey = sloeberProperties.get('Config.Release.board.BOARD.MENU.eesz');
		platformKeyRoot = sloeberProperties.get('Config.Release.board.BOARD.ID') + '.menu.eesz.' + spiffKey;
	}
	var partitionProperties = propertiesReader(platformFile);

	props['A.BUILD.SPIFFS_START'] = '0x' + partitionProperties.get(platformKeyRoot + '.build.spiffs_start').toString(16);
	props['A.BUILD.SPIFFS_END'] = '0x' + partitionProperties.get(platformKeyRoot + '.build.spiffs_end').toString(16);
	props['A.BUILD.SPIFFS_SIZE'] = parseInt(props['A.BUILD.SPIFFS_END'].substring(2),16) - parseInt(props['A.BUILD.SPIFFS_START'].substring(2), 16);
	props['A.BUILD.SPIFFS_PAGESIZE'] = 256;
	props['A.BUILD.SPIFFS_BLOCKSIZE'] = partitionProperties.get(platformKeyRoot + '.build.spiffs_blocksize');
}

function getPartitionData() {
	var partitionFile = 'Release/partitions.csv';
	if (fs.existsSync(partitionFile)) {
		getCSVPartitionData(partitionFile);
	} else {
		getPlatformPartitionData();
	}
}

gulp.task('load_properties', function(cb) {
	var sloeberFile = '.sproject';
	sloeberProperties = propertiesReader(sloeberFile);

	sloeberProperties.each((key, value) => {
	    console.log(key + ":" + value);
	});

	var boardPath = sloeberProperties.get('Config.Release.board.BOARD.TXT');
	arch = boardPath.includes('8266') ? 'esp8266' :
			boardPath.includes('esp32') ? 'esp32' :
			'n/f';

	if (arch == 'n/f') {
		console.log(`stderr: Unknown board architecture`);
		process.exit(1);
	}

	getPartitionData();

	props["A.UPLOAD.SPEED"] = sloeberProperties.get('Config.Release.board.BOARD.MENU.UploadSpeed');
	if (!props["A.UPLOAD.SPEED"]) {
		props["A.UPLOAD.SPEED"] = sloeberProperties.get('Config.Release.board.BOARD.MENU.baud');
	}
	props["A.SERIAL.PORT"] = sloeberProperties.get('Config.Release.board.UPLOAD.PORT');
	props['A.BUILD.FLASH_MODE'] = 'dio';

	var mkspiffsPath;
	if (arch == 'esp8266') {
		mkspiffsPath='/arduinoPlugin/packages/esp8266/tools/mkspiffs/0.1.2';
	} else {
		mkspiffsPath='/arduinoPlugin/packages/esp32/tools/mkspiffs/0.2.3';
	}

	var esptoolPath;
	if (arch == 'esp8266') {
		esptoolPath = '/arduinoPlugin/packages/esp8266/tools/esptool/0.4.9';
	} else {
		esptoolPath = '/arduinoPlugin/packages/esp32/tools/esptool_py/3.0.0';
	}

	props['A.TOOLS.MKSPIFFS.PATH'] = props['eclipse_home'] + mkspiffsPath;
	if (props['os'] == 'windows') {
		props["A.TOOLS.MKSPIFFS.CMD"] = "mkspiffs.exe";
	} else {
		props["A.TOOLS.MKSPIFFS.CMD"] = "mkspiffs";
	}
	props["A.TOOLS.ESPTOOL.PATH"] = props['eclipse_home'] + esptoolPath;
	if (props['os'] == 'windows') {
		props["A.TOOLS.ESPTOOL.CMD"] = 'esptool.exe';
	} else {
		props["A.TOOLS.ESPTOOL.CMD"] = 'esptool';
	}
	props['A.UPLOAD.RESETMETHOD'] = sloeberProperties.get('Config.Release.board.BOARD.MENU.ResetMethod');
	if (!props['A.UPLOAD.RESETMETHOD']) {
		props['A.UPLOAD.RESETMETHOD'] = 'nodemcu';	// A hack
	}

	cb();	// Done
});

global.esp8266Upload = function () {
	var args = [
		"-cd",
		props["A.UPLOAD.RESETMETHOD"],
		"-cb",
		props["A.UPLOAD.SPEED"],
		"-cp",
		props["A.SERIAL.PORT"],
		"-ca",
		props["A.BUILD.SPIFFS_START"],
		"-cf",
		props["project_loc"] + "/" + spiffs_file
	];

	var cmd = props["A.TOOLS.ESPTOOL.PATH"] + "/" + props["A.TOOLS.ESPTOOL.CMD"];
	log("Spawning task: ", cmd);
	log(JSON.stringify(args));
	return spawn(cmd, args);
}

global.esp32Upload = function() {
	var args = [
		"--chip", "esp32",
		"--baud", props["A.UPLOAD.SPEED"],
		"--port", props["A.SERIAL.PORT"],
		"--before", "default_reset",
		"--after", "hard_reset", "write_flash",
		"-z", "--flash_mode", props['A.BUILD.FLASH_MODE'],
		"--flash_size", "detect", props["A.BUILD.SPIFFS_START"],
		props["project_loc"] + "/" + spiffs_file
	];

	var cmd = props["A.TOOLS.ESPTOOL.PATH"] + "/" + props["A.TOOLS.ESPTOOL.CMD"];
	log("Spawning task: ", cmd);
	log(JSON.stringify(args));
	return spawn(cmd, args);
}

gulp.task('wired_spiff_upload', gulp.series('load_properties', function(cb) {
	proc = global[arch + 'Upload']();

	proc.stdout.on('data', (data) => {
		console.log(`stdout: ${data}`);
	});

	proc.stderr.on('data', (data) => {
		console.log(`stderr: ${data}`);
	});

    proc.on('exit', function (code) {
        cb();
    });
}));

/* Clean destination folder */
gulp.task('clean', function() {
    return del([web_dist + '/*']);
});

gulp.task('buildfs_inline', gulp.series('clean', function() {
    return gulp.src(web_src + '/*.html')
        .pipe(favicon())
        .pipe(inline({
            base: web_src,
            js: uglify,
            css: [cleancss],
            disabledTypes: ['svg', 'img']
        }))
    	.pipe(inlineImages({
            webRoot: web_src
    	}))
        .pipe(htmlmin({
            collapseWhitespace: true,
            removeComments: true,
            minifyCSS: true,
            minifyJS: true
        }))
        .pipe(gzip())
        .pipe(gulp.dest(web_dist));
}));

gulp.task('erase_flash', gulp.series('load_properties', function(cb) {
	var args = [
		"--chip", arch,
		"--port", props["A.SERIAL.PORT"],
		"erase_flash"
	];

	var cmd = props["A.TOOLS.ESPTOOL.PATH"] + "/" + props["A.TOOLS.ESPTOOL.CMD"];
	log("Spawning task: ", cmd);
	var proc = spawn(cmd, args);

	proc.stdout.on('data', (data) => {
		console.log(`stdout: ${data}`);
	});

	proc.stderr.on('data', (data) => {
		console.log(`stderr: ${data}`);
	});

	proc.on('exit', function (code) {
	    cb();
	});
}));

gulp.task('show_spiffs', gulp.series('load_properties', function(cb) {
	var args = [
		"-i",
		props["project_loc"] + "/" + spiffs_file
	];

	var cmd = props["A.TOOLS.MKSPIFFS.PATH"] + "/" + props["A.TOOLS.MKSPIFFS.CMD"];
	log("Spawning task: ", cmd);
	var proc = spawn(cmd, args);

	proc.stdout.on('data', (data) => {
		console.log(`stdout: ${data}`);
	});

	proc.stderr.on('data', (data) => {
		console.log(`stderr: ${data}`);
	});

    proc.on('exit', function (code) {
        cb();
    });
}));

gulp.task('mkspiffs', gulp.series('buildfs_inline', 'load_properties', function(cb) {
	var args = [
		"-d",
		"5",
		"-c",
		props["project_loc"] + "/" + web_dist,
		"-p",
		props["A.BUILD.SPIFFS_PAGESIZE"],
		"-b",
		props["A.BUILD.SPIFFS_BLOCKSIZE"],
		"-s",
		props["A.BUILD.SPIFFS_SIZE"],
		spiffs_file
	];

	var cmd = props["A.TOOLS.MKSPIFFS.PATH"] + "/" + props["A.TOOLS.MKSPIFFS.CMD"];
	log("Spawning task: ", cmd);
	log(args);
	var proc = spawn(cmd, args);

	proc.stdout.on('data', (data) => {
		console.log(`stdout: ${data}`);
	});

	proc.stderr.on('data', (data) => {
		console.log(`stderr: ${data}`);
	});

    proc.on('exit', function (code) {
        cb();
    });
}));

/* Copy static files */
gulp.task('files', function() {
    return gulp.src([
    		web_src + '/**/*.{jpg,jpeg,png,ico,gif}',
    		web_src + '/fsversion'
        ])
        .pipe(gulp.dest(web_dist));
});

/* Process HTML, CSS, JS  --- INLINE --- */
gulp.task('inline', function() {
    return gulp.src(web_src + '/*.html')
        .pipe(inline({
            base: web_src,
            js: uglify,
            css: cleancss,
            disabledTypes: ['svg', 'img']
        }))
        .pipe(htmlmin({
            collapseWhitespace: true,
            removeComments: true,
            minifyCSS: true,
            minifyJS: true
        }))
        .pipe(gzip())
        .pipe(gulp.dest(web_dist));
});


/* Process HTML, CSS, JS */
gulp.task('html', function() {
    return gulp.src(web_src + '/*.html')
        .pipe(useref())
        .pipe(plumber())
        .pipe(gulpif('*.css', cleancss()))
        .pipe(gulpif('*.js', uglify()))
        .pipe(gulpif('*.html', htmlmin({
            collapseWhitespace: true,
            removeComments: true,
            minifyCSS: true,
            minifyJS: true
        })))
        .pipe(gzip())
        .pipe(gulp.dest(web_dist));
});

/* Build file system */
gulp.task('buildfs', gulp.series('clean', 'files', 'html'));
gulp.task('buildfs2', gulp.series('clean', 'files', 'inline'));
gulp.task('default', gulp.series('clean', 'buildfs_inline'));
