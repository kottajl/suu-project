#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <opentelemetry/exporters/otlp/otlp_grpc_exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/sdk/resource/resource.h>

#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>

#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/metrics/provider.h>

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

namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_api = opentelemetry::trace;
namespace otlp_exporter = opentelemetry::exporter::otlp;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace metrics_api = opentelemetry::metrics;
namespace resource = opentelemetry::sdk::resource;
namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;

void initTelemetry() {
    // Resource attributes
    auto resource_attributes = resource::Resource::Create({
        {"service.name", "package-service"},
        {"service.version", "1.0.0"},
        {"deployment.environment", "dev"}
    });

    // Tracing
    otlp_exporter::OtlpGrpcExporterOptions trace_opts;
    trace_opts.endpoint = "simplest-collector:4317";
    trace_opts.use_ssl_credentials = false;
    auto trace_exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new otlp_exporter::OtlpGrpcExporter(trace_opts));
    auto trace_processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::SimpleSpanProcessor(std::move(trace_exporter)));
    auto trace_provider = std::shared_ptr<trace_api::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(trace_processor), resource_attributes));
    trace_api::Provider::SetTracerProvider(trace_provider);

    // Logging
    otlp_exporter::OtlpGrpcLogRecordExporterOptions log_opts;
    log_opts.endpoint = "simplest-collector:4317";
    log_opts.use_ssl_credentials = false;
    auto log_exporter = otlp_exporter::OtlpGrpcLogRecordExporterFactory::Create(log_opts);
    auto log_processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(log_exporter));
    std::shared_ptr<logs_api::LoggerProvider> log_provider =
        logs_sdk::LoggerProviderFactory::Create(std::move(log_processor), resource_attributes);
    logs_api::Provider::SetLoggerProvider(log_provider);

    // Metrics
    otlp_exporter::OtlpGrpcMetricExporterOptions metric_opts;
    metric_opts.endpoint = "simplest-collector:4317";
    metric_opts.use_ssl_credentials = false;
    auto metric_exporter = otlp_exporter::OtlpGrpcMetricExporterFactory::Create(metric_opts);

    metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
    reader_options.export_interval_millis = std::chrono::milliseconds(1000);
    reader_options.export_timeout_millis  = std::chrono::milliseconds(500);

    auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(metric_exporter), reader_options);
    auto context = metrics_sdk::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto u_provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
    std::shared_ptr<metrics_api::MeterProvider> provider(std::move(u_provider));
    metrics_api::Provider::SetMeterProvider(provider);
}

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
    opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer_;
    opentelemetry::nostd::shared_ptr<logs_api::Logger> logger_;
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> meter_;
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>> created_packages_counter_;
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>> get_package_status_counter_;
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>> update_packages_requests_counter_;
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<double>> delivered_packages_counter_;
    opentelemetry::nostd::shared_ptr<metrics_api::Histogram<double>> create_package_duration_histogram_;
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>> not_found_package_status_counter_;

