# Makefile for multiverse_server

CXX = g++
CXXFLAGS = -Wall -I./ -L./../lib/linux
LDFLAGS = -lzmq -ljsoncpp -pthread
TARGET = ../bin/multiverse_server
SRC = main.cpp multiverse_server.cpp

all: $(TARGET)

$(TARGET):
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
