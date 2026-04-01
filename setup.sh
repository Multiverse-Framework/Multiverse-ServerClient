#!/bin/bash

set -euo pipefail

start_time=$(date +%s)

# --- Ensure Rust (cargo) is installed ---
if ! command -v cargo >/dev/null 2>&1; then
    echo "[INFO] Rust not found. Installing via rustup..."

    curl https://sh.rustup.rs -sSf | sh -s -- -y

    # Load cargo into current shell
    export PATH="$HOME/.cargo/bin:$PATH"

    # Optional: ensure toolchain is ready
    source "$HOME/.cargo/env"
else
    echo "[INFO] Rust already installed."
fi

# --- Verify cargo again (fail fast if still missing) ---
if ! command -v cargo >/dev/null 2>&1; then
    echo "[ERROR] Cargo still not found after installation."
    exit 1
fi

# --- Build ---
echo "[INFO] Building multiverse_server_rust..."
(
    cd multiverse_server_rust || exit
    make clean
    make install
)

export PATH=/usr/bin

echo "Building multiverse_server_cpp..."
(cd multiverse_server_cpp || exit; make clean; make)

echo "Building multiverse_client..."
(cd multiverse_client || exit; make clean; make)

echo "Building multiverse_client pybind..."
(cd multiverse_client || exit; bash ./build_pybind.sh)

echo "Formatting source code with clang-format..."
find . -path ./ext/include -prune -o \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 -P "$(nproc)" clang-format -i

end_time=$(date +%s)
elapsed=$(( end_time - start_time ))

echo "Build completed in $elapsed seconds"