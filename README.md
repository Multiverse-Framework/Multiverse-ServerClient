# Multiverse-ServerClient

This repository contains the C++ implementations of both `multiverse_server` and `multiverse_client`.

---

## 🔧 Prerequisites

### Rust Toolchain

Install Rust via rustup:

**On Linux / macOS:**

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
sudo snap install rustup --classic
rustup update stable
```

**On Windows:**

Download and run [rustup-init.exe](https://win.rustup.rs/x86_64) then follow the onscreen instructions.

### Python Dependency (Linux & Windows)

- pybind11
  Install via pip:

  ```bash
  pip install pybind11
  ```

### Windows Setup

To build on Windows, ensure the **Microsoft C++ Build Tools** are properly installed:

1. **Install Visual Studio** (Community, Professional, or Enterprise):  
   👉 [https://visualstudio.microsoft.com/](https://visualstudio.microsoft.com/)

2. During installation, select the **"Desktop development with C++"** workload.

3. This will include the required `vcvarsall.bat` file, typically located at:

   ```bat
   C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvarsall.bat
   ```

> You’ll need to call `vcvarsall.bat` (or use the “Developer Command Prompt for VS”) when building the project to configure the MSVC environment.

---

## 🚀 Getting Started

### On Linux

1. Open a terminal (`Ctrl + Alt + T`)
2. Download this repository:
   - Option 1: Clone via Git:
  
     ```bash
     git clone https://github.com/Multiverse-Framework/Multiverse-ServerClient
     ```

   - Option 2: [Download as ZIP](https://github.com/Multiverse-Framework/Multiverse-ServerClient/archive/refs/heads/main.zip) and extract it.

3. Run the setup script:

   ```bash
   ./Multiverse-ServerClient/setup.sh
   ```

### On Windows

1. Open a Command Prompt (`Win + R` → type `cmd`)
2. Download this repository:
   - Option 1: Clone via Git:

     ```cmd
     git clone https://github.com/Multiverse-Framework/Multiverse-ServerClient
     ```

   - Option 2: [Download as ZIP](https://github.com/Multiverse-Framework/Multiverse-ServerClient/archive/refs/heads/main.zip) and extract it.

3. Run the setup script:

   ```cmd
   .\Multiverse-ServerClient\setup.bat
   ```

---

## 📂 Output

- The compiled **executables** will be located in the `bin` directory.
- The generated **libraries** will be found in `lib/<os>` (e.g., `lib/linux` or `lib/windows`).

---

## ▶️ Running the Server

To run the `multiverse_server`, simply execute the corresponding binary from the `bin` directory:

```bash
./Multiverse-ServerClient/bin/multiverse_server
```

On Windows:

```cmd
.\Multiverse-ServerClient\bin\multiverse_server.exe
```

---

## 🧪 Simple Run Test

This section demonstrates how to start the **server** and **clients** for **ZMQ**, **TCP** and **UDP** transport layers.

### 🖥️ Linux / macOS

```bash
./bin/multiverse_server_cpp \
  --transport zmq --bind "tcp://*:7000" \
  --transport tcp --bind 127.0.0.1:8000 \
  --transport udp --bind 127.0.0.1:9000
```

### 🪟 Windows (PowerShell)

```powershell
.\bin\multiverse_server_rust.exe `
  --transport zmq --bind "tcp://*:7000" `
  --transport tcp --bind 127.0.0.1:8000 `
  --transport udp --bind 127.0.0.1:9000
```

---

## ⚙️ Option 1: ZMQ Transport

### 🖥️ Linux / macOS

Receiver:

```bash
./bin/test_multiverse_client_all --transport zmq --mode receiver --host tcp://127.0.0.1 --server 7000 --client 7001 --sim sim_1
```

Sender:

```bash
./bin/test_multiverse_client_all --transport zmq --mode sender --host tcp://127.0.0.1 --server 7000 --client 7002 --sim sim_2
```

### 🪟 Windows (PowerShell)

Receiver:

```powershell
.\bin\test_multiverse_client_all.exe --transport zmq --mode sender --host tcp://127.0.0.1 --server 7000 --client 7001 --sim sim_1
```

Sender:

```powershell
.\bin\test_multiverse_client_all.exe --transport zmq --mode receiver --host tcp://127.0.0.1 --server 7000 --client 7002 --sim sim_2
```

---

## ⚙️ Option 2: TCP Transport

### 🖥️ Linux / macOS

Sender:

```bash
./bin/test_multiverse_client_all --transport tcp --mode sender --host 127.0.0.1 --server 8000 --client 8001 --sim sim_1
```

Receiver:

```bash
./bin/test_multiverse_client_all --transport tcp --mode receiver --host 127.0.0.1 --server 8000 --client 8002 --sim sim_2
```

### 🪟 Windows (PowerShell)

Sender:

```powershell
.\bin\test_multiverse_client_all.exe --transport tcp --mode sender --host 127.0.0.1 --server 8000 --client 8001 --sim sim_1
```

Receiver:

```powershell
.\bin\test_multiverse_client_all.exe --transport tcp --mode receiver --host 127.0.0.1 --server 8000 --client 8002 --sim sim_2
```

---

## ⚙️ Option 3: UDP Transport

### 🖥️ Linux / macOS

Sender:

```bash
./bin/test_multiverse_client_all --transport udp --mode sender --host 127.0.0.1 --server 9000 --client 9001 --sim sim_1
```

Receiver:

```bash
./bin/test_multiverse_client_all --transport udp --mode receiver --host 127.0.0.1 --server 9000 --client 9002 --sim sim_2
```

### 🪟 Windows (PowerShell)

Sender:

```powershell
.\bin\test_multiverse_client_all.exe --transport udp --mode sender --host 127.0.0.1 --server 9000 --client 9001 --sim sim_1
```

Receiver:

```powershell
.\bin\test_multiverse_client_all.exe --transport udp --mode receiver --host 127.0.0.1 --server 9000 --client 9002 --sim sim_2
```

---

### 💡 Notes

- Choose **either `zmq`, `tcp` or `udp`** as your transport mode.
- All port numbers `7000`, `7001`, or `7002`, etc. are examples — adjust them if needed.
- Ensure all binaries are in your `PATH` or reference them with relative paths.

---
