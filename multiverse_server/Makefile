# Makefile for multiverse_server

CXX = g++

# Detect OS
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++17 -Wall -g -I./ -I./../ext/include
    LDFLAGS = -L./../lib/windows -l:libzmq.dll.a -l:libjsoncpp.dll.a -pthread -lws2_32
    LDFLAGS += -Wl,-Bstatic -static-libgcc -static-libstdc++ -lwinpthread -Wl,-Bdynamic
    TARGET = ..\bin\multiverse_server.exe
    RM = rm -f $(TARGET)
else
    CXXFLAGS = -std=c++17 -Wall -g -I./ -I./../ext/include
    LDFLAGS = -L./../lib/linux -l:libzmq.a -l:libjsoncpp.a -pthread
    TARGET = ../bin/multiverse_server
    RM = rm -f $(TARGET)
endif

# Source files
# List all .cpp files that need to be compiled.
SRCS = main.cpp \
        multiverse_server.cpp \
        transport/RawTCPServer.cpp \
        transport/RawUDPServer.cpp \
        transport/RawZMQServer.cpp

# Object files are derived from source files by replacing .cpp with .o
OBJS = $(SRCS:.cpp=.o)

# Default rule that is run when you just type "make"
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJS)
	@echo "Linking target: $@"
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Rule to compile .cpp files into .o object files
# This is a pattern rule that tells make how to build any .o file from a .cpp file.
%.o: %.cpp
	@echo "Compiling source: $<"
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Rule to clean up the build artifacts (object files and the executable)
clean:
	@echo "Cleaning up project..."
	rm -f $(OBJS) $(TARGET)

# Phony targets are not actual files. 'all' and 'clean' are common examples.
.PHONY: all clean
