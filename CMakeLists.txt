cmake_minimum_required(VERSION 3.14)
project(FleetService)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(OpenTelemetry CONFIG REQUIRED)

add_executable(vehicle_server main.cpp vehicle_service.pb.cc vehicle_service.grpc.pb.cc)
target_link_libraries(vehicle_server
  gRPC::grpc++
  protobuf::libprotobuf
  OpenTelemetry::opentelemetry_trace
  OpenTelemetry::opentelemetry_exporter_otlp_grpc
)