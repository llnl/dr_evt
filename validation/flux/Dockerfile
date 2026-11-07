FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake libtool build-essential cmake meson ninja-build \
    pkg-config git ca-certificates jq patch file \
    libzmq3-dev uuid-dev libjansson-dev liblz4-dev libarchive-dev \
    libhwloc-dev libsqlite3-dev libncurses-dev libssl-dev \
    libboost-graph-dev libedit-dev libyaml-cpp-dev \
    lua5.1 liblua5.1-dev lua-posix libfaketime \
    python3 python3-dev python3-pip python3-venv python3-cffi python3-ply \
    python3-setuptools python3-yaml python3-wheel python3-jsonschema \
    python3-pydantic python3-tqdm python3-pytest python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m pip install --break-system-packages 'meson-python>=0.16,<0.19'

COPY . /validation
ENV PATH=/workspace/install/bin:$PATH
ENV LD_LIBRARY_PATH=/workspace/install/lib:/workspace/install/lib64
ENV PKG_CONFIG_PATH=/workspace/install/lib/pkgconfig:/workspace/install/lib64/pkgconfig
ENV FLUX_PREFIX=/workspace/install
WORKDIR /workspace
CMD ["bash", "/validation/build-and-test.sh"]
