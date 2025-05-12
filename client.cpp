// vehicle_client.cpp
#include <grpcpp/grpcpp.h>
#include "vehicle_service.grpc.pb.h"

#include <iostream>
#include <thread>
#include <chrono>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReader;
using grpc::ClientWriter;
using grpc::ClientReaderWriter;
using namespace fleet;

class VehicleClient {
public:
  VehicleClient(std::shared_ptr<Channel> channel) : stub_(VehicleService::NewStub(channel)) {}

  void GetVehicleInfo(const std::string& vehicle_id) {
    VehicleRequest request;
    request.set_vehicle_id(vehicle_id);
    VehicleInfo reply;
    ClientContext context;
    Status status = stub_->GetVehicleInfo(&context, request, &reply);

    if (status.ok()) {
      std::cout << "Vehicle ID: " << reply.vehicle_id() << " Status: " << reply.status()
                << " Location: " << reply.location() << std::endl;
    } else {
      std::cerr << "GetVehicleInfo RPC failed." << std::endl;
    }
  }

  void UpdateVehicleStatus(const std::string& vehicle_id, const std::string& new_status) {
    VehicleStatusUpdate request;
    request.set_vehicle_id(vehicle_id);
    request.set_new_status(new_status);
    UpdateAck ack;
    ClientContext context;
    Status status = stub_->UpdateVehicleStatus(&context, request, &ack);

    if (status.ok() && ack.success()) {
      std::cout << "Status updated successfully." << std::endl;
    } else {
      std::cerr << "Status update failed." << std::endl;
    }
  }

  void SendTelemetry(const std::string& vehicle_id) {
    ClientContext context;
    TelemetryAck ack;
    std::unique_ptr<ClientWriter<TelemetryData>> writer(stub_->StreamVehicleTelemetry(&context, &ack));

    for (int i = 0; i < 5; ++i) {
      TelemetryData data;
      data.set_vehicle_id(vehicle_id);
      data.set_speed(60.0 + i);
      data.set_location("51.1079 N, 17.0385 E");
      data.set_fuel_level(80.0 - i);
      writer->Write(data);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    writer->WritesDone();
    writer->Finish();
    std::cout << "Telemetry sent." << std::endl;
  }

  void TrackVehicle(const std::string& vehicle_id) {
    VehicleTrackRequest request;
    request.set_vehicle_id(vehicle_id);
    ClientContext context;
    VehicleLocation location;
    std::unique_ptr<ClientReader<VehicleLocation>> reader(stub_->TrackVehicle(&context, request));

    while (reader->Read(&location)) {
      std::cout << "[Tracking] Vehicle: " << location.vehicle_id()
                << " Location: " << location.location()
                << " Timestamp: " << location.timestamp() << std::endl;
    }
    reader->Finish();
  }

  void ControlVehicle(const std::string& vehicle_id) {
    ClientContext context;
    std::shared_ptr<ClientReaderWriter<ControlRequest, ControlResponse>> stream(
      stub_->VehicleControl(&context));

    std::thread writer([stream, vehicle_id]() {
      for (int i = 0; i < 3; ++i) {
        ControlRequest request;
        request.set_vehicle_id(vehicle_id);
        request.set_command("cmd_" + std::to_string(i));
        stream->Write(request);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      stream->WritesDone();
    });

    ControlResponse response;
    while (stream->Read(&response)) {
      std::cout << "[Control] Vehicle: " << response.vehicle_id()
                << " Status: " << response.status()
                << " Msg: " << response.message() << std::endl;
    }
    writer.join();
    stream->Finish();
  }

private:
  std::unique_ptr<VehicleService::Stub> stub_;
};

int main() {
  VehicleClient client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));
  client.GetVehicleInfo("V001");
  client.UpdateVehicleStatus("V001", "maintenance");
  client.SendTelemetry("V001");
  client.TrackVehicle("V001");
  client.ControlVehicle("V001");
  return 0;
}