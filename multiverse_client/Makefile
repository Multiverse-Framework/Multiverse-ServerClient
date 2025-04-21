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
	EXT_LIB_DIR = .$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)msys2$(PATH_SEP)mingw64$(PATH_SEP)lib
	PYBIND_EXT = multiverse_client_pybind.cp312-win_amd64.pyd
	PYTHON_LIB_DIR = $(shell python -c "import sys; from pathlib import Path; print(Path(sys.executable).parent)")
    LDFLAGS = -shared
    RM = cmd /C del /Q
else
    PLATFORM = linux
    EXT_SO = .so
    EXT_A = .a
    EXT_IMP =
    PATH_SEP = /
	EXT_LIB_DIR = .$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)
	PYBIND_EXT = multiverse_client_pybind$(shell python-config --extension-suffix)
	PYTHON_LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)
    LDFLAGS = -shared
    RM = rm -f
endif

CXXFLAGS = -fPIC -std=c++17 -Wall -I.$(PATH_SEP) -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include -L$(EXT_LIB_DIR)

# Sources
CLIENT_SRC = multiverse_client.cpp
CLIENT_OBJ = multiverse_client.o

CLIENT_JSON_SRC = multiverse_client_json.cpp
CLIENT_JSON_OBJ = multiverse_client_json.o

PYBIND_SRC = multiverse_client_pybind.cpp
PYBIND_OBJ = multiverse_client_pybind.o

# Output file paths
LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)

CLIENT_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_SO)
CLIENT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_A)
CLIENT_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP)

CLIENT_JSON_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_SO)
CLIENT_JSON_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_A)
CLIENT_JSON_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP)

PYBIND_LIB = $(LIB_DIR)$(PATH_SEP)$(PYBIND_EXT)
PYBIND_CXXFLAGS = -O3 -Wall -I.$(PATH_SEP) -L$(EXT_LIB_DIR) -L$(PYTHON_LIB_DIR) -shared -std=c++17 -fPIC -fvisibility=hidden $(shell python -m pybind11 --includes)

# Final target
all: $(CLIENT_SHARED_LIB) $(CLIENT_STATIC_LIB) $(CLIENT_JSON_SHARED_LIB) $(CLIENT_JSON_STATIC_LIB) $(PYBIND_LIB)
	@$(RM) $(CLIENT_OBJ)
	@$(RM) $(CLIENT_JSON_OBJ)

# multiverse_client shared
$(CLIENT_SHARED_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(CXX) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_IMPORT_LIB) -o $@ $(CLIENT_OBJ) -lzmq

# multiverse_client static
$(CLIENT_STATIC_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_OBJ) -lzmq

# multiverse_client_json shared — depends on multiverse_client shared
$(CLIENT_JSON_SHARED_LIB): $(CLIENT_JSON_SRC) $(CLIENT_SHARED_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_JSON_IMPORT_LIB) -o $@ $(CLIENT_JSON_OBJ) $(CLIENT_OBJ) -lzmq -l:libjsoncpp.a

# multiverse_client_json static — depends on multiverse_client static
$(CLIENT_JSON_STATIC_LIB): $(CLIENT_JSON_SRC) $(CLIENT_STATIC_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_JSON_OBJ) $(CLIENT_OBJ) -l:libjsoncpp.a

# multiverse_client_pybind shared — depends on multiverse_client static
$(PYBIND_LIB): $(PYBIND_SRC) $(CLIENT_STATIC_LIB)
	$(CXX) $(PYBIND_CXXFLAGS) $< -o $@ $(CLIENT_STATIC_LIB) -lzmq -lpython312

# Clean all outputs
clean:
	$(RM) $(CLIENT_SHARED_LIB)
	$(RM) $(CLIENT_STATIC_LIB)
	$(RM) $(CLIENT_JSON_SHARED_LIB)
	$(RM) $(CLIENT_JSON_STATIC_LIB)
	$(RM) $(CLIENT_IMPORT_LIB)
	$(RM) $(CLIENT_JSON_IMPORT_LIB)
	$(RM) $(PYBIND_LIB)