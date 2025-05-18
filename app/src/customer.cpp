#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

#include <grpc/grpc.h>
#include "package_service.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using package::PackageService;
using package::PackageData;
using package::PackageResponse;
using package::PackageStatusRequest;
using package::PackageStatusResponse;
using package::PackageStatus;

class PackageClient {
public:
    PackageClient(std::shared_ptr<Channel> channel)
        : stub_(PackageService::NewStub(channel)) {}

    int CreatePackage(const std::string& from, const std::string& to) {
		PackageData request;
		request.set_sender_address(from);
		request.set_recipient_address(to);

		PackageResponse response;
		ClientContext context;

		Status status = stub_->createPackage(&context, request, &response);
		if (status.ok()) {
			std::cout << "[+] Created package with ID: " << response.package_id() << std::endl;
			return response.package_id();
		} else {
			std::cerr << "[!] Failed to create package: " << status.error_message() << std::endl;
			return -1;
		}
	}

    void GetStatus(int package_id) {
		PackageStatusRequest request;
		request.set_package_id(package_id);

		PackageStatusResponse response;
		ClientContext context;

		Status status = stub_->getPackageStatus(&context, request, &response);
		if (status.ok()) {
			std::string status_str = PackageStatus_Name(response.status());
			std::cout << "[=] Status of package " << package_id << ": " << status_str << std::endl;
		} else {
			std::cerr << "[!] Failed to get status of package " << package_id << ": " << status.error_message() << std::endl;
		}
	}

private:
    std::unique_ptr<PackageService::Stub> stub_;
};

int main() {
    std::string target = "localhost:50051";
    PackageClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

    std::vector<int> package_ids;

    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<int> wait_time(1000, 5000);
    std::uniform_int_distribution<int> choose_action(0, 1);
    std::uniform_int_distribution<int> choose_index(0, 0); 

    while (true) {
        int action = choose_action(rng);

        if (action == 0) {
            std::string id = client.CreatePackage("Sender Street 1", "Recipient Ave 9");
            if (!id.empty()) {
                package_ids.push_back(id);
                choose_index = std::uniform_int_distribution<int>(0, package_ids.size() - 1);
            }
        } else if (!package_ids.empty()) {
            std::string id = package_ids[choose_index(rng)];
            client.GetStatus(id);
        }

        int delay = wait_time(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    return 0;
}