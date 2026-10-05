FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ libeccodes-dev && rm -rf /var/lib/apt/lists/*
COPY grib_crop.cpp /build/grib_crop.cpp
RUN g++ -O2 -std=c++17 -Wall -Wextra /build/grib_crop.cpp -leccodes -o /build/grib-crop

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends python3 ca-certificates libeccodes0 libeccodes-tools && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/grib-crop /app/grib-crop
COPY service.py /app/service.py
WORKDIR /app
ENV PYTHONUNBUFFERED=1 XGRIB_CACHE=/data XGRIB_CROP=/app/grib-crop
USER 1000:1000
ENTRYPOINT ["python3", "/app/service.py"]
