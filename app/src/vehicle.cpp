#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstdlib>

#include <condition_variable>
#include <grpcpp/grpcpp.h>
#include <grpc/grpc.h>
#include "vehicle_service.grpc.pb.h"
#include "package_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientWriter;
using grpc::ClientReaderWriter;
using grpc::ChannelInterface;

using namespace vehicle;
using namespace packages;

class VehicleClient {
public:
    VehicleClient(std::shared_ptr<ChannelInterface> vehicle_channel,
                  std::shared_ptr<ChannelInterface> package_channel)
    : vehicle_stub_(VehicleService::NewStub(vehicle_channel)),
    package_stub_(PackageService::NewStub(package_channel)) {}

    void Start(int vehicle_id) {
        std::thread loc_thread(&VehicleClient::SendLocations, this, vehicle_id);
        std::thread pkg_thread(&VehicleClient::UpdatePackages, this, vehicle_id);

        loc_thread.join();
        pkg_thread.join();
    }

private:
    std::unique_ptr<VehicleService::Stub> vehicle_stub_;
    std::unique_ptr<PackageService::Stub> package_stub_;

    std::mutex packages_mutex_;

    void SendLocations(int vehicle_id) {
        ClientContext context;
        Ack ack;
        auto writer = vehicle_stub_->sendLocation(&context, &ack);

        std::default_random_engine rng(std::random_device{}());
        std::uniform_real_distribution<double> lat_dist(50.0, 52.0);
        std::uniform_real_distribution<double> lon_dist(18.0, 20.0);

        while (true) {
            Location loc;
            loc.set_vehicle_id(vehicle_id);
            loc.set_latitude(lat_dist(rng));
            loc.set_longitude(lon_dist(rng));

            std::cout << "[GPS] Sending location: " << loc.latitude() << ", " << loc.longitude() << std::endl;

            if (!writer->Write(loc)) {
                std::cerr << "[!] Failed to write location to stream." << std::endl;
                break;
            }
            std::cout << "[CLIENT] Wrote location for vehicle_id=" << vehicle_id << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        writer->WritesDone();
        Status status = writer->Finish();
        if (!status.ok()) {
            std::cerr << "[!] Location stream failed: " << status.error_message() << std::endl;
        }
    }

    void UpdatePackages(int vehicle_id) {
        ClientContext context;
        auto stream = package_stub_->updatePackages(&context);

        // Initial dummy update to ask for first package
        PackageUpdate dummy;
        dummy.set_vehicle_id(vehicle_id);
        dummy.set_status(PackageStatus::DELIVERED); // Pretend we just delivered one
        dummy.set_package_id(-1); // Dummy ID
        stream->Write(dummy);

        PackageInstruction instr;

        std::default_random_engine rng(std::random_device{}());
        std::uniform_int_distribution<int> delay_dist(5, 10);

        while (stream->Read(&instr)) {
            int pkg_id = instr.package_id();
            std::string address = instr.delivery_address();

            std::cout << "[CLIENT] Received package " << pkg_id << " to deliver at: " << address << std::endl;

            int delay = delay_dist(rng);
            std::cout << "[CLIENT] Delivering in " << delay << " seconds...\n";
            std::this_thread::sleep_for(std::chrono::seconds(delay));

            PackageUpdate update;
            update.set_vehicle_id(vehicle_id);
            update.set_package_id(pkg_id);
            update.set_status(PackageStatus::DELIVERED);

            if (!stream->Write(update)) {
                std::cerr << "[!] Failed to send update for package " << pkg_id << std::endl;
                break;
            }

            std::cout << "[CLIENT] Delivered package " << pkg_id << ", waiting for next...\n";
        }

        stream->WritesDone();
        Status status = stream->Finish();
        if (!status.ok()) {
            std::cerr << "[!] Stream finished with error: " << status.error_message() << std::endl;
        }
    }
};

int main(int argc, char** argv) {
    const char* pod_name_env = std::getenv("VEHICLE_ID");
    if (!pod_name_env) {
        std::cerr << "[!] VEHICLE_ID env var not set\n";
        return 1;
    }
    std::string pod_name(pod_name_env);

    size_t pos = pod_name.find_last_of('-');
    if (pos == std::string::npos || pos + 1 >= pod_name.size()) {
        std::cerr << "[!] Invalid pod name format: " << pod_name << "\n";
        return 1;
    }
    int vehicle_id = std::stoi(pod_name.substr(pos + 1));

    std::cout << "[INFO] Vehicle client started with vehicle_id = " << vehicle_id << "\n";

    std::string vehicle_addr = "vehicle-service:50052";
    std::string package_addr = "package-service:50052";

    VehicleClient client(
        grpc::CreateChannel(vehicle_addr, grpc::InsecureChannelCredentials()),
        grpc::CreateChannel(package_addr, grpc::InsecureChannelCredentials())
    );

    client.Start(vehicle_id);

    return 0;
}
