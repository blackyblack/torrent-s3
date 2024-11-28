# syntax = docker/dockerfile:1.9
########################################
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=target=/var/cache/apt,id=apt,type=cache,sharing=locked \
    rm -f /etc/apt/apt.conf.d/docker-clean && \
    apt-get update && apt-get install -y --no-install-recommends sudo build-essential ca-certificates git cmake curl unzip tar zip pkg-config python3 dos2unix && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /home
RUN --mount=target=/home/vcpkg,id=vcpkg,type=cache,sharing=locked \
    if [ -d "./vcpkg" ] ; then \
    cd ./vcpkg && \
    git pull ; \
    else \
    git clone https://github.com/Microsoft/vcpkg.git && \
    cd ./vcpkg ; \
    fi && \
    ./bootstrap-vcpkg.sh && \
    ./vcpkg install minio-cpp libtorrent

WORKDIR /home/torrent-s3
COPY . .
RUN --mount=target=/home/vcpkg,id=vcpkg,type=cache,sharing=locked \
    cmake . -B ./build/Debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=/home/vcpkg/scripts/buildsystems/vcpkg.cmake && \
    cmake --build ./build/Debug && \
    cp ./build/Debug/torrent-s3 /usr/local/bin && \
    dos2unix ./docker-entrypoint.sh && \
    cp ./docker-entrypoint.sh /home && \
    chmod +x /home/docker-entrypoint.sh
CMD [ "/home/docker-entrypoint.sh" ]
