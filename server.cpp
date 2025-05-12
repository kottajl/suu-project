// vehicle_server.cpp
#include <grpcpp/grpcpp.h>
#include "vehicle_service.grpc.pb.h"

#include <opentelemetry/exporters/otlp/otlp_grpc_exporter.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/scope.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <unordered_map>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerReaderWriter;
using grpc::Status;
using namespace fleet;

namespace trace_api = opentelemetry::trace;
namespace sdktrace = opentelemetry::sdk::trace;

std::unordered_map<std::string, std::string> vehicle_status = {
  {"V001", "active"},
  {"V002", "inactive"}
};

std::unordered_map<std::string, std::string> vehicle_location = {
  {"V001", "51.1079 N, 17.0385 E"},
  {"V002", "50.2649 N, 19.0238 E"}
};

void InitTracer() {
  auto exporter = std::make_unique<opentelemetry::exporter::otlp::OtlpGrpcExporter>();
  auto processor = std::make_unique<sdktrace::SimpleSpanProcessor>(std::move(exporter));
  auto provider = std::make_shared<sdktrace::TracerProvider>(std::move(processor));
  trace_api::Provider::SetTracerProvider(provider);
}

class VehicleServiceImpl final : public VehicleService::Service {
public:
  Status GetVehicleInfo(ServerContext* context, const VehicleRequest* request, VehicleInfo* reply) override {
    auto tracer = trace_api::Provider::GetTracerProvider()->GetTracer("fleet.tracer");
    auto span = tracer->StartSpan("GetVehicleInfo");
    opentelemetry::trace::Scope scope(span);

    std::string id = request->vehicle_id();
    reply->set_vehicle_id(id);
    reply->set_status(vehicle_status[id]);
    reply->set_location(vehicle_location[id]);

    span->End();
    return Status::OK;
  }

  Status UpdateVehicleStatus(ServerContext* context, const VehicleStatusUpdate* request, UpdateAck* response) override {
    auto span = trace_api::Provider::GetTracerProvider()->GetTracer("fleet.tracer")->StartSpan("UpdateVehicleStatus");
    opentelemetry::trace::Scope scope(span);

    vehicle_status[request->vehicle_id()] = request->new_status();
    response->set_success(true);

    span->End();
    return Status::OK;
  }

  Status StreamVehicleTelemetry(ServerContext* context, ServerReader<TelemetryData>* reader, TelemetryAck* response) override {
    TelemetryData data;
    while (reader->Read(&data)) {
      std::cout << "[Telemetry] " << data.vehicle_id() << " speed: " << data.speed()
                << " location: " << data.location() << " fuel: " << data.fuel_level() << std::endl;
    }
    response->set_received(true);
    return Status::OK;
  }

  Status TrackVehicle(ServerContext* context, const VehicleTrackRequest* request, ServerWriter<VehicleLocation>* writer) override {
    for (int i = 0; i < 5; ++i) {
      VehicleLocation loc;
      loc.set_vehicle_id(request->vehicle_id());
      loc.set_location(vehicle_location[request->vehicle_id()]);
      loc.set_timestamp(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
      writer->Write(loc);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return Status::OK;
  }

  Status VehicleControl(ServerContext* context, ServerReaderWriter<ControlResponse, ControlRequest>* stream) override {
    ControlRequest request;
    while (stream->Read(&request)) {
      ControlResponse response;
      response.set_vehicle_id(request.vehicle_id());
      response.set_status("received");
      response.set_message("Command: " + request.command());
      stream->Write(response);
    }
    return Status::OK;
  }
};

void RunServer() {
  std::string address("0.0.0.0:50051");
  VehicleServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server running on " << address << std::endl;
  server->Wait();
}

int main() {
  InitTracer();
  RunServer();
  return 0;
}
