#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
eccodes_package=libeccodes-dev
if [ "$(dpkg --print-architecture)" = armhf ] &&
   ! apt-cache show libeccodes-dev 2>/dev/null | grep -q '^Version: '; then
  eccodes_package=
fi
apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  gettext \
  git \
  jq \
  lsb-release \
  pkg-config \
  libaec-dev \
  libopenjp2-7-dev \
  libpng-dev \
  libblosc-dev \
  libbz2-dev \
  libcurl4-openssl-dev \
  $eccodes_package \
  libgl1-mesa-dev \
  libglew-dev \
  libglu1-mesa-dev \
  libgtk-3-dev \
  libjsoncpp-dev \
  liblz4-dev \
  libnetcdf-dev \
  libproj-dev \
  libqhull-dev \
  libsodium-dev \
  libwxgtk3.2-dev \
  libzip-dev \
  libzstd-dev \
  zlib1g-dev

if [ -z "$eccodes_package" ]; then
  sh /usr/local/bin/build-armhf-eccodes.sh
fi
rm -rf /var/lib/apt/lists/*
