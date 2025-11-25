# Makefile for multiverse_client and multiverse_client_json

CXX = g++
AR = ar
ARFLAGS = rcs

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    EXT_SO = .dll
    EXT_A = .lib
    EXT_IMP = .dll.a
    EXT_IMP_STATIC = .dll.a
    PATH_SEP = /
    PYBIND_INCLUDE_DIR = $(shell py.exe -m pybind11 --includes)
    PYTHON_VER = $(shell py.exe -c "import sys; print(f'{sys.version_info.major}{sys.version_info.minor}')")
    PYBIND_EXT = multiverse_client_pybind$(shell py.exe -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
    PYTHON_LIBDIR = $(shell py.exe -c "import sysconfig; print(sysconfig.get_config_var('BINDIR'))")
    PYTHON_LIB = $(PYTHON_LIBDIR)$(PATH_SEP)python$(PYTHON_VER).dll
    RM = rm -f
    SYS_LIBS = -lws2_32 -pthread
else
    PLATFORM = linux
    EXT_SO = .so
    EXT_A = .a
    EXT_IMP =
    EXT_IMP_STATIC = .a
    PATH_SEP = /
    PYBIND_INCLUDE_DIR = $(shell python3 -m pybind11 --includes)
    PYTHON_VER = $(shell python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
    PYBIND_EXT = multiverse_client_pybind$(shell python3-config --extension-suffix)
    PYTHON_LIBDIR = $(shell python3 -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))")
    PYTHON_MULTIARCH = $(shell python3 -c "import sysconfig; print(sysconfig.get_config_var('MULTIARCH'))")
    PYTHON_LIB_WITH_MULTIARCH = $(shell echo $(PYTHON_LIBDIR) | grep -q $(PYTHON_MULTIARCH) && echo $(PYTHON_LIBDIR) || echo $(PYTHON_LIBDIR)/$(PYTHON_MULTIARCH))
    PYTHON_LIB = $(PYTHON_LIB_WITH_MULTIARCH)$(PATH_SEP)libpython$(PYTHON_VER).so
    RM = rm -f
    SYS_LIBS = -pthread
endif

EXT_LIB_DIR = .$(PATH_SEP)..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)
CXXFLAGS = -fPIC -std=c++17 -Wall -I.$(PATH_SEP) -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)
LDFLAGS = -shared -L$(EXT_LIB_DIR)

# Transport client sources
TRANSPORT_CLIENT_SRC = \
    transport/client.cpp \
    transport/tcp_client_transport.cpp \
    transport/udp_client_transport.cpp \
    transport/zmq_client_transport.cpp

# Sources
CLIENT_SRC = multiverse_client.cpp $(TRANSPORT_CLIENT_SRC)
CLIENT_OBJ = multiverse_client.o
CLIENT_JSON_SRC = multiverse_client_json.cpp $(TRANSPORT_CLIENT_SRC)
CLIENT_JSON_OBJ = multiverse_client_json.o
PYBIND_SRC = multiverse_client_pybind.cpp
PYBIND_OBJ = multiverse_client_pybind.o

# Test
TEST_CLIENT_SRC = test_multiverse_client_simple.cpp 
TEST_CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)

# Output file paths
LIB_DIR = ..$(PATH_SEP)lib$(PATH_SEP)$(PLATFORM)
CLIENT_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_SO)
CLIENT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_A)
CLIENT_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP)
CLIENT_IMPORT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client$(EXT_IMP_STATIC)
CLIENT_JSON_SHARED_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_SO)
CLIENT_JSON_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_A)
CLIENT_JSON_IMPORT_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP)
CLIENT_JSON_IMPORT_STATIC_LIB = $(LIB_DIR)$(PATH_SEP)libmultiverse_client_json$(EXT_IMP_STATIC)
PYBIND_LIB = $(LIB_DIR)$(PATH_SEP)$(PYBIND_EXT)
PYBIND_CXXFLAGS = -O3 -Wall -I.$(PATH_SEP) $(PYBIND_INCLUDE_DIR) -I.$(PATH_SEP)..$(PATH_SEP)ext$(PATH_SEP)include -std=c++17 -fPIC -fvisibility=hidden

# --- transport variants ---
VARIANTS = zmq tcp udp all
USE_FLAGS_zmq = -DUSE_ZMQ=1 -DUSE_TCP=0 -DUSE_UDP=0
USE_FLAGS_tcp = -DUSE_ZMQ=0 -DUSE_TCP=1 -DUSE_UDP=0
USE_FLAGS_udp = -DUSE_ZMQ=0 -DUSE_TCP=0 -DUSE_UDP=1
USE_FLAGS_all = -DUSE_ZMQ=1 -DUSE_TCP=1 -DUSE_UDP=1

# --- pattern rules for each variant ---
$(LIB_DIR)/libmultiverse_client_%$(EXT_SO): $(CLIENT_SRC)
	@echo "Building multiverse_client ($*) shared..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $< -o multiverse_client_$*.o
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) $(LDFLAGS) -Wl,--out-implib,$(LIB_DIR)/libmultiverse_client_$*$(EXT_IMP) \
		-o $@ multiverse_client_$*.o -l:libzmq$(EXT_IMP_STATIC) $(SYS_LIBS)

$(LIB_DIR)/libmultiverse_client_%$(EXT_A): $(CLIENT_SRC)
	@echo "Building multiverse_client ($*) static..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $< -o multiverse_client_$*.o
	$(AR) $(ARFLAGS) $@ multiverse_client_$*.o $(EXT_LIB_DIR)$(PATH_SEP)libzmq$(EXT_IMP_STATIC)

