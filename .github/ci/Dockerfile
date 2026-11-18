FROM ubuntu:24.04@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8
COPY linux-packages.txt /tmp/linux-packages.txt
RUN apt-get update && DEBIAN_FRONTEND=noninteractive xargs -a /tmp/linux-packages.txt apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*
