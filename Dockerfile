FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        libmodbus-dev \
        libpq-dev \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B /build \
        -DBUILD_TESTING=OFF \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libmodbus5 \
        libpq5 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /data \
    && chown 10001:10001 /data

COPY --from=build /build/sun2000Tool /usr/local/bin/sun2000Tool

USER 10001:10001
WORKDIR /data
ENTRYPOINT ["/usr/local/bin/sun2000Tool"]
