FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 TZ=UTC
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake libtool build-essential cmake git ca-certificates pkg-config \
    libmunge-dev munge libpam0g-dev libssl-dev libncurses-dev libreadline-dev \
    liblua5.1-dev libnuma-dev libedit-dev liblz4-dev libsqlite3-dev \
    zlib1g-dev libjansson-dev libcurl4-openssl-dev \
    libboost-program-options-dev libboost-serialization-dev python3 python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*
COPY . /validation-slurm
WORKDIR /workspace
CMD ["bash", "/validation-slurm/build-and-test.sh"]
