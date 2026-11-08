FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 TZ=UTC
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake libtool build-essential cmake git ca-certificates pkg-config \
    libhwloc-dev libx11-dev libxt-dev libedit-dev libical-dev libncurses-dev \
    postgresql-server-dev-all python3-dev tcl-dev tk-dev swig libexpat1-dev \
    libssl-dev libxext-dev libxft-dev libcjson-dev flex bison \
    libboost-program-options-dev libboost-serialization-dev python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
