#include <iostream>
#include <thread>
#include <chrono>
#include <random>

#include <grpcpp/grpcpp.h>
#include <grpc/grpc.h>
#include "vehicle_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReader;

using namespace vehicle;

class ManagerClient {
public:
    ManagerClient(std::shared_ptr<grpc::ChannelInterface> channel, int num_vehicles)
    : stub_(VehicleService::NewStub(channel)), rng_(std::random_device{}()), max_vehicle_id_(num_vehicles) {}

    void Run() {
        std::uniform_int_distribution<int> vehicle_dist(1, max_vehicle_id_);
        std::uniform_int_distribution<int> sleep_dist(3000, 6000);

        while (true) {
            int vehicle_id = vehicle_dist(rng_);
            std::cout << "\n[MANAGER] Tracking vehicle " << vehicle_id << std::endl;

            TrackRequest req;
            req.set_vehicle_id(vehicle_id);

            ClientContext context;
            auto reader = stub_->trackVehicle(&context, req);

            auto start = std::chrono::steady_clock::now();
            Location loc;
            while (reader->Read(&loc)) {
                std::cout << "[TRACK] Vehicle " << vehicle_id
                          << " Location: (" << loc.latitude() << ", " << loc.longitude() << ")\n";

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
                    break;
                }
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                std::cerr << "[!] trackVehicle failed: " << status.error_message() << std::endl;
            }

            DeliveryQuery dq;
            dq.set_vehicle_id(vehicle_id);

            ClientContext ctx2;
            DeliveryCount dc;
            Status s2 = stub_->getPackagesDeliveredBy(&ctx2, dq, &dc);
            if (s2.ok()) {
                std::cout << "[DELIVERY COUNT] Vehicle " << vehicle_id
                          << " delivered " << dc.count() << " packages today\n";
            } else {
                std::cerr << "[!] getPackagesDeliveredBy failed: " << s2.error_message() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(rng_)));
        }
    }

private:
    std::unique_ptr<VehicleService::Stub> stub_;
    std::default_random_engine rng_;
    int max_vehicle_id_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <number_of_vehicles>\n";
        return 1;
    }

    int num_vehicles = std::atoi(argv[1]);
    if (num_vehicles <= 0) {
        std::cerr << "[!] Invalid number of vehicles: " << argv[1] << "\n";
        return 1;
    }

    std::string server_addr = "vehicle-service:50052";
    auto channel = grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
    ManagerClient client(channel, num_vehicles-1);
    client.Run();

    return 0;
}
