mkdir build
cd build

cmake .. -DWITH_WEBSOCKETS=ON -DWITH_CJSON=OFF -DDOCUMENTATION=OFF -DCMAKE_C_FLAGS="-I$PWD/../libwebsockets/build/include" -DCMAKE_EXE_LINKER_FLAGS="-L$PWD/../libwebsockets/build/lib"
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