$(LIB_DIR)/libmultiverse_client_json_%$(EXT_SO): $(CLIENT_JSON_SRC) $(LIB_DIR)/libmultiverse_client_%$(EXT_SO)
	@echo "Building multiverse_client_json ($*) shared..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $< -o multiverse_client_json_$*.o
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) $(LDFLAGS) -Wl,--out-implib,$(LIB_DIR)/libmultiverse_client_json_$*$(EXT_IMP) \
		-o $@ multiverse_client_json_$*.o $(LIB_DIR)/libmultiverse_client_$*$(EXT_IMP_STATIC) -l:libjsoncpp$(EXT_IMP_STATIC) $(SYS_LIBS)

$(LIB_DIR)/libmultiverse_client_json_%$(EXT_A): $(CLIENT_JSON_SRC) $(LIB_DIR)/libmultiverse_client_%$(EXT_A)
	@echo "Building multiverse_client_json ($*) static..."
	$(CXX) $(CXXFLAGS) $(USE_FLAGS_$*) -c $< -o multiverse_client_json_$*.o
	$(AR) $(ARFLAGS) $@ multiverse_client_json_$*.o $(LIB_DIR)/libmultiverse_client_$*$(EXT_A)

# --- final combined variant targets ---
VARIANT_TARGETS = $(foreach v,$(VARIANTS), \
	$(LIB_DIR)/libmultiverse_client_$(v)$(EXT_SO) \
	$(LIB_DIR)/libmultiverse_client_$(v)$(EXT_A) \
	$(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_SO) \
	$(LIB_DIR)/libmultiverse_client_json_$(v)$(EXT_A))

# --- variant test executables ---
TEST_VARIANTS = zmq tcp udp all
TEST_APP_DIR = ..$(PATH_SEP)bin$(PATH_SEP)
TEST_APP_VARIANTS = $(foreach v,$(TEST_VARIANTS),$(TEST_APP_DIR)test_multiverse_client_$(v)$(EXE_EXT))

TEST_USE_FLAGS_zmq = -DUSE_ZMQ=1 -DUSE_TCP=0 -DUSE_UDP=0
TEST_USE_FLAGS_tcp = -DUSE_ZMQ=0 -DUSE_TCP=1 -DUSE_UDP=0
TEST_USE_FLAGS_udp = -DUSE_ZMQ=0 -DUSE_TCP=0 -DUSE_UDP=1
TEST_USE_FLAGS_all = -DUSE_ZMQ=1 -DUSE_TCP=1 -DUSE_UDP=1

$(TEST_APP_DIR)test_multiverse_client_%$(EXE_EXT): $(TEST_CLIENT_SRC) $(CLIENT_SRC) $(CLIENT_JSON_SRC) $(TRANSPORT_CLIENT_SRC)
	@echo "🔧 Building test variant: $* ..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_USE_FLAGS_$*) $^ \
		-L$(EXT_LIB_DIR) -l:libzmq$(EXT_IMP_STATIC) -l:libjsoncpp$(EXT_IMP_STATIC) \
		$(SYS_LIBS) -o $@

# --- Final target ---
all: $(CLIENT_SHARED_LIB) $(CLIENT_STATIC_LIB) $(CLIENT_JSON_SHARED_LIB) $(CLIENT_JSON_STATIC_LIB) \
     $(PYBIND_LIB) $(VARIANT_TARGETS) $(TEST_APP_VARIANTS)
	@$(RM) $(CLIENT_OBJ)
	@$(RM) $(CLIENT_JSON_OBJ)
	@echo "✅ All variants (ZMQ, TCP, UDP, ALL) and tests built."

# --- original targets ---
$(CLIENT_SHARED_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_IMPORT_LIB) -o $@ $(CLIENT_OBJ) -l:libzmq$(EXT_IMP_STATIC) $(SYS_LIBS)

$(CLIENT_STATIC_LIB): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_OBJ) $(EXT_LIB_DIR)$(PATH_SEP)libzmq$(EXT_IMP_STATIC)

$(CLIENT_JSON_SHARED_LIB): $(CLIENT_JSON_SRC) $(CLIENT_SHARED_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,--out-implib,$(CLIENT_JSON_IMPORT_LIB) -o $@ $(CLIENT_JSON_OBJ) $(CLIENT_IMPORT_STATIC_LIB) -l:libjsoncpp$(EXT_IMP_STATIC) $(SYS_LIBS)

$(CLIENT_JSON_STATIC_LIB): $(CLIENT_JSON_SRC) $(CLIENT_STATIC_LIB)
	$(CXX) $(CXXFLAGS) -c $< -o $(CLIENT_JSON_OBJ)
	$(AR) $(ARFLAGS) $@ $(CLIENT_JSON_OBJ) $(CLIENT_STATIC_LIB)

$(PYBIND_LIB): $(PYBIND_SRC) $(CLIENT_STATIC_LIB)
	$(CXX) $(PYBIND_CXXFLAGS) $(LDFLAGS) $< -o $@ $(CLIENT_STATIC_LIB) -l:libzmq$(EXT_IMP_STATIC) $(PYTHON_LIB) $(SYS_LIBS)

clean:
	$(RM) $(CLIENT_SHARED_LIB) $(CLIENT_STATIC_LIB) $(CLIENT_JSON_SHARED_LIB) $(CLIENT_JSON_STATIC_LIB)
	$(RM) $(CLIENT_IMPORT_LIB) $(CLIENT_JSON_IMPORT_LIB) $(PYBIND_LIB)
	$(RM) $(VARIANT_TARGETS)
	$(RM) $(TEST_APP_VARIANTS)
	@echo "🧹 Clean complete (including all variants and tests)."
