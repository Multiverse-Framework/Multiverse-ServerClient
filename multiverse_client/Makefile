# Makefile for multiverse_client

CXX = g++
CXXFLAGS = -fPIC -std=c++17 -Wall -I./ -L./../lib/linux
AR = ar
ARFLAGS = rcs

SRC = multiverse_client.cpp
OBJ = multiverse_client.o

SHARED_LIB = ../lib/linux/libmultiverse_client.so
STATIC_LIB = ../lib/linux/libmultiverse_client.a

LDLIBS = -lzmq -ljsoncpp
LDFLAGS = -shared

all: $(SHARED_LIB) $(STATIC_LIB)
	@rm -f $(OBJ)

# Compile and link shared library
$(SHARED_LIB): $(SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# Compile and archive static library
$(STATIC_LIB): $(SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(OBJ)
	$(AR) $(ARFLAGS) $@ $(OBJ)

# Clean everything
clean:
	rm -f $(OBJ) $(SHARED_LIB) $(STATIC_LIB)
