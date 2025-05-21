PROTOC=protoc
GRPC_CPP_PLUGIN1=grpc_cpp_plugin
GRPC_CPP_PLUGIN ?= `which $(GRPC_CPP_PLUGIN1)`
PKG_CONFIG_PATH := $(HOME)/.local/lib/pkgconfig
export PKG_CONFIG_PATH

CPPFLAGS += -I$(HOME)/.local/include `pkg-config --cflags protobuf grpc absl_flags absl_flags_parse`
LDFLAGS += -L/usr/local/lib `pkg-config --libs --static protobuf grpc++ absl_flags absl_flags_parse absl_log_initialize $(PROTOBUF_ABSL_DEPS)`\
           $(PROTOBUF_UTF8_RANGE_LINK_LIBS) \
           -pthread\
           -lsystemd\
           -ldl

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
