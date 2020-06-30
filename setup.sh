#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]
then
    echo "*** Installing dependencies using Homebrew ***"
    brew update
    brew install cmake openssl

    OPENSSL_DIR=$(find /usr/local/Cellar/openssl -name '1.*')
    echo "OpenSSL installed in: $OPENSSL_DIR"

    if [ ! -d "./libwebsockets" ]
    then
        echo "*** Downloading libwebsockets ***"
        git submodule add --force https://github.com/warmcat/libwebsockets.git
    fi
    cd libwebsockets
    mkdir build
    cd build

    echo "*** Building libwebsockets ***"
    cmake .. -DOPENSSL_ROOT_DIR="$OPENSSL_DIR"
    make
    cd ..

    echo "*** Set up done ***"

else
    echo "*** No set up needed! ***"
fi