public:
    PackageServiceImpl() {
        tracer_ = trace_api::Provider::GetTracerProvider()->GetTracer("package-service");
        logger_ = logs_api::Provider::GetLoggerProvider()->GetLogger("package-service");
        meter_ = metrics_api::Provider::GetMeterProvider()->GetMeter("package-service");
        created_packages_counter_ = meter_->CreateUInt64Counter("created_packages_total");
        get_package_status_counter_ = meter_->CreateUInt64Counter("get_package_status_requests_total");
        update_packages_requests_counter_ = meter_->CreateUInt64Counter("update_packages_requests_total");
        delivered_packages_counter_ = meter_->CreateDoubleCounter("delivered_packages_total");
        create_package_duration_histogram_ = meter_->CreateDoubleHistogram("create_package_duration_seconds");
        not_found_package_status_counter_ = meter_->CreateUInt64Counter("not_found_package_status_total");
    }
    Status createPackage(ServerContext* context,
                     const PackageData* request,
                     PackageResponse* response) override {

    auto start = std::chrono::steady_clock::now();
    auto span = tracer_->StartSpan("create_package");
    span->SetAttribute("sender", request->sender_address());
    span->SetAttribute("recipient", request->recipient_address());
    auto ctx = span->GetContext();

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

    created_packages_counter_->Add(1);

    logger_->EmitLogRecord(
        logs_api::Severity::kInfo,
        "New package created: sender=" + pkg.sender_address + ", recipient=" + pkg.recipient_address,
        ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),
        opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now())
    );

    span->AddEvent("Package created with ID " + std::to_string(pkg.package_id));
    span->AddEvent("Sender address: " + pkg.sender_address);
    span->AddEvent("Recipient address: " + pkg.recipient_address);
    span->End();

    package_available_cv_.notify_one();
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    create_package_duration_histogram_->Record(elapsed.count(), opentelemetry::context::Context{});

    return Status::OK;
}


    Status getPackageStatus(ServerContext* context,
                            const PackageStatusRequest* request,
                            PackageStatusResponse* response) override {
        get_package_status_counter_->Add(1);
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pkg : packages_) {
            if (pkg.package_id == request->package_id()) {
                response->set_status(pkg.status);
                return Status::OK;
            }
        }
        not_found_package_status_counter_->Add(1);

        auto span = tracer_->StartSpan("get_package_status_not_found");
        auto ctx = span->GetContext();
        logger_->EmitLogRecord(
            logs_api::Severity::kWarn,
            "Package not found: id=" + std::to_string(request->package_id()),
            ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),
            opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now())
        );
        span->End();

        return Status(grpc::NOT_FOUND, "Package not found");
    }

    Status updatePackages(ServerContext* context,
    ServerReaderWriter<PackageInstruction, PackageUpdate>* stream) override {
        update_packages_requests_counter_->Add(1);
        PackageUpdate update;
        auto span = tracer_->StartSpan("update_packages");
        auto ctx = span->GetContext();
        logger_->EmitLogRecord(logs_api::Severity::kInfo, "updatePackages called",
                       ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),
                       opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));
        while (stream->Read(&update)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Handle delivery status
                if (update.package_id() != -1 && update.status() == PackageStatus::DELIVERED) {
                    for (auto& pkg : packages_) {
                        if (pkg.package_id == update.package_id()) {
                            pkg.status = PackageStatus::DELIVERED;
                            pkg.delivered_by = update.vehicle_id();

                            std::map<std::string, std::string> labels = {{"vehicle_id", std::to_string(update.vehicle_id())}};
                            auto labelkv = opentelemetry::common::KeyValueIterableView<decltype(labels)>{labels};
                            delivered_packages_counter_->Add(1.0, labelkv);

                            std::cout << "[SERVER] Package " << pkg.package_id << " delivered by vehicle "
                            << update.vehicle_id() << std::endl;

                            span->AddEvent("Package " + std::to_string(pkg.package_id) + " delivered by vehicle " + std::to_string(update.vehicle_id()));

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

                span->AddEvent("Assigned package " + std::to_string(selected->package_id) + " to vehicle " + std::to_string(update.vehicle_id()));

                stream->Write(instr);
            } else {
                logger_->EmitLogRecord(
                    logs_api::Severity::kInfo,
                    "No packages available for assignment to vehicle " + std::to_string(update.vehicle_id()),
                    ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),
                    opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now())
                );
            }
        }
        span->End();
        return Status::OK;
    }

    Status getDeliveredCountByVehicle(ServerContext* context, const VehicleQuery* request,
                                      DeliveredCount* response) override {
        std::lock_guard<std::mutex> lock(mutex_);

        auto span = tracer_->StartSpan("get_delivered_count_by_vehicle");
        span->SetAttribute("vehicle_id", request->vehicle_id());
        auto ctx = span->GetContext();
        logger_->EmitLogRecord(logs_api::Severity::kInfo, "getDeliveredCountByVehicle called",
                            ctx.trace_id(), ctx.span_id(), ctx.trace_flags(),
                            opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));

        int count = 0;
        for (const auto& pkg : packages_) {
            if (pkg.status == PackageStatus::DELIVERED && pkg.delivered_by == request->vehicle_id()) {
                ++count;
            }
        }

        response->set_count(count);
        span->End();
        return Status::OK;
    }
};

int main(int argc, char** argv) {
    initTelemetry();
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
