# Build/QA only; never included in installation images.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc g++ make pkg-config libpam0g-dev libgtk-3-dev libjson-glib-dev libssl-dev \
    libopencv-dev opencv-data xvfb xauth libzbar-dev apt-utils gnupg curl \
    && rm -rf /var/lib/apt/lists/*
