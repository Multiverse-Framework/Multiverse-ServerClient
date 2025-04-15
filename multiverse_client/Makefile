# Makefile for multiverse_client and multiverse_client_json

CXX = g++
AR = ar
ARFLAGS = rcs

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    EXT_SO = .dll
    EXT_A = .lib
    EXT_IMP = .dll.a
    PATH_SEP = \\
	LIB_DIR = .\..\lib\$(PLATFORM)
    CXXFLAGS = -std=c++17 -Wall -I.$(PATH_SEP) -L$(LIB_DIR)
    LDFLAGS = -shared
    RM = cmd /C del /Q
else
    PLATFORM = linux
    EXT_SO = .so
    EXT_A = .a
    EXT_IMP =
    PATH_SEP = /
	LIB_DIR = ./../lib/$(PLATFORM)
    CXXFLAGS = -fPIC -std=c++17 -Wall -I.$(PATH_SEP) -L$(LIB_DIR)
    LDFLAGS = -shared
    RM = rm -f
endif

# Sources
CLIENT_SRC = multiverse_client.cpp
CLIENT_OBJ = multiverse_client.o

CLIENT_JSON_SRC = multiverse_client_json.cpp
CLIENT_JSON_OBJ = multiverse_client_json.o

# Output file paths
LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)

CLIENT_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_SO)
CLIENT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_A)
CLIENT_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP)

CLIENT_JSON_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_SO)
CLIENT_JSON_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_A)
CLIENT_JSON_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP)

# Final target
all: $(CLIENT_SHARED_LIB) $(CLIENT_STATIC_LIB) $(CLIENT_JSON_SHARED_LIB) $(CLIENT_JSON_STATIC_LIB)
	@$(RM) $(CLIENT_OBJ)
	@$(RM) $(CLIENT_JSON_OBJ)

# multiverse_client shared
$(CLIENT_SHARED_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(CXX) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_IMPORT_LIB) -o $@ $(CLIENT_OBJ) -lzmq -ljsoncpp

# multiverse_client static
$(CLIENT_STATIC_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_OBJ)

# multiverse_client_json shared — depends on multiverse_client shared
$(CLIENT_JSON_SHARED_LIB): $(CLIENT_JSON_SRC) $(CLIENT_SHARED_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_JSON_IMPORT_LIB) -o $@ $(CLIENT_JSON_OBJ)  -lzmq -ljsoncpp -lmultiverse_client

# multiverse_client_json static — depends on multiverse_client static
$(CLIENT_JSON_STATIC_LIB): $(CLIENT_JSON_SRC) $(CLIENT_STATIC_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_JSON_OBJ)

# Clean all outputs
clean:
	$(RM) $(CLIENT_SHARED_LIB)
	$(RM) $(CLIENT_STATIC_LIB)
	$(RM) $(CLIENT_JSON_SHARED_LIB)
	$(RM) $(CLIENT_JSON_STATIC_LIB)
	$(RM) $(CLIENT_IMPORT_LIB)
	$(RM) $(CLIENT_JSON_IMPORT_LIB)