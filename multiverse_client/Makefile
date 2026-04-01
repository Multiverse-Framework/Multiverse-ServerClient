.PHONY: default all clean

default: all

CXX     = g++
AR      = ar
ARFLAGS = rcs

# --- Platform detection ------------------------------------------------------

ifeq ($(OS),Windows_NT)
    PLATFORM        = windows
    EXT_SO          = .dll
    EXT_A           = .lib
    EXT_IMP         = .dll.a
    EXT_IMP_STATIC  = .dll.a
    PATH_SEP        = /
    EXE_EXT         = .exe

    RM       = rm -f
    SYS_LIBS = -lws2_32 -pthread
else
    PLATFORM        = linux
    EXT_SO          = .so
    EXT_A           = .a
    EXT_IMP         =
    EXT_IMP_STATIC  = .a
    PATH_SEP        = /
    EXE_EXT         =

    RM       = rm -f
    SYS_LIBS = -pthread
endif

# --- Paths / common flags ----------------------------------------------------

EXT_LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)

CXXFLAGS = -fPIC -std=c++17 -Wall \
           -I.$(PATH_SEP) \
           -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include \
           -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)

LDFLAGS = -shared -L$(EXT_LIB_DIR)

# Transport client sources
TRANSPORT_CLIENT_SRC = \
    transport/tcp_client_transport.cpp \
    transport/udp_client_transport.cpp \
    transport/zmq_client_transport.cpp

# Sources
CLIENT_SRC      = multiverse_client.cpp $(TRANSPORT_CLIENT_SRC)
CLIENT_OBJ      = multiverse_client.o
CLIENT_JSON_SRC = multiverse_client_json.cpp $(TRANSPORT_CLIENT_SRC)
CLIENT_JSON_OBJ = multiverse_client_json.o

# Test compile flags
TEST_CXXFLAGS = -std=c++17 -O2 -Wall -Wextra \
                -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include \
                -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)

# Test simple
TEST_CLIENT_SRC = test_multiverse_client_simple.cpp
# Test RGB
TEST_RGB_CLIENT_SRC = test_multiverse_client.cpp

# Output file paths
LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)
BIN_DIR = ..$(PATH_SEP)bin$(PATH_SEP)

CLIENT_SHARED_LIB             = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_SO)
CLIENT_STATIC_LIB             = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_A)
CLIENT_IMPORT_LIB             = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP)
CLIENT_IMPORT_STATIC_LIB      = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP_STATIC)

CLIENT_JSON_SHARED_LIB        = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_SO)
CLIENT_JSON_STATIC_LIB        = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_A)
CLIENT_JSON_IMPORT_LIB        = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP)
CLIENT_JSON_IMPORT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP_STATIC)

# --- transport variants ------------------------------------------------------

VARIANTS = zmq tcp udp all

USE_FLAGS_zmq = -DUSE_ZMQ=1 -DUSE_TCP=0 -DUSE_UDP=0
USE_FLAGS_tcp = -DUSE_ZMQ=0 -DUSE_TCP=1 -DUSE_UDP=0
USE_FLAGS_udp = -DUSE_ZMQ=0 -DUSE_TCP=0 -DUSE_UDP=1
USE_FLAGS_all = -DUSE_ZMQ=1 -DUSE_TCP=1 -DUSE_UDP=1

# --- per-variant client libraries -------------------------------------------

# Shared lib per variant: libmultiverse_client_<variant>.(dll|so)
$(LIB_DIR)/libmultiverse_client_%$(EXT_SO): $(CLIENT_SRC)
	@echo "Building multiverse_client ($*) shared..."
	# Compile main client for this variant
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c multiverse_client.cpp -o multiverse_client_$*.o
	# Compile all transport sources for this variant
	for src in $(TRANSPORT_CLIENT_SRC); do \
	    base=$${src%.cpp}; \
	    $(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $$src -o $${base}_$*.o; \
	done
	# Link variant DLL/SO
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) $(LDFLAGS) \
		-Wl,--out-implib,$(LIB_DIR)/libmultiverse_client_$*$(EXT_IMP) \
		-o $@ \
		multiverse_client_$*.o \
		$(foreach s,$(TRANSPORT_CLIENT_SRC),$(basename $(s))_$*.o) \
		-l:libzmq$(EXT_IMP_STATIC) \
		$(SYS_LIBS)

# Static lib per variant: libmultiverse_client_<variant>.(lib|a)
$(LIB_DIR)/libmultiverse_client_%$(EXT_A): $(CLIENT_SRC)
	@echo "Building multiverse_client ($*) static..."
	# Compile main client for this variant
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c multiverse_client.cpp -o multiverse_client_$*.o
	# Compile all transport sources for this variant
	for src in $(TRANSPORT_CLIENT_SRC); do \
	    base=$${src%.cpp}; \
	    $(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $$src -o $${base}_$*.o; \
	done
	# Archive variant static library
	$(AR) $(ARFLAGS) $@ \
		multiverse_client_$*.o \
		$(foreach s,$(TRANSPORT_CLIENT_SRC),$(basename $(s))_$*.o) \
		$(EXT_LIB_DIR)$(PATH_SEP)libzmq$(EXT_IMP_STATIC)

# --- per-variant JSON client libraries --------------------------------------

$(LIB_DIR)/libmultiverse_client_json_%$(EXT_SO): $(CLIENT_JSON_SRC) $(LIB_DIR)/libmultiverse_client_%$(EXT_A)
	@echo "Building multiverse_client_json ($*) shared..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c multiverse_client_json.cpp -o multiverse_client_json_$*.o
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) $(LDFLAGS) \
		-Wl,--out-implib,$(LIB_DIR)/libmultiverse_client_json_$*$(EXT_IMP) \
		-o $@ \
		multiverse_client_json_$*.o \
		$(LIB_DIR)/libmultiverse_client_$*$(EXT_A) \
		-l:libzmq$(EXT_IMP_STATIC) \
		-l:libjsoncpp$(EXT_IMP_STATIC) \
		$(SYS_LIBS)

$(LIB_DIR)/libmultiverse_client_json_%$(EXT_A): $(CLIENT_JSON_SRC) $(LIB_DIR)/libmultiverse_client_%$(EXT_A)
	@echo "Building multiverse_client_json ($*) static..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c multiverse_client_json.cpp -o multiverse_client_json_$*.o
	$(AR) $(ARFLAGS) $@ \
		multiverse_client_json_$*.o \
		$(LIB_DIR)/libmultiverse_client_$*$(EXT_A)

# --- final combined variant target list -------------------------------------

VARIANT_TARGETS = $(foreach v,$(VARIANTS), \
	$(LIB_DIR)/libmultiverse_client_$(v)$(EXT_SO) \
	$(LIB_DIR)/libmultiverse_client_$(v)$(EXT_A) \
	$(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_SO) \
	$(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_A))

# --- test executables per variant -------------------------------------------

TEST_VARIANTS          = zmq tcp udp all
TEST_APP_VARIANTS      = $(foreach v,$(TEST_VARIANTS),$(BIN_DIR)test_multiverse_client_$(v)$(EXE_EXT))
TEST_APP_RGB_VARIANTS  = $(foreach v,$(TEST_VARIANTS),$(BIN_DIR)test_multiverse_client_rgb_$(v)$(EXE_EXT))

TEST_USE_FLAGS_zmq = -DUSE_ZMQ=1 -DUSE_TCP=0 -DUSE_UDP=0
TEST_USE_FLAGS_tcp = -DUSE_ZMQ=0 -DUSE_TCP=1 -DUSE_UDP=0
TEST_USE_FLAGS_udp = -DUSE_ZMQ=0 -DUSE_TCP=0 -DUSE_UDP=1
TEST_USE_FLAGS_all = -DUSE_ZMQ=1 -DUSE_TCP=1 -DUSE_UDP=1

$(BIN_DIR)test_multiverse_client_%$(EXE_EXT): $(TEST_CLIENT_SRC) $(CLIENT_SRC) $(CLIENT_JSON_SRC)
	@echo "Building test_multiverse_client_$* ..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_USE_FLAGS_$*) $^ \
		-L$(EXT_LIB_DIR) \
		-l:libzmq$(EXT_IMP_STATIC) \
		-l:libjsoncpp$(EXT_IMP_STATIC) \
		$(SYS_LIBS) -o $@

$(BIN_DIR)test_multiverse_client_rgb_%$(EXE_EXT): $(TEST_RGB_CLIENT_SRC) $(CLIENT_SRC) $(CLIENT_JSON_SRC)
	@echo "Building test_multiverse_client_rgb_$* ..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_USE_FLAGS_$*) $^ \
		-L$(EXT_LIB_DIR) \
		-l:libzmq$(EXT_IMP_STATIC) \
		-l:libjsoncpp$(EXT_IMP_STATIC) \
		$(SYS_LIBS) -o $@

# --- original "default" client targets (all transports enabled) --------------

$(CLIENT_SHARED_LIB): $(CLIENT_SRC)
	@echo "Building multiverse_client (default, all transports) shared..."
	# Compile main client with all transports enabled
	$(CXX) $(CXXFLAGS) -c multiverse_client.cpp -o $(CLIENT_OBJ)
	# Compile all transport sources
	for src in $(TRANSPORT_CLIENT_SRC); do \
	    base=$${src%.cpp}; \
	    $(CXX) $(CXXFLAGS) $(USE_FLAGS_all) -c $$src -o $${base}.o; \
	done
	# Link default shared library
	$(CXX) $(CXXFLAGS) $(LDFLAGS) \
		-Wl,--out-implib,$(CLIENT_IMPORT_LIB) \
		-o $@ \
		$(CLIENT_OBJ) \
		$(SYS_LIBS)

$(CLIENT_STATIC_LIB): $(CLIENT_SRC)
	@echo "Building multiverse_client (default, all transports) static..."
	# Compile main client with all transports enabled
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_all) -c multiverse_client.cpp -o $(CLIENT_OBJ)
	# Compile all transport sources
	for src in $(TRANSPORT_CLIENT_SRC); do \
	    base=$${src%.cpp}; \
	    $(CXX) $(CXXFLAGS) $(USE_FLAGS_all) -c $$src -o $${base}.o; \
	done
	# Archive default static library
	$(AR) $(ARFLAGS) $@ \
		$(CLIENT_OBJ)

$(CLIENT_JSON_SHARED_LIB): $(CLIENT_JSON_SRC) $(CLIENT_SHARED_LIB)
	@echo "Building multiverse_client_json (default) shared..."
	$(CXX) $(CXXFLAGS) -c multiverse_client_json.cpp -o $(CLIENT_JSON_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) \
		-Wl,--out-implib,$(CLIENT_JSON_IMPORT_LIB) \
		-o $@ \
		$(CLIENT_JSON_OBJ) \
		$(CLIENT_IMPORT_STATIC_LIB) \
		-l:libjsoncpp$(EXT_IMP_STATIC) \
		$(SYS_LIBS)

$(CLIENT_JSON_STATIC_LIB): $(CLIENT_JSON_SRC) $(CLIENT_STATIC_LIB)
	@echo "Building multiverse_client_json (default) static..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_all) -c multiverse_client_json.cpp -o $(CLIENT_JSON_OBJ)
	$(AR) $(ARFLAGS) $@ \
		$(CLIENT_JSON_OBJ) \
		$(CLIENT_STATIC_LIB)

# --- final top-level target --------------------------------------------------

all: $(CLIENT_SHARED_LIB) $(CLIENT_STATIC_LIB) \
     $(CLIENT_JSON_SHARED_LIB) $(CLIENT_JSON_STATIC_LIB) \
     $(VARIANT_TARGETS)
	 $(RM) *.o
	 $(RM) transport/*.o
	@echo "✅ All variants (ZMQ, TCP, UDP, ALL) and default builds complete."

test: $(TEST_APP_VARIANTS) $(TEST_APP_RGB_VARIANTS)
	@echo "✅ All test executables built."

# --- clean -------------------------------------------------------------------

clean:
	@echo "🧹 Cleaning all build products..."

	# Remove all object files
	$(RM) *.o
	$(RM) transport/*.o

	# Remove default client libraries
	$(RM) $(CLIENT_SHARED_LIB)
	$(RM) $(CLIENT_STATIC_LIB)
	$(RM) $(CLIENT_IMPORT_LIB)
	$(RM) $(CLIENT_IMPORT_STATIC_LIB)

	# Remove default JSON client libraries
	$(RM) $(CLIENT_JSON_SHARED_LIB)
	$(RM) $(CLIENT_JSON_STATIC_LIB)
	$(RM) $(CLIENT_JSON_IMPORT_LIB)
	$(RM) $(CLIENT_JSON_IMPORT_STATIC_LIB)

	# Remove variant client libraries (zmq/tcp/udp/all)
	$(foreach v,$(VARIANTS), \
		$(RM) $(LIB_DIR)/libmultiverse_client_$(v)$(EXT_SO); \
		$(RM) $(LIB_DIR)/libmultiverse_client_$(v)$(EXT_A); \
		$(RM) $(LIB_DIR)/libmultiverse_client_$(v)$(EXT_IMP); \
		$(RM) $(LIB_DIR)/libmultiverse_client_$(v)$(EXT_IMP_STATIC);)

	# Remove variant JSON client libraries
	$(foreach v,$(VARIANTS), \
		$(RM) $(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_SO); \
		$(RM) $(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_A); \
		$(RM) $(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_IMP); \
		$(RM) $(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_IMP_STATIC);)

	# Remove pybind extension module
	$(RM) $(LIB_DIR)/multiverse_client_pybind.*

	# Remove test executables
	$(foreach v,$(TEST_VARIANTS), \
		$(RM) $(BIN_DIR)test_multiverse_client_$(v)$(EXE_EXT); )
	$(foreach v,$(TEST_VARIANTS), \
		$(RM) $(BIN_DIR)test_multiverse_client_rgb_$(v)$(EXE_EXT); )

	@echo "✅ Clean complete."
