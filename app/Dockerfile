FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    unzip \
    pkg-config \
    libgrpc++-dev \
    grpc-proto \
    protobuf-compiler-grpc \
    protobuf-compiler \
    libprotobuf-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

RUN make all

ENTRYPOINT ["/app/vehicle_service"]