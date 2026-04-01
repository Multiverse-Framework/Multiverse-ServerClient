# Makefile for multiverse_server_rust

CARGO = cargo
BIN_NAME = multiverse_server_rust

# --------------------------------------------------------------------
# Detect OS
# --------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    BIN_DIR = ..\bin
    EXE_EXT = .exe
    MKDIR = if not exist "$(BIN_DIR)" mkdir "$(BIN_DIR)"
    CP = copy /Y
    RM = del /Q 2>nul
    RELEASE_BIN = target\release\$(BIN_NAME)$(EXE_EXT)
    INSTALL_BIN = $(BIN_DIR)\$(BIN_NAME)$(EXE_EXT)
    # Rust check/install
    CARGO_CHECK = where cargo >nul 2>&1
    RUSTUP_INSTALL = powershell -Command "Invoke-WebRequest -Uri 'https://win.rustup.rs/x86_64' -OutFile '$$env:TEMP\rustup-init.exe'; & $$env:TEMP\rustup-init.exe -y"
else
    BIN_DIR = ../bin
    EXE_EXT =
    MKDIR = mkdir -p $(BIN_DIR)
    CP = cp -f
    RM = rm -f
    RELEASE_BIN = target/release/$(BIN_NAME)$(EXE_EXT)
    INSTALL_BIN = $(BIN_DIR)/$(BIN_NAME)$(EXE_EXT)
    # Rust check/install
    CARGO_CHECK = command -v cargo >/dev/null 2>&1
    RUSTUP_INSTALL = curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y && . "$$HOME/.cargo/env"
endif

# --------------------------------------------------------------------
# Targets
# --------------------------------------------------------------------
TARGET = $(BIN_DIR)/$(BIN_NAME)$(EXE_EXT)

.PHONY: all release debug install clean setup-rust check-rust

# --------------------------------------------------------------------
# Default rule: build and install
# --------------------------------------------------------------------
all: install

# --------------------------------------------------------------------
# Rust environment setup
# --------------------------------------------------------------------
check-rust:
	@echo "Checking Rust installation..."
	@$(CARGO_CHECK) || (echo "Rust/Cargo not found." && exit 1)
	@echo "Rust is installed."
	@$(CARGO) --version

setup-rust:
	@echo "Checking Rust installation..."
ifeq ($(OS),Windows_NT)
	@$(CARGO_CHECK) || ( \
		echo "Rust not found. Installing..." && \
		$(RUSTUP_INSTALL) \
	)
else
	@$(CARGO_CHECK) || ( \
		echo "Rust not found. Installing..." && \
		$(RUSTUP_INSTALL) \
	)
endif

# --------------------------------------------------------------------
# Build targets
# --------------------------------------------------------------------
release: setup-rust
	@echo "Building release..."
	$(CARGO) build --release

debug: setup-rust
	@echo "Building debug..."
	$(CARGO) build

# --------------------------------------------------------------------
# Install target (matches C++ Makefile behavior)
# --------------------------------------------------------------------
install: release
	@echo "Installing to $(BIN_DIR)..."
	$(MKDIR)
	$(CP) $(RELEASE_BIN) $(INSTALL_BIN)
	@echo "Installed: $(INSTALL_BIN)"

# --------------------------------------------------------------------
# Cleaning
# --------------------------------------------------------------------
clean:
	@echo "Cleaning build artifacts..."
	$(CARGO) clean
	-$(RM) $(INSTALL_BIN)
