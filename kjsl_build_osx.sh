#!/bin/bash

# KJSL: script to build FFmpeg on OSX

brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

curl -O -L https://github.com/kevleyski/aom/archive/master.zip
unzip master.zip
cd aom-master
./configure --prefix="$HOME/ffmpeg_build" --cpu=sse3 --enable-av1 --disable-shared
make
make install
cd ..

# build FFmpeg 4 (with AV1)
 ./configure  --prefix=/usr/local --enable-gpl --enable-nonfree \
 --enable-libass --enable-libfdk-aac --enable-libfreetype \
 --enable-libopus --enable-libtheora --enable-libvorbis \
 --enable-libvpx --enable-libx264 --enable-libx265 --enable-libxvid --enable-ffplay \
 --enable-libaom \
 --enable-libtesseract \
 --enable-libsrt --enable-librtmp \
 --extra-ldflags="-L/usr/local/Cellar/lame/3.99.5/lib \
 -L/usr/local/Cellar/libogg/1.3.2/lib \
 -L/usr/local/Cellar/theora/1.1.1/lib \
 -L/usr/local/Cellar/libvorbis/1.3.5/lib \
 -L/usr/local/Cellar/xvid/1.3.4/lib \
 -L/usr/local/Cellar/aom/HEAD-285d941/lib \
 -L/usr/lib" \
 --extra-cflags="-I/usr/local/Cellar/lame/3.99.5/include \
 -I/usr/local/Cellar/libogg/1.3.2/include \
 -I/usr/local/Cellar/theora/1.1.1/include \
 -I/usr/local/Cellar/libvorbis/1.3.5/include \
 -I/usr/local/Cellar/xvid/1.3.4/include \
 -I/usr/local/Cellar/sdl/1.2.15/include \
 -I/usr/local/Cellar/aom/HEAD-285d941/include \
 -I/usr/include"


make -j5
sudo make install
