UNAME_S := $(shell uname -s)

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

# BOOST_DIR may be supplied explicitly:
#   make BOOST_DIR=/path/to/boost
BOOST_DIR ?=

# 1. If BOOST_DIR was explicitly supplied, verify it.
ifneq ($(strip $(BOOST_DIR)),)
  ifeq ($(wildcard $(BOOST_DIR)/include/boost/program_options.hpp),)
    $(error Boost header boost/program_options.hpp not found under BOOST_DIR=$(BOOST_DIR))
  endif

  ifeq ($(strip $(wildcard \
      $(BOOST_DIR)/lib/libboost_program_options* \
      $(BOOST_DIR)/lib64/libboost_program_options*)),)
    $(error libboost_program_options not found under BOOST_DIR=$(BOOST_DIR))
  endif
endif

# 2. Otherwise try CMAKE_INSTALL_PREFIX.
ifeq ($(strip $(BOOST_DIR)),)
  ifneq ($(wildcard $(CMAKE_INSTALL_PREFIX)/include/boost/program_options.hpp),)
    ifneq ($(strip $(wildcard \
        $(CMAKE_INSTALL_PREFIX)/lib/libboost_program_options* \
        $(CMAKE_INSTALL_PREFIX)/lib64/libboost_program_options*)),)
      BOOST_DIR := $(CMAKE_INSTALL_PREFIX)
    endif
  endif
endif

# 3. On macOS only, try Homebrew.
ifeq ($(strip $(BOOST_DIR)),)
  ifeq ($(UNAME_S),Darwin)
    BREW_BOOST_DIR := $(shell brew --prefix boost 2>/dev/null)

    ifneq ($(wildcard $(BREW_BOOST_DIR)/include/boost/program_options.hpp),)
      ifneq ($(strip $(wildcard \
          $(BREW_BOOST_DIR)/lib/libboost_program_options* \
          $(BREW_BOOST_DIR)/lib64/libboost_program_options*)),)
        BOOST_DIR := $(BREW_BOOST_DIR)
      endif
    endif
  endif
endif

ifeq ($(strip $(BOOST_DIR)),)
  $(error Boost program_options not found. Set BOOST_DIR=/path/to/boost; CMAKE_INSTALL_PREFIX only contains Boost when DR_EVT built Boost via FetchContent)
endif

BOOST_INC_DIR := $(BOOST_DIR)/include

ifneq ($(wildcard $(BOOST_DIR)/lib64/libboost_program_options*),)
  BOOST_LIB_DIR := $(BOOST_DIR)/lib64
else
  BOOST_LIB_DIR := $(BOOST_DIR)/lib
endif

BOOST_CFLAGS := -I$(BOOST_INC_DIR)
BOOST_LIBS := -L$(BOOST_LIB_DIR) -lboost_program_options

CXXFLAGS = -Wall -std=c++17 -m64 \
           -I../.. \
           $(BOOST_CFLAGS) \
           $(PROTO_CFLAGS) \
           -I$(CMAKE_INSTALL_PREFIX)/include

LDFLAGS = -m64 $(BOOST_LIBS) $(PROTO_LIBS)

all: dr_evt_proto_progopt_test

dr_evt_params.pb.cc: ../dr_evt_params.proto
	$(PROTOC) --proto_path=.. --cpp_out=. dr_evt_params.proto

dr_evt_params.pb.o: dr_evt_params.pb.cc
	$(CXX) $(CXXFLAGS) \
	    -c dr_evt_params.pb.cc \
	    -o dr_evt_params.pb.o

dr_evt_params.o: ../../params/dr_evt_params.cpp ../../params/dr_evt_params.hpp
	$(CXX) $(CXXFLAGS) \
	    -c ../../params/dr_evt_params.cpp \
	    -o dr_evt_params.o

dr_evt_proto_progopt_test: \
	    dr_evt_proto_progopt_test.cpp \
	    dr_evt_params.pb.o \
	    dr_evt_params.o
	$(CXX) $(CXXFLAGS) \
	    -o dr_evt_proto_progopt_test \
	    dr_evt_proto_progopt_test.cpp \
	    dr_evt_params.pb.o \
	    dr_evt_params.o \
	    $(LDFLAGS)

test: dr_evt_proto_progopt_test
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test \
	    --setup-all ../test/dr_evt_params.prototext \
	    --input-model model
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test \
	    --setup-all ../test/dr_evt_params.prototext \
	    --input-model model \
	    --setup-sim sim_setup.prototext
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test \
	    --input-model model \
	    --setup-sim sim_setup.prototext
	@echo ----------------------------------------------------

clean:
	@rm -f *.o dr_evt_proto_progopt_test
	@rm -f dr_evt_params.pb.cc dr_evt_params.pb.h
