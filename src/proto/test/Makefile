PROTOC := $(shell command -v protoc 2>/dev/null)

ifeq ($(strip $(PROTOC)),)
  PROTOC := $(CMAKE_INSTALL_PREFIX)/bin/protoc
endif

ifeq ($(wildcard $(PROTOC)),)
  $(error protoc not found in PATH or $(CMAKE_INSTALL_PREFIX)/bin)
endif

PROTO_DIR := $(abspath $(dir $(PROTOC))/..)

# Make pkg-config search the same Protobuf installation as protoc.
PKG_CONFIG_PATH := $(PROTO_DIR)/lib64/pkgconfig:$(PROTO_DIR)/lib/pkgconfig:$(PKG_CONFIG_PATH)
export PKG_CONFIG_PATH

PROTO_CFLAGS := $(shell pkg-config --cflags protobuf 2>/dev/null)
PROTO_LIBS := $(shell pkg-config --libs protobuf 2>/dev/null)

ifeq ($(strip $(PROTO_LIBS)),)
  $(error pkg-config could not find protobuf under $(PROTO_DIR))
endif

CXXFLAGS = -Wall -std=c++17 -m64 \
           -I../.. \
           $(PROTO_CFLAGS) \
           -I$(CMAKE_INSTALL_PREFIX)/include

LDFLAGS = $(STDLIB) -m64 $(PROTO_LIBS)

all: dr_evt_proto_test dr_evt_params.protobin

dr_evt_params.pb.cc: ../dr_evt_params.proto
	$(PROTOC) --proto_path=.. --cpp_out=. dr_evt_params.proto

dr_evt_params.pb.o: dr_evt_params.pb.cc
	$(CXX) $(CXXFLAGS) $(STDLIB) \
	    -c dr_evt_params.pb.cc \
	    -o dr_evt_params.pb.o

dr_evt_proto_test: dr_evt_proto_test.cpp dr_evt_params.pb.o
	$(CXX) $(CXXFLAGS) \
	    -o dr_evt_proto_test \
	    dr_evt_proto_test.cpp dr_evt_params.pb.o \
	    $(LDFLAGS)

# Convert the prototext file into a binary file.
dr_evt_params.protobin: dr_evt_params.prototext ../dr_evt_params.proto
	$(PROTOC) \
	    --proto_path=.. \
	    --encode="dr_evt_proto.DR_EVT_Params" \
	    dr_evt_params.proto \
	    < dr_evt_params.prototext \
	    > dr_evt_params.protobin

# Try reading both the prototext input and its binary counterpart.
test: dr_evt_proto_test dr_evt_params.protobin
	./dr_evt_proto_test dr_evt_params.prototext > readtext.txt
	./dr_evt_proto_test dr_evt_params.protobin 1 > readbin.txt
	@egrep -v '^[[:blank:]]*#' dr_evt_params.prototext | diff readtext.txt -
	@egrep -v '^[[:blank:]]*#' dr_evt_params.prototext | diff readbin.txt -

clean:
	@rm -f *.o dr_evt_proto_test
	@rm -f dr_evt_params.protobin
	@rm -f dr_evt_params.pb.cc dr_evt_params.pb.h
	@rm -f readtext.txt readbin.txt
