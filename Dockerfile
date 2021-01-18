# Xdag C Node Dockerfile Version 1.1

FROM ubuntu:20.04
COPY . /usr/src/xdag
ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /usr/local/xdag_pool
# Update the repository
RUN apt-get update &&\
    # Install Xdag dependency lib
    apt-get install --yes g++ cmake automake autoconf libssl-dev libsecp256k1-dev libjemalloc-dev libgtest-dev libgoogle-perftools-dev git && \
    # Install Rocksdb dependency lib
    apt-get install --yes g++ libgflags-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev && \
    # Compile Rocksdb from source(Master branch)
    cd ~ && \
    git clone https://github.com/facebook/rocksdb.git && \
    cd rocksdb && mkdir build && cd build && cmake .. && make all && make install && \
    # Compile Xdag
    mkdir -p /usr/src/xdag/build && \
    mkdir -p /usr/local/xdag_pool && \
    cd /usr/src/xdag/build && \
    cmake .. && make && \
    cp /usr/src/xdag/build/xdag /usr/local/xdag_pool && \
    cp /usr/src/xdag/client/netdb-testnet.txt /usr/local/xdag_pool/ && \
    cp /usr/src/xdag/client/netdb-white-testnet.txt /usr/local/xdag_pool/ && \
    cp /usr/src/xdag/client/example.pool.config /usr/local/xdag_pool/pool.config
RUN echo '#!/bin/sh' >> /usr/local/xdag_pool/start_pool.sh && \
    echo './xdag -t -tag docker_xdag -f pool.config -disable-refresh' >> /usr/local/xdag_pool/start_pool.sh && \
    chmod +x /usr/local/xdag_pool/start_pool.sh

