FROM alpine:3.19 AS build

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    asio-dev \
    openssl-dev \
    ca-certificates

WORKDIR /work

COPY src/ /work/src/

RUN cmake -S /work/src -B /work/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /work/build -j"$(nproc)"

RUN test -f /work/build/qr_c

RUN strip /work/build/qr_c || true

FROM alpine:3.19 AS runtime

RUN apk add --no-cache \
    socat \
    libstdc++ \
    openssl \
    ca-certificates

COPY --from=build /work/build/qr_c /usr/local/bin/qr-c
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENV SERIAL_PORT=/tmp/ttyS1 \
    FAKE_SERIAL_HOST=fake-serial \
    FAKE_SERIAL_PORT=7000 \
    LOG_PATH=/var/log/qr-c/qr-c.log \
    REST_BIND=0.0.0.0 \
    REST_PORT=8080 \
    BAUDRATE=115200 \
    READ_TIMEOUT_MS=3000 \
    REOPEN_DELAY_MS=1000

EXPOSE 8080
ENTRYPOINT ["/entrypoint.sh"]
