FROM ubuntu:latest

ENV LWS_VERSION=2.4.2

## Make apt noninteractive
ENV DEBIAN_FRONTEND noninteractive
ENV DEBCONF_NONINTERACTIVE_SEEN true

## Preesed tzdata, update package index, upgrade packages and install needed software
RUN truncate -s0 /tmp/preseed.cfg; \
    echo "tzdata tzdata/Areas select Europe" >> /tmp/preseed.cfg; \
    echo "tzdata tzdata/Zones/Europe select Berlin" >> /tmp/preseed.cfg; \
    debconf-set-selections /tmp/preseed.cfg && \
    rm -f /etc/timezone /etc/localtime && \
    apt-get update && \
    apt-get install -y build-essential \
        cmake \
        gnupg \
        libssl-dev \
        wget \
        curl \
        netcat \
        ca-certificates \
        vim && \
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

# Build /usr/lib/libmosquitto_jwt_auth.so
ENV PATH="/root/.cargo/bin:${PATH}"
RUN curl https://sh.rustup.rs -sSf | bash -s -- -y && \
    cd /build/mosq/jwt-auth && \
    cargo build --release && \
    mkdir -p /mosquitto/jwt-auth && \
    install -s -m644 target/release/libmosquitto_jwt_auth.so /usr/lib/libmosquitto_jwt_auth.so

RUN mkdir -p /mosquitto/www && \
    cp -r /build/mosq/graph_viewer/html/ /mosquitto/www/

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
    addgroup mosquitto && \
    useradd -ms /sbin/nologin -g mosquitto mosquitto && \
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
    rm -rf /build

HEALTHCHECK CMD /usr/bin/nc -vzw 5 localhost 1883

VOLUME ["/mosquitto/data", "/mosquitto/log"]

# Set up the entry point script and default command
EXPOSE 1883
CMD ["/usr/sbin/mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
