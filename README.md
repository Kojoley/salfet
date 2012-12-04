Small and Little Frameset Extractor Tool
======
This is small tool based on FFmpeg. Primary goals are dumping frames.
All you need is video file, output template and timestamps. 

Build system based on GNU autotools and pkg-config. Because of that
You should have installed aclocal, autoconf and pkg-config.

Simple build with: ./autogen.sh && make
If FFmpeg is not installed in system, but builded in some directory
Provide this directory as ./autogen.sh FFMPEG_PATH=/path/to/ffmpeg
You can enable static linking with libs by passing --enable-static-link
More information upon building you can get by ./autogen.sh --help

Simple usage is: ./salfet input.mpg output_%d.jpg 0 1 2 3
For help in usage, please call executable with --help
