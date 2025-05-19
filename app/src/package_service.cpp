#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

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

struct Package {
    int package_id;
    std::string sender_address;
    std::string recipient_address;
    PackageStatus status;
};

class PackageServiceImpl final : public PackageService::Service {
private:
    std::vector<Package> packages_;
    int next_id_ = 1;
    std::mutex mutex_;

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

        packages_.push_back(pkg);

        response->set_package_id(pkg.package_id);
        std::cout << "Created package ID: " << pkg.package_id << std::endl;

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
            std::lock_guard<std::mutex> lock(mutex_);
            bool found = false;
            for (auto& pkg : packages_) {
                if (pkg.package_id == update.package_id()) {
                    pkg.status = update.status();
                    found = true;

                    PackageInstruction instruction;
                    instruction.set_package_id(pkg.package_id);
                    instruction.set_delivery_address(pkg.recipient_address);

                    stream->Write(instruction);
                    break;
                }
            }

            if (!found) {
                std::cerr << "Package not found for update: ID = " << update.package_id() << std::endl;
            }
        }

        return Status::OK;
    }
};

int main(int argc, char** argv) {
    std::string server_address("0.0.0.0:50051");
    PackageServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "PackageService server listening on " << server_address << std::endl;

    server->Wait();
	
    return 0;
}
