FROM alpine:3.19 AS build
RUN apk add --no-cache build-base
WORKDIR /src
COPY fake_serial.c .
RUN gcc -O2 -Wall -Wextra -o fake-serial fake_serial.c

FROM alpine:3.19
RUN apk add --no-cache ca-certificates
COPY --from=build /src/fake-serial /usr/local/bin/fake-serial
ENV FAKE_SERIAL_PORT=7000 \
    PARCEL_PAYLOAD="QR:123456" \
    PARCEL_INTERVAL_MS=2000
EXPOSE 7000
ENTRYPOINT ["/usr/local/bin/fake-serial"]
