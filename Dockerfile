FROM alpine:3.12

ENV LWS_VERSION=2.4.2

RUN set -x && \
    apk --no-cache add --virtual build-deps \
        build-base \
        cmake \
        gnupg \
        openssl-dev \
        util-linux-dev && \
    wget https://github.com/warmcat/libwebsockets/archive/v${LWS_VERSION}.tar.gz -O /tmp/lws.tar.gz && \
    mkdir -p /build/lws && \
    tar --strip=1 -xf /tmp/lws.tar.gz -C /build/lws && \
    rm /tmp/lws.tar.gz && \
    cd /build/lws && \
    cmake . \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DLWS_IPV6=ON \
        -DLWS_WITHOUT_BUILTIN_GETIFADDRS=ON \
        -DLWS_WITHOUT_CLIENT=ON \
        -DLWS_WITHOUT_EXTENSIONS=ON \
        -DLWS_WITHOUT_TESTAPPS=ON \
        -DLWS_WITH_SHARED=OFF \
        -DLWS_WITH_ZIP_FOPS=OFF \
        -DLWS_WITH_ZLIB=OFF && \
    make -j "$(nproc)" && \
    rm -rf /root/.cmake && \
    mkdir -p /build/mosq
    
WORKDIR /build/mosq

COPY . .

RUN mkdir -p /mosquitto/www && \
    cp -r /build/mosq/www_graph/html/ /mosquitto/www/ 

RUN rm -rf build || true && \
    mkdir build && \
    cd build && \
    cmake .. -DWITH_WEBSOCKETS=ON -DDOCUMENTATION=OFF -DCMAKE_C_FLAGS="-I/usr/local/include -I/build/lws/include" -DCMAKE_EXE_LINKER_FLAGS="-L/usr/local/lib -L/build/lws/lib" && \
    make && \
    cd .. && \
    rm -rf bin && \
    mkdir bin && \
    mv build/client/mosquitto* bin/ && \
    mv build/src/mosquitto* bin/ && \
    addgroup -S -g 1883 mosquitto 2>/dev/null && \
    adduser -S -u 1883 -D -H -h /var/empty -s /sbin/nologin -G mosquitto -g mosquitto mosquitto 2>/dev/null && \
    mkdir -p /mosquitto/config /mosquitto/data /mosquitto/log && \
    install -d /usr/sbin/ && \
    install -s -m755 /build/mosq/bin/mosquitto_pub /usr/bin/mosquitto_pub && \
    install -s -m755 /build/mosq/bin//mosquitto_rr /usr/bin/mosquitto_rr && \
    install -s -m755 /build/mosq/bin/mosquitto_sub /usr/bin/mosquitto_sub && \
    install -s -m644 /build/mosq/build/lib/libmosquitto.so.1 /usr/lib/libmosquitto.so.1 && \
    install -s -m755 /build/mosq/bin/mosquitto /usr/sbin/mosquitto && \
    install -s -m755 /build/mosq/bin/mosquitto_passwd /usr/bin/mosquitto_passwd && \
    install -m644 /build/mosq/conf/mosquitto.conf /mosquitto/config/mosquitto.conf && \
    chown -R mosquitto:mosquitto /mosquitto && \
    apk --no-cache add \
        ca-certificates && \
    apk del build-deps && \
    rm -rf /build

VOLUME ["/mosquitto/data", "/mosquitto/log"]

# Set up the entry point script and default command
COPY docker/1.6-openssl/docker-entrypoint.sh /
EXPOSE 1883
ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["/usr/sbin/mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
