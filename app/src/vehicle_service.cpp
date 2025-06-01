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

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"

#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/default_aggregation.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"

#include "opentelemetry/sdk/metrics/view/instrument_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/meter_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/view_factory.h"


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

namespace metrics_sdk      = opentelemetry::sdk::metrics;
namespace metrics_api      = opentelemetry::metrics;

namespace otlp = opentelemetry::exporter::otlp;

namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;

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

void AddLatencyView(opentelemetry::sdk::metrics::MeterProvider* provider,
                    const std::string& name, const std::string& unit) {
    auto histogram_config = std::make_shared<
    opentelemetry::sdk::metrics::HistogramAggregationConfig>();
    histogram_config->boundaries_ = {
        0,     0.00001, 0.00005, 0.0001, 0.0003, 0.0006, 0.0008, 0.001, 0.002,
        0.003, 0.004,   0.005,   0.006,  0.008,  0.01,   0.013,  0.016, 0.02,
        0.025, 0.03,    0.04,    0.05,   0.065,  0.08,   0.1,    0.13,  0.16,
        0.2,   0.25,    0.3,     0.4,    0.5,    0.65,   0.8,    1,     2,
        5,     10,      20,      50,     100};
        provider->AddView(
            opentelemetry::sdk::metrics::InstrumentSelectorFactory::Create(
                opentelemetry::sdk::metrics::InstrumentType::kHistogram, name, unit),
                opentelemetry::sdk::metrics::MeterSelectorFactory::Create(
                    "grpc-c++", grpc::Version(), ""),
                          opentelemetry::sdk::metrics::ViewFactory::Create(
                              name, "", unit,
                              opentelemetry::sdk::metrics::AggregationType::kHistogram,
                              std::move(histogram_config)));
}

void initMetrics()
{
    otlp::OtlpHttpMetricExporterOptions opts;
    opts.url = "simplest-collector:4318/v1/metrics";
    auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(opts);
    metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
    reader_options.export_interval_millis = std::chrono::milliseconds(1000);
    reader_options.export_timeout_millis  = std::chrono::milliseconds(500);
    auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), reader_options);
    auto context = metrics_sdk::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));
    auto u_provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
    AddLatencyView(u_provider.get(), "grpc.server.call.duration", "s");
    std::shared_ptr<metrics_api::MeterProvider> provider(std::move(u_provider));
    metrics_api::Provider::SetMeterProvider(provider);

}

void initLogger()
{
    otlp::OtlpHttpLogRecordExporterOptions opts;
    opts.url = "simplest-collector:4318/v1/logs";
    auto exporter  = otlp::OtlpHttpLogRecordExporterFactory::Create(opts);
    auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<logs_api::LoggerProvider> provider =
    logs_sdk::LoggerProviderFactory::Create(std::move(processor));
    logs_api::Provider::SetLoggerProvider(provider);
}

int main(int argc, char** argv) {
    std::string server_address("0.0.0.0:50052");
    std::string package_service_address("package-service:50052");
    auto package_channel = grpc::CreateChannel(package_service_address, grpc::InsecureChannelCredentials());
    initTracer();
    initMetrics();
    initLogger();

    VehicleServiceImpl service(package_channel);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "VehicleService server listening on " << server_address << std::endl;

    server->Wait();

    return 0;
}
