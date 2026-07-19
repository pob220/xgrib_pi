#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
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
  libblosc-dev \
  libbz2-dev \
  libcurl4-openssl-dev \
  libeccodes-dev \
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

rm -rf /var/lib/apt/lists/*
