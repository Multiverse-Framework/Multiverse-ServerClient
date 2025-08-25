# Makefile for multiverse_server

CXX = g++

# Detect OS
ifeq ($(OS),Windows_NT)
    CXXFLAGS = -std=c++17 -Wall -I./ -I./../ext/include
    LDFLAGS = -L./../lib/windows
    TARGET = ..\bin\multiverse_server.exe
    RM = rm -f $(TARGET)
else
    CXXFLAGS = -std=c++17 -Wall -I./ -I./../ext/include
    LDFLAGS = -L./../lib/linux
    TARGET = ../bin/multiverse_server
    RM = rm -f $(TARGET)
endif

SRC = main.cpp multiverse_server.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	$(RM)
