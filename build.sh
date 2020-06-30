#!/bin/sh

if [ "$(uname)" == "Darwin" ]
then
    OPENSSL_DIR=$(find /usr/local/Cellar/openssl -name '1.*')
fi

if [ -d "./build" ]
then
    rm -rf build
fi
mkdir build
cd build

echo "*** Building mosquitto ***"
echo "OpenSSL from: $OPENSSL_DIR"
cmake .. -DOPENSSL_ROOT_DIR="$OPENSSL_DIR" -DWITH_WEBSOCKETS=ON -DDOCUMENTATION=OFF -DCMAKE_C_FLAGS="-I$PWD/../libwebsockets/build/include" -DCMAKE_EXE_LINKER_FLAGS="-L$PWD/../libwebsockets/build/lib -lwebsockets"
make
cd ..

if [ -d "./bin" ]
then
    rm -rf bin
fi
mkdir bin
mv build/client/mosquitto* bin/
mv build/src/mosquitto* bin/

echo "*** Build done ***"
