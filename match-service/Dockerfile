# --- Build stage ---------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev protobuf-compiler \
    libpqxx-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY proto /workspace/proto
COPY match-service /workspace/match-service

WORKDIR /workspace/match-service
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target match_service -j"$(nproc)"

# --- Runtime stage ---------------------------------------------------------
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgrpc++-dev libprotobuf-dev libpqxx-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /workspace/match-service/build/match_service /app/match_service

EXPOSE 50052
ENTRYPOINT ["/app/match_service"]
