# --- Build stage ---------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev protobuf-compiler \
    libpqxx-dev libpq-dev libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY proto /workspace/proto
COPY player-service /workspace/player-service

WORKDIR /workspace/player-service
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target player_service -j"$(nproc)"

# --- Runtime stage ---------------------------------------------------------
# Reuse -dev packages for runtime: avoids guessing versioned runtime
# package names across Ubuntu releases, at the cost of a larger image.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgrpc++-dev libprotobuf-dev libpqxx-dev libpq-dev libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /workspace/player-service/build/player_service /app/player_service

EXPOSE 50051
ENTRYPOINT ["/app/player_service"]
