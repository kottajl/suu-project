#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/security/server_credentials.h>
#include "package_service.grpc.pb.h"
#include "vehicle_service.grpc.pb.h"


using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using packages::PackageService;
using packages::PackageData;
using packages::PackageResponse;
using packages::PackageStatusRequest;
using packages::PackageStatusResponse;
using packages::PackageUpdate;
using packages::PackageInstruction;
using packages::PackageStatus;
using packages::VehicleQuery;
using packages::DeliveredCount;

struct Package {
    int package_id;
    int delivered_by;
    std::string sender_address;
    std::string recipient_address;
    PackageStatus status;
};

class PackageServiceImpl final : public PackageService::Service {
private:
    std::vector<Package> packages_;
    int next_id_ = 1;
    std::mutex mutex_;
    std::condition_variable package_available_cv_;

public:
    Status createPackage(ServerContext* context,
                         const PackageData* request,
                         PackageResponse* response) override {
        std::lock_guard<std::mutex> lock(mutex_);

        Package pkg;
        pkg.package_id = next_id_++;
        pkg.sender_address = request->sender_address();
        pkg.recipient_address = request->recipient_address();
        pkg.status = PackageStatus::CREATED;
        pkg.delivered_by = -1;

        packages_.push_back(pkg);

        response->set_package_id(pkg.package_id);
        std::cout << "Created package ID: " << pkg.package_id << std::endl;

        package_available_cv_.notify_one();

        return Status::OK;
    }

    Status getPackageStatus(ServerContext* context,
                            const PackageStatusRequest* request,
                            PackageStatusResponse* response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pkg : packages_) {
            if (pkg.package_id == request->package_id()) {
                response->set_status(pkg.status);
                return Status::OK;
            }
        }

        return Status(grpc::NOT_FOUND, "Package not found");
    }

    Status updatePackages(ServerContext* context,
    ServerReaderWriter<PackageInstruction, PackageUpdate>* stream) override {
        PackageUpdate update;

        while (stream->Read(&update)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Handle delivery status
                if (update.package_id() != -1 && update.status() == PackageStatus::DELIVERED) {
                    for (auto& pkg : packages_) {
                        if (pkg.package_id == update.package_id()) {
                            pkg.status = PackageStatus::DELIVERED;
                            pkg.delivered_by = update.vehicle_id();
                            std::cout << "[SERVER] Package " << pkg.package_id << " delivered by vehicle "
                            << update.vehicle_id() << std::endl;
                            break;
                        }
                    }
                }
            }

            // 🔁 Wait until at least one CREATED package is available
            std::unique_lock<std::mutex> lock(mutex_);
            package_available_cv_.wait(lock, [&]() {
                return std::any_of(packages_.begin(), packages_.end(),
                                    [](const Package& p) { return p.status == PackageStatus::CREATED; });
            });

            // Pick a random CREATED package
            std::vector<Package*> created;
            for (auto& pkg : packages_) {
                if (pkg.status == PackageStatus::CREATED) {
                    created.push_back(&pkg);
                }
            }

            if (!created.empty()) {
                Package* selected = created[rand() % created.size()];
                selected->status = PackageStatus::IN_TRANSIT;

                PackageInstruction instr;
                instr.set_package_id(selected->package_id);
                instr.set_delivery_address(selected->recipient_address);

                std::cout << "[SERVER] Assigned package " << selected->package_id << " to vehicle "
                << update.vehicle_id() << std::endl;

                stream->Write(instr);
            }
        }

        return Status::OK;
    }

    Status getDeliveredCountByVehicle(ServerContext* context, const VehicleQuery* request,
                                      DeliveredCount* response) override {
        std::lock_guard<std::mutex> lock(mutex_);

        int count = 0;
        for (const auto& pkg : packages_) {
            if (pkg.status == PackageStatus::DELIVERED && pkg.delivered_by == request->vehicle_id()) {
                ++count;
            }
        }

        response->set_count(count);
        return Status::OK;
    }
};

int main(int argc, char** argv) {
    std::string server_address("0.0.0.0:50052");
    PackageServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "PackageService server listening on " << server_address << std::endl;

    server->Wait();
	
    return 0;
}
