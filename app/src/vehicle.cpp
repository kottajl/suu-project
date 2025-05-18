#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstdlib>

#include <grpc/grpc.h>
#include "vehicle_service.grpc.pb.h"
#include "package_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientWriter;
using grpc::ClientReaderWriter;

using namespace vehicle;
using namespace package;

class VehicleClient {
public:
    VehicleClient(std::shared_ptr<Channel> vehicle_channel,
                  std::shared_ptr<Channel> package_channel)
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

    std::vector<PackageInstruction> active_packages_;
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

		std::mutex mtx;
		std::condition_variable cv;
		bool has_package = false;
		PackageInstruction current_package;

		std::thread reader([&]() {
			PackageInstruction instr;
			while (stream->Read(&instr)) {
				{
					std::lock_guard<std::mutex> lock(mtx);
					current_package = instr;
					has_package = true;
					std::cout << "[INSTRUCTION] Received package " << instr.package_id()
							  << " to " << instr.delivery_address() << " for " << instr.recipient() << std::endl;
				}
				cv.notify_one();
			}
		});

		std::thread writer([&]() {
			while (true) {
				std::unique_lock<std::mutex> lock(mtx);
				cv.wait(lock, [&]() { return has_package; });

				PackageUpdate update;
				update.set_vehicle_id(vehicle_id);
				update.set_package_id(current_package.package_id());
				update.set_status(PackageStatus::DELIVERED);

				std::cout << "[PKG] Delivering package " << current_package.package_id() << std::endl;

				if (!stream->Write(update)) {
					std::cerr << "[!] Failed to send package delivery update." << std::endl;
					break;
				}

				has_package = false;
			}
		});

		reader.join();
		stream->WritesDone();
		writer.join();

		Status status = stream->Finish();
		if (!status.ok()) {
			std::cerr << "[!] Package update stream failed: " << status.error_message() << std::endl;
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
    int vehicle_id = std::stoi(pod_name.substr(pos + 1)) + 1; 

    std::cout << "[INFO] Vehicle client started with vehicle_id = " << vehicle_id << "\n";

    std::string vehicle_addr = "localhost:50052";
    std::string package_addr = "localhost:50051";

    VehicleClient client(
        grpc::CreateChannel(vehicle_addr, grpc::InsecureChannelCredentials()),
        grpc::CreateChannel(package_addr, grpc::InsecureChannelCredentials())
    );

    client.Start(vehicle_id);

    return 0;
}