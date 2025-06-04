#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <cstdlib>

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

#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h"
#include <opentelemetry/sdk/resource/resource.h>

#include "opentelemetry/metrics/sync_instruments.h"

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
namespace resource  = opentelemetry::sdk::resource;

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

    opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer_;
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> meter_;
    opentelemetry::nostd::shared_ptr<logs_api::Logger> logger_;

    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>> send_location_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>> track_vehicle_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>> get_packages_delivered_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Counter<double>> locations_processed_counter;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Histogram<double>> package_service_latency_histogram;

public:
	VehicleServiceImpl(std::shared_ptr<grpc::Channel> package_channel)
    : package_stub_(packages::PackageService::NewStub(std::static_pointer_cast<grpc::ChannelInterface>(package_channel))) {

        tracer_ = trace_api::Provider::GetTracerProvider()->GetTracer("vehicle_service");
        meter_ = metrics_api::Provider::GetMeterProvider()->GetMeter("vehicle_service");
        logger_ = logs_api::Provider::GetLoggerProvider()->GetLogger("vehicle_service");

        send_location_counter = meter_->CreateDoubleCounter("send_location_requests_total");
        track_vehicle_counter = meter_->CreateDoubleCounter("track_vehicle_requests_total");
        get_packages_delivered_counter = meter_->CreateDoubleCounter("get_packages_delivered_requests_total");

        locations_processed_counter = meter_->CreateDoubleCounter("locations_processed_total");
        package_service_latency_histogram = meter_->CreateDoubleHistogram(
            "package_service_latency_ms",
            "Latency for calls to PackageService",
            "ms"
        );

    }

    Status sendLocation(ServerContext* context,
                    ServerReader<Location>* reader,
                    Ack* response) override {

        Location loc;
        int32_t vehicle_id = 0;
        int location_count = 0;

        std::map<std::string, std::string> labels = {{"vehicle_id", std::to_string(vehicle_id)}};
        auto labelkv = opentelemetry::common::KeyValueIterableView<decltype(labels)>{labels};
        send_location_counter->Add(1.0, labelkv);

        while (reader->Read(&loc)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + rand() % 151));

            auto span = tracer_->StartSpan("process_location");
            span->AddEvent("Starting processing received location");
            span->SetAttribute("vehicle_id", loc.vehicle_id());
            span->SetAttribute("latitude", loc.latitude());
            span->SetAttribute("longitude", loc.longitude());
            auto ctx = span->GetContext();

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
            logger_->EmitLogRecord(opentelemetry::logs::Severity::kDebug, "Received location for vehicle_id=" + std::to_string(loc.vehicle_id()) + " at (" + std::to_string(loc.latitude()) + ", " + std::to_string(loc.longitude()) + ")",
                                   ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));

            ++location_count;

            std::map<std::string, std::string> locations_labels = {{"vehicle_id", std::to_string(loc.vehicle_id())}};
            auto labelkv_locations = opentelemetry::common::KeyValueIterableView<decltype(locations_labels)>{locations_labels};
            locations_processed_counter->Add(1.0, labelkv_locations);

            span->AddEvent("Finished processing");
            span->End();
        }

        response->set_message("Received " + std::to_string(location_count) + " locations for vehicle " + std::to_string(vehicle_id));
        std::cout << response->message() << std::endl;
        logger_->EmitLogRecord(opentelemetry::logs::Severity::kInfo, response->message());
        
        return Status::OK;
    }

    Status trackVehicle(ServerContext* context,
                    const TrackRequest* request,
                    ServerWriter<Location>* writer) override {

        auto span = tracer_->StartSpan("track_vehicle");
        span->AddEvent("Starting vehicle tracking");
        span->SetAttribute("vehicle_id", request->vehicle_id());
        auto ctx = span->GetContext();

        std::map<std::string, std::string> labels = {{"vehicle_id", std::to_string(request->vehicle_id())}};
        auto labelkv = opentelemetry::common::KeyValueIterableView<decltype(labels)>{labels};
        track_vehicle_counter->Add(1.0, labelkv);

        logger_->EmitLogRecord(opentelemetry::logs::Severity::kInfo, "trackVehicle called for vehicle_id=" + std::to_string(request->vehicle_id()),
                               ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));

        std::cout << "[VEHICLE_SERVICE] trackVehicle called for vehicle_id=" << request->vehicle_id() << std::endl;

        std::default_random_engine rng(std::random_device{}());
        std::uniform_real_distribution<double> lat_dist(50.0, 52.0);
        std::uniform_real_distribution<double> lon_dist(18.0, 20.0);

        int32_t vehicle_id = request->vehicle_id();
        int amount = rand() % 10;
        for (int i = 0; i < amount; i++) {

            Location loc;
            loc.set_vehicle_id(vehicle_id);
            loc.set_latitude(lat_dist(rng));
            loc.set_longitude(lon_dist(rng));

            std::this_thread::sleep_for(std::chrono::milliseconds(80 + rand() % 171));

            writer->Write(loc);

            span->AddEvent("Sending location " + std::to_string(loc.latitude()) + ", " + std::to_string(loc.longitude()));
            logger_->EmitLogRecord(opentelemetry::logs::Severity::kDebug, "Sent location for vehicle_id=" + std::to_string(vehicle_id),
                                   ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));
            std::cout << "[VEHICLE_SERVICE] Sent location for vehicle_id=" << vehicle_id << std::endl;
        }

        std::cout << "Streaming for vehicle " << vehicle_id << " finished." << std::endl;
        logger_->EmitLogRecord(opentelemetry::logs::Severity::kInfo, "Streaming for vehicle " + std::to_string(vehicle_id) + " finished",
                               ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));

        span->AddEvent("Finished tracking");
        span->End();
        return Status::OK;
    }


    Status getPackagesDeliveredBy(ServerContext* context,
                              const DeliveryQuery* request,
                              DeliveryCount* response) override {

        auto span = tracer_->StartSpan("get_packages_delivered_by");
        span->AddEvent("Calling package_service to get packages count");
        span->SetAttribute("vehicle_id", request->vehicle_id());
        auto ctx = span->GetContext();

        std::this_thread::sleep_for(std::chrono::milliseconds(100 + rand() % 201));

        std::cout << "[VEHICLE_SERVICE] getPackagesDeliveredBy called for vehicle_id=" << request->vehicle_id() << std::endl;
        logger_->EmitLogRecord(opentelemetry::logs::Severity::kInfo, "getPackagesDeliveredBy called for vehicle_id=" + std::to_string(request->vehicle_id()),
                               ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));
    
        VehicleQuery query;
		query.set_vehicle_id(request->vehicle_id());

		grpc::ClientContext client_context;
		packages::DeliveredCount pkg_response;

        auto start_ext_clock = std::chrono::steady_clock::now();

		grpc::Status status = package_stub_->getDeliveredCountByVehicle(&client_context, query, &pkg_response);
        
        auto end_ext_clock = std::chrono::steady_clock::now();
        std::chrono::duration<double> ext_elapsed_time = end_ext_clock - start_ext_clock;

		if (!status.ok()) {
            std::cerr << "Failed to query PackageService: " << status.error_message() << std::endl;
            logger_->EmitLogRecord(opentelemetry::logs::Severity::kError, "Failed to query PackageService: " + status.error_message(),
                                   ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));
            span->AddEvent("Error occured, PackageService not responding");
            span->End();
			return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PackageService not responding");
        }
        span->AddEvent("package_service called succesfully");

        std::map<std::string, std::string> labels = {{"vehicle_id", std::to_string(request->vehicle_id())}};
        auto labelkv = opentelemetry::common::KeyValueIterableView<decltype(labels)>{labels};
        get_packages_delivered_counter->Add(1.0, labelkv);

        package_service_latency_histogram->Record(ext_elapsed_time.count(), opentelemetry::context::Context{});

        response->set_count(pkg_response.count());
        span->SetAttribute("package_count", pkg_response.count());
        std::cout << "Queried delivered count from PackageService: " << pkg_response.count() << std::endl;
        logger_->EmitLogRecord(opentelemetry::logs::Severity::kError, "Queried delivered count from PackageService: " + std::to_string(pkg_response.count()),
                               ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));

        span->AddEvent("Responded with the delivered count");
        span->End();
		return grpc::Status::OK;
	}
};

