# Makefile for multiverse_server

CXX = g++

# Detect OS
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++17 -Wall -I.\ -L.\..\lib\windows
    TARGET = ..\bin\multiverse_server.exe
    RM = cmd /C del /Q $(TARGET)
else
    CXXFLAGS = -std=c++14 -Wall -I./
    TARGET = ../bin/multiverse_server
    RM = rm -f $(TARGET)
endif

LDFLAGS = -lzmq -ljsoncpp -pthread
SRC = main.cpp multiverse_server.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	$(RM)
