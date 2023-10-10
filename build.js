var gulp = require('gulp');
require('./gulpfile.js');

gulp.series(gulp.task(process.argv[2]))();