void initTracer()
{
    otlp_exporter::OtlpGrpcExporterOptions options;
    options.endpoint = "simplest-collector:4317";
    options.use_ssl_credentials = false;

    auto resource_attributes = resource::Resource::Create({
        {"service.name", "vehicle-service"},
        {"service.version", "1.0.0"},
        {"deployment.environment", "dev"}
    });

    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new otlp_exporter::OtlpGrpcExporter(options));

    auto processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::SimpleSpanProcessor(std::move(exporter)));

    auto provider = std::shared_ptr<trace_api::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(processor), resource_attributes));

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
    otlp::OtlpGrpcMetricExporterOptions opts;
    opts.endpoint = "simplest-collector:4317";
    opts.use_ssl_credentials = false;

    auto exporter = otlp::OtlpGrpcMetricExporterFactory::Create(opts);

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
    auto resource_attributes = resource::Resource::Create({
        {"service.name", "vehicle-service"},
        {"service.version", "1.0.0"},
        {"deployment.environment", "dev"}
    });
    otlp::OtlpGrpcLogRecordExporterOptions opts;
    opts.endpoint = "simplest-collector:4317";
    opts.use_ssl_credentials = false;
    auto exporter = otlp::OtlpGrpcLogRecordExporterFactory::Create(opts);
    auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<logs_api::LoggerProvider> provider =
    logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource_attributes);
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
