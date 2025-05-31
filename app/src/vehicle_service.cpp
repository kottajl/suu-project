#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/security/server_credentials.h>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include "vehicle_service.grpc.pb.h"
#include "package_service.grpc.pb.h"

#include <opentelemetry/exporters/otlp/otlp_grpc_exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/provider.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;

using vehicle::VehicleService;
using vehicle::Location;
using vehicle::Ack;
using vehicle::TrackRequest;
using vehicle::DeliveryQuery;
using vehicle::DeliveryCount;
using packages::PackageService;
using packages::VehicleQuery;

namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_api = opentelemetry::trace;
namespace otlp_exporter = opentelemetry::exporter::otlp;

struct VehicleLocation {
    double latitude;
    double longitude;
};

class VehicleServiceImpl final : public VehicleService::Service {
private:
    std::mutex mutex_;
    std::unordered_map<int32_t, std::vector<Location>> vehicle_locations_;
    std::unordered_map<int32_t, int32_t> delivered_packages_;
	std::unique_ptr<PackageService::Stub> package_stub_;
	
    struct TrackData {
    std::mutex track_mutex;
    std::condition_variable cv;
    Location latest_location;
    bool updated = false;
};
    std::unordered_map<int32_t, std::shared_ptr<TrackData>> tracking_data_;

public:
	VehicleServiceImpl(std::shared_ptr<grpc::Channel> package_channel)
        : package_stub_(packages::PackageService::NewStub(std::static_pointer_cast<grpc::ChannelInterface>(package_channel))) {}
		
    Status sendLocation(ServerContext* context,
                    ServerReader<Location>* reader,
                    Ack* response) override {
	
        Location loc;
        int32_t vehicle_id = 0;
        int location_count = 0;

        while (reader->Read(&loc)) {

            auto provider = trace_api::Provider::GetTracerProvider();
            auto tracer = provider->GetTracer("example_tracer");
            auto span = tracer->StartSpan("main_span");
            span->AddEvent("Starting processing received locations");
            span->SetAttribute("sendLocation.status", "running");

            std::shared_ptr<TrackData> track_data;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                vehicle_id = loc.vehicle_id();
                vehicle_locations_[vehicle_id].push_back(loc);

                auto it = tracking_data_.find(vehicle_id);
                if (it != tracking_data_.end()) {
                    it->second->latest_location = loc;
                    it->second->updated = true;
                    track_data = it->second;
                }
            }

            if (track_data) {
                std::lock_guard<std::mutex> lock(track_data->track_mutex);
                track_data->latest_location = loc;
                track_data->updated = true;
                track_data->cv.notify_all();
            }

            std::cout << "[VEHICLE_SERVICE] Received location for vehicle_id=" << loc.vehicle_id()
                    << " at (" << loc.latitude() << ", " << loc.longitude() << ")" << std::endl;

            ++location_count;

            span->AddEvent("Finished processing");
            span->End();
        }
        response->set_message("Received " + std::to_string(location_count) + " locations for vehicle " + std::to_string(vehicle_id));
        std::cout << response->message() << std::endl;

        return Status::OK;
    }

    Status trackVehicle(ServerContext* context,
                    const TrackRequest* request,
                    ServerWriter<Location>* writer) override {
        std::cout << "[VEHICLE_SERVICE] trackVehicle called for vehicle_id=" << request->vehicle_id() << std::endl;
        int32_t vehicle_id = request->vehicle_id();
        std::shared_ptr<TrackData> track_data;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = tracking_data_.find(vehicle_id);
            if (it == tracking_data_.end()) {
                track_data = std::make_shared<TrackData>();
                tracking_data_[vehicle_id] = track_data;
            } else {
                track_data = it->second;
            }

            // Immediately send last known location
            auto loc_it = vehicle_locations_.find(vehicle_id);
            if (loc_it != vehicle_locations_.end() && !loc_it->second.empty()) {
                const Location& last_loc = loc_it->second.back();
                writer->Write(last_loc);
                std::cout << "[VEHICLE_SERVICE] Sent location for vehicle_id=" << vehicle_id << std::endl;
            }
        }

        while (!context->IsCancelled()) {
            std::unique_lock<std::mutex> track_lock(track_data->track_mutex);
            track_data->cv.wait(track_lock, [&] {
                return track_data->updated || context->IsCancelled();
            });

            if (context->IsCancelled()) {
                break;
            }

            if (!writer->Write(track_data->latest_location)) {
                break;
            }

            std::cout << "[VEHICLE_SERVICE] Sent location for vehicle_id=" << vehicle_id << std::endl;
            track_data->updated = false;
        }

        std::cout << "Streaming for vehicle " << vehicle_id << " finished." << std::endl;
        return Status::OK;
    }


    Status getPackagesDeliveredBy(ServerContext* context,
                              const DeliveryQuery* request,
                              DeliveryCount* response) override {
		std::cout << "[VEHICLE_SERVICE] getPackagesDeliveredBy called for vehicle_id=" << request->vehicle_id() << std::endl;
        VehicleQuery query;
		query.set_vehicle_id(request->vehicle_id());

		grpc::ClientContext client_context;
		packages::DeliveredCount pkg_response;

		grpc::Status status = package_stub_->getDeliveredCountByVehicle(&client_context, query, &pkg_response);

		if (!status.ok()) {
			std::cerr << "Failed to query PackageService: " << status.error_message() << std::endl;
			return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PackageService not responding");
		}

		response->set_count(pkg_response.count());
		std::cout << "Queried delivered count from PackageService: " << pkg_response.count() << std::endl;

		return grpc::Status::OK;
	}
};

void initTracer()
{
    otlp_exporter::OtlpGrpcExporterOptions options;
    options.endpoint = "simplest-collector:4317";
    options.use_ssl_credentials = false;

    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new otlp_exporter::OtlpGrpcExporter(options));

    auto processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::SimpleSpanProcessor(std::move(exporter)));

    auto provider = std::shared_ptr<trace_api::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(processor)));

    trace_api::Provider::SetTracerProvider(provider);
}

int main(int argc, char** argv) {
    std::string server_address("0.0.0.0:50052");
    std::string package_service_address("package-service:50052");
	initTracer();
	auto package_channel = grpc::CreateChannel(package_service_address, grpc::InsecureChannelCredentials());
	
    VehicleServiceImpl service(package_channel);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "VehicleService server listening on " << server_address << std::endl;

    server->Wait();
	
    return 0;
}
