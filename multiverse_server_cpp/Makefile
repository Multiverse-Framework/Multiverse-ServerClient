# Makefile for multiverse_server

CXX = g++

# --------------------------------------------------------------------
# Detect OS
# --------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++17 -Wall -I./ -I./transport -I./../ext/include -I./../ext/
    LDFLAGS = -L./../lib/windows -l:libzmq.dll.a -l:libjsoncpp.dll.a -llz4 -lws2_32 -pthread
    BIN_DIR = ..\bin
    EXE_EXT = .exe
else
    CXXFLAGS = -std=c++17 -Wall -I./ -I./transport -I./../ext/include -I./../ext/
    LDFLAGS = -L./../lib/linux -l:libzmq.a -l:libjsoncpp.a -llz4 -pthread
    BIN_DIR = ../bin
    EXE_EXT =
endif

# --------------------------------------------------------------------
# Source files
# --------------------------------------------------------------------
SRC = \
    main.cpp \
    multiverse_server.cpp \
    transport_runner.cpp \
    transport/tcp_server_transport.cpp \
    transport/udp_server_transport.cpp \
    transport/zmq_server_transport.cpp

# --------------------------------------------------------------------
# Targets
# --------------------------------------------------------------------
TARGET_ZMQ = $(BIN_DIR)/multiverse_server_zmq$(EXE_EXT)
TARGET_TCP = $(BIN_DIR)/multiverse_server_tcp$(EXE_EXT)
TARGET_UDP = $(BIN_DIR)/multiverse_server_udp$(EXE_EXT)
TARGET_ALL = $(BIN_DIR)/multiverse_server_cpp$(EXE_EXT)

# --------------------------------------------------------------------
# Default rule: build all variants
# --------------------------------------------------------------------
# all: $(TARGET_ZMQ) $(TARGET_TCP) $(TARGET_UDP) $(TARGET_ALL)
all: $(TARGET_ALL)

# --------------------------------------------------------------------
# Individual builds
# --------------------------------------------------------------------
# $(TARGET_ZMQ): $(SRC)
# 	@echo "Building ZMQ-only version..."
# 	$(CXX) $(CXXFLAGS) -DUSE_ZMQ=1 -DUSE_TCP=0 -DUSE_UDP=0 -o $@ $(SRC) $(LDFLAGS)

# $(TARGET_TCP): $(SRC)
# 	@echo "Building TCP-only version..."
# 	$(CXX) $(CXXFLAGS) -DUSE_ZMQ=0 -DUSE_TCP=1 -DUSE_UDP=0 -o $@ $(SRC) $(LDFLAGS)

# $(TARGET_UDP): $(SRC)
# 	@echo "Building UDP-only version..."
# 	$(CXX) $(CXXFLAGS) -DUSE_ZMQ=0 -DUSE_TCP=0 -DUSE_UDP=1 -o $@ $(SRC) $(LDFLAGS)

$(TARGET_ALL): $(SRC)
	@echo "Building ALL (ZMQ + TCP + UDP) version..."
	$(CXX) $(CXXFLAGS) -DUSE_ZMQ=1 -DUSE_TCP=1 -DUSE_UDP=1 -o $@ $(SRC) $(LDFLAGS)

# --------------------------------------------------------------------
# Cleaning
# --------------------------------------------------------------------
clean:
	@echo "Cleaning binaries..."
	$(RM) $(TARGET_ALL)
