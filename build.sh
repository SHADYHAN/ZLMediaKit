#!bin/bash
#
#
rm -rf build release
mkdir build
cd build

cmake .. -DENABLE_WEBRTC=true -DENABLE_FFMPEG=true -DENABLE_TRANSCODE=ON -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_LIBRARIES=/usr/lib/x86_64-linux-gnu

cmake --build . --target MediaServer -- -j8
