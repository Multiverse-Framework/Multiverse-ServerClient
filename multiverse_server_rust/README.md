# Multiverse Server (Rust)

A high-performance multiverse server implementation in Rust with support for multiple transport protocols (ZMQ, TCP, UDP).

## ▶️ Running the Server

### Using the Binary

**On Linux / macOS:**

```bash
./target/release/multiverse_server_rust
```

**On Windows:**

```cmd
.\target\release\multiverse_server_rust.exe
```

### Using Cargo

```bash
cargo run --release
```

---

## 🧪 Usage Examples

### Default Server (ZMQ on port 7000)

```bash
cargo run --release
```

### Single Transport

#### ZMQ Transport

**Linux / macOS:**

```bash
./target/release/multiverse_server_rust --transport zmq --bind "tcp://*:7000"
```

**Windows (PowerShell):**

```powershell
.\target\release\multiverse_server_rust.exe --transport zmq --bind "tcp://*:7000"
```

**With Cargo:**

```bash
cargo run --release -- --transport zmq --bind "tcp://*:7000"
```

#### TCP Transport

**Linux / macOS:**

```bash
./target/release/multiverse_server_rust --transport tcp --bind 0.0.0.0:8000
```

**Windows (PowerShell):**

```powershell
.\target\release\multiverse_server_rust.exe --transport tcp --bind 0.0.0.0:8000
```

**With Cargo:**

```bash
cargo run --release -- --transport tcp --bind 0.0.0.0:8000
```

#### UDP Transport

**Linux / macOS:**

```bash
./target/release/multiverse_server_rust --transport udp --bind 0.0.0.0:9000
```

**Windows (PowerShell):**

```powershell
.\target\release\multiverse_server_rust.exe --transport udp --bind 0.0.0.0:9000
```

**With Cargo:**

```bash
cargo run --release -- --transport udp --bind 0.0.0.0:9000
```

---

## ⚙️ Multiple Transports (ZMQ + TCP + UDP)

You can run multiple transport protocols simultaneously:

### Linux / macOS

```bash
./target/release/multiverse_server_rust --transport zmq --bind "tcp://*:7000" --transport tcp --bind 0.0.0.0:8000 --transport udp --bind 0.0.0.0:9000
```

### Windows (PowerShell)

```powershell
.\target\release\multiverse_serve_rust.exe --transport zmq --bind "tcp://*:7000" --transport tcp --bind 0.0.0.0:8000 --transport udp --bind 0.0.0.0:9000
```

**With Cargo:**

```powershell
cargo run --release -- --transport zmq --bind "tcp://*:7000" --transport tcp --bind 0.0.0.0:8000 --transport udp --bind 0.0.0.0:9000
```

---

## 🔍 Logging Configuration

Control logging verbosity using the `RUST_LOG` environment variable:

### Linux / macOS

```bash
# Info level (default)
RUST_LOG=info ./target/release/multiverse_server_rust --transport tcp --bind 0.0.0.0:7000

# Debug level
RUST_LOG=debug ./target/release/multiverse_server_rust --transport tcp --bind 0.0.0.0:7000

# Warning level only
RUST_LOG=warn ./target/release/multiverse_server_rust --transport tcp --bind 0.0.0.0:7000

# Trace level (most verbose)
RUST_LOG=trace ./target/release/multiverse_server_rust --transport tcp --bind 0.0.0.0:7000
```

**With Cargo:**

```bash
RUST_LOG=debug cargo run --release -- --transport tcp --bind 0.0.0.0:7000
```

### Windows (PowerShell)

```powershell
# Set environment variable
$env:RUST_LOG="debug"

# Run the server
.\target\release\multiverse_server_rust.exe --transport tcp --bind 0.0.0.0:7000
```

**With Cargo:**

```powershell
$env:RUST_LOG="debug"
cargo run --release -- --transport tcp --bind 0.0.0.0:7000
```

---

## 📋 Command Line Options

```bash
USAGE:
    multiverse_server_rust [OPTIONS]

OPTIONS:
    --transport <TYPE>    Transport type (zmq, tcp, or udp)
    --bind <ADDRESS>      Bind address for the transport
    -h, --help           Print help information
    -V, --version        Print version information
```

---

## 🏗️ Architecture

- **Single-threaded async runtime**: Optimized for the dispatcher/worker pattern using Tokio's `current_thread` runtime
- **Dispatcher/Worker pattern**: Each transport has a dispatcher that spawns workers on demand
- **Graceful shutdown**: Handles `Ctrl+C` (SIGINT) for clean shutdown

---

## 🎯 Features

The server supports three transport protocols, each can be enabled/disabled at compile time:

- `use-zmq`: Enable ZeroMQ transport
- `use-tcp`: Enable TCP transport  
- `use-udp`: Enable UDP transport

**Default features:** All transports are enabled by default.

To build with specific features:

```bash
# Only TCP and UDP (no ZMQ)
cargo build --release --features use-tcp,use-udp --no-default-features

# Only ZMQ
cargo build --release --features use-zmq --no-default-features
```

---

## 💡 Notes

- **Default behavior**: If no transport is specified, the server defaults to ZMQ on `tcp://*:7000` (or TCP on `0.0.0.0:7000` if ZMQ is not enabled)
- **Port numbers**: `7000`, `8000`, `9000` are examples — adjust them as needed
- **Bind addresses**:
  - ZMQ: Use ZMQ-style URLs like `tcp://*:7000` or `tcp://0.0.0.0:7000`
  - TCP/UDP: Use `host:port` format like `0.0.0.0:7000` or `127.0.0.1:7000`
- **Multiple instances**: You can run multiple transport protocols on the same server instance
- **Graceful shutdown**: Press `Ctrl+C` to stop the server cleanly

---

## 🐛 Troubleshooting

### Port Already in Use

If you see an error like "Address already in use", another process is using that port:

```bash
# On Linux/macOS - find what's using the port
lsof -i :7000

# On Windows - find what's using the port
netstat -ano | findstr :7000
```

### Feature Not Enabled

If you see "No valid transport specifications provided", make sure you've built with the required features:

```bash
# Check which features are enabled
cargo build --release --features use-tcp,use-udp,use-zmq
```

### Logging Not Showing

Make sure `RUST_LOG` is set before running the server:

```bash
# Linux/macOS
export RUST_LOG=debug

# Windows (PowerShell)
$env:RUST_LOG="debug"
```
