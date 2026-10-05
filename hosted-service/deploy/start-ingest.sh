#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
docker run -d --name xgrib-grib-ingest --restart unless-stopped \
    --cpus 1 --memory 768m --pids-limit 32 --read-only \
    --cap-drop ALL --security-opt no-new-privileges \
    --tmpfs /tmp:rw,noexec,nosuid,size=64m \
    --log-opt max-size=5m --log-opt max-file=2 \
    --user "$(id -u):$(id -g)" \
    -v "$PWD/data:/data" xgrib-grib-service:0.2.1 watch
