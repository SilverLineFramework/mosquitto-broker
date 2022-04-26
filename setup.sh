if [ ! -d "./libwebsockets" ]
then
    echo "*** Downloading libwebsockets ***"
    git submodule add https://github.com/warmcat/libwebsockets.git
fi
cd libwebsockets

mkdir build
cd build

echo "*** Building libwebsockets ***"
cmake .. -DLWS_WITH_EXTERNAL_POLL=ON
make

cd ..

echo "*** Set up done ***"
