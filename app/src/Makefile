PROTOC=protoc
GRPC_CPP_PLUGIN1=grpc_cpp_plugin
GRPC_CPP_PLUGIN ?= `which $(GRPC_CPP_PLUGIN1)`
PKG_CONFIG_PATH := $(HOME)/.local/lib/pkgconfig
export PKG_CONFIG_PATH

CPPFLAGS += -I$(HOME)/.local/include -I/usr/local/include/opentelemetry `pkg-config --cflags protobuf grpc grpc++ absl_flags absl_flags_parse`
LDFLAGS += -L/usr/local/lib \
		-lopentelemetry_exporter_otlp_grpc_metrics -lopentelemetry_exporter_otlp_http_metric -lopentelemetry_exporter_otlp_http_log -lopentelemetry_exporter_otlp_http_client -lopentelemetry_http_client_curl -lopentelemetry_exporter_otlp_http -lopentelemetry_exporter_otlp_grpc -lopentelemetry_exporter_otlp_grpc_client -lopentelemetry_exporter_otlp_grpc_log -lopentelemetry_otlp_recordable -lopentelemetry_logs -lopentelemetry_proto -lopentelemetry_proto_grpc -lopentelemetry_resources -lopentelemetry_metrics -lopentelemetry_trace -lopentelemetry_version -lopentelemetry_common -lcurl \
	   `pkg-config --libs --static protobuf grpc grpc++ absl_flags absl_flags_parse absl_log_initialize $(PROTOBUF_ABSL_DEPS)`\
           $(PROTOBUF_UTF8_RANGE_LINK_LIBS) \
           -pthread \



UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lsystemd -ldl
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -framework CoreFoundation
	LDFLAGS += -Wl,-rpath,$(shell brew --prefix zlib)/lib
endif

PROTO_SRCS=vehicle_service.proto package_service.proto
PROTO_GEN_SRCS=vehicle_service.pb.cc vehicle_service.grpc.pb.cc package_service.pb.cc package_service.grpc.pb.cc
PROTO_GEN_HDRS=vehicle_service.pb.h vehicle_service.grpc.pb.h package_service.pb.h package_service.grpc.pb.h

SOURCES=package_service.cpp vehicle_service.cpp customer.cpp manager.cpp vehicle.cpp
OBJECTS=$(SOURCES:.cpp=.o)
BINARIES=package_service vehicle_service customer manager vehicle

CXX=g++
CXXFLAGS += -std=c++17 -Wall -O2

all: $(PROTO_GEN_SRCS) $(BINARIES)

%.pb.cc %.pb.h: %.proto
	$(PROTOC) --cpp_out=. $<

%.grpc.pb.cc %.grpc.pb.h: %.proto
	$(PROTOC) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

vehicle_service.pb.cc vehicle_service.grpc.pb.cc: vehicle_service.proto
package_service.pb.cc package_service.grpc.pb.cc: package_service.proto

package_service: package_service.o vehicle_service.pb.o vehicle_service.grpc.pb.o package_service.pb.o package_service.grpc.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@

vehicle_service: vehicle_service.o vehicle_service.pb.o vehicle_service.grpc.pb.o package_service.pb.o package_service.grpc.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@

customer: customer.o vehicle_service.pb.o vehicle_service.grpc.pb.o package_service.pb.o package_service.grpc.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@

manager: manager.o vehicle_service.pb.o vehicle_service.grpc.pb.o package_service.pb.o package_service.grpc.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@

vehicle: vehicle.o vehicle_service.pb.o vehicle_service.grpc.pb.o package_service.pb.o package_service.grpc.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@

%.o: %.cpp $(PROTO_GEN_HDRS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

all_clean: all
	rm -f *.o $(PROTO_GEN_SRCS) $(PROTO_GEN_HDRS) *pb.cc *pb.h

clean:
	rm -f *.o $(BINARIES) $(PROTO_GEN_SRCS) $(PROTO_GEN_HDRS) *pb.cc *pb.h

.PHONY: all clean all_clean
