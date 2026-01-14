#!/bin/sh
set -eu

echo "[qr-c] Creating PTY at ${SERIAL_PORT} and bridging to ${FAKE_SERIAL_HOST}:${FAKE_SERIAL_PORT}"
socat -d -d pty,raw,echo=0,link="${SERIAL_PORT}" "tcp:${FAKE_SERIAL_HOST}:${FAKE_SERIAL_PORT}" &

i=0
while [ ! -e "${SERIAL_PORT}" ] && [ $i -lt 50 ]; do
  i=$((i+1))
  sleep 0.1
done

ls -l "${SERIAL_PORT}" || true

mkdir -p "$(dirname "${LOG_PATH}")"
touch "${LOG_PATH}"

exec /usr/local/bin/qr-c
