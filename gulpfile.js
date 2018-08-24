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

var web_dist = "data";
var spiffs_file = "spiffs.bin";
var web_src = "web";

var props = {
	eclipse_home: process.argv[3],
	project_loc: process.argv[4],
	project_name: process.argv[5]
}

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
	});
}

gulp.task('load_properties', function(cb) {
	var lines = fs.readFileSync(".settings/org.eclipse.cdt.core.prefs", 'utf-8')
    .split(endOfLine)
    .filter(Boolean);
	
	for (var i=0; i<lines.length; i++) {
	    var matched = lines[i].match(/.*\/([AJ].+)\/value=(.+)$/);
	    if (matched) {
	    	props[matched[1]] = matched[2];
	    }
	}
	
	resolveProperties(props);
	props['A.BUILD.SPIFFS_SIZE'] = parseInt(props['A.BUILD.SPIFFS_END'].substring(2),16) - parseInt(props['A.BUILD.SPIFFS_START'].substring(2), 16);
	
	cb();	// Done
});

gulp.task('wired_spiff_upload', ['load_properties'], function(cb) {
	var args = [
		"-cd ",
		props["A.UPLOAD.RESETMETHOD"],
		"-cb ",
		props["A.UPLOAD.SPEED"],
		"-cp ",
		props["A.SERIAL.PORT"],
		"-ca ",
		props["A.BUILD.SPIFFS_START"],
		"-cf ",
		props["project_loc"] + "/" + spiffs_file
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
});

gulp.task('mkspiffs', ['buildfs_inline', 'load_properties'], function(cb) {
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
});

/* Clean destination folder */
gulp.task('clean', function() {
    return del([web_dist + '/*']);
});
 
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
})
 
gulp.task('buildfs_inline', ['clean'], function() {
    return gulp.src(web_src + '/*.html')
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
gulp.task('buildfs', ['clean', 'files', 'html']);
gulp.task('buildfs2', ['clean', 'files', 'inline']);
gulp.task('default', ['clean', 'buildfs_inline']);
 