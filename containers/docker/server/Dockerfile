# syntax=docker/dockerfile:1
# DR_EVT gRPC simulation server image. Build from the repository root:
# docker build -f containers/docker/server/Dockerfile -t dr-evt-server .

FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        libboost-all-dev \
        libgrpc++-dev \
        libprotobuf-dev \
        protobuf-compiler \
        protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# CMake's install rule includes every configured executable, so build the
# default target before installing rather than only dr_evt_server-bin.
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/dr-evt \
        -DDR_EVT_ENABLE_PROTOBUF=ON \
        -DDR_EVT_ENABLE_GRPC=ON \
    && cmake --build /build \
    && cmake --install /build

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        libboost-all-dev \
        libgrpc++-dev \
        libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/dr-evt /opt/dr-evt

# Session result files are written in the current directory.
WORKDIR /data
VOLUME ["/data"]

EXPOSE 50051

ENTRYPOINT ["/opt/dr-evt/bin/dr_evt_server"]
CMD ["0.0.0.0:50051"]
