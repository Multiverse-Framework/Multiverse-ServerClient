# Makefile for multiverse_server

CXX = g++

# Detect OS
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++17 -Wall -I.\ -I.\..\ext\include
    LDFLAGS = -L.\..\lib\windows -l:libzmq.dll.a -l:libjsoncpp.dll.a -pthread
    TARGET = ..\bin\multiverse_server.exe
    RM = cmd /C del /Q $(TARGET)
else
    CXXFLAGS = -std=c++17 -Wall -I./ -I./../ext/include
    LDFLAGS = -L./../lib/linux -l:libzmq.a -l:libjsoncpp.a -pthread
    TARGET = ../bin/multiverse_server
    RM = rm -f $(TARGET)
endif

SRC = main.cpp multiverse_server.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	$(RM)
