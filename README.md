# Multiverse-ServerClient #

This repository provides the core communication layer of the Multiverse framework.

It includes:

- **C++ implementations** of multiverse_server and multiverse_client
- A **Rust implementation** of multiverse_server
- **Python bindings** for multiverse_client (via pybind11)

The goal is to support high-performance, cross-language communication between simulators, controllers, and external tools.

---

## 🔧 Prerequisites ##

### Rust Toolchain ###

Install Rust via [rustup](https://sh.rustup.rs):

**On Linux / macOS:**

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
sudo snap install rustup --classic
rustup update stable
```

**On Windows:**

Download and run [rustup-init.exe](https://win.rustup.rs/x86_64) then follow the onscreen instructions.

### Python Dependency (Linux & Windows) ###

- pybind11
  Install via pip:

  ```bash
  pip install -U pybind11
  ```

### Windows Setup ###

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

## 🚀 Getting Started ##

### Linux ###

1. Open a terminal (`Ctrl + Alt + T`)

2. Clone the repository from Bitbucket:

   ```bash
   export VR_USER=<your_username>   # e.g. v.giangnh84
   git clone https://${VR_USER}@bitbucket.org/vinrobotics/multiverse-serverclient.git
   cd multiverse-serverclient
   ```

3. Run the setup script:

   ```bash
   ./setup.sh
   ```

---

### Windows ###

1. Open **Command Prompt** (`Win + R` → `cmd`)

2. Clone the repository from Bitbucket:

   ```cmd
   set VR_USER=<your_username>   REM e.g. v.giangnh84
   git clone https://%VR_USER%@bitbucket.org/vinrobotics/multiverse-serverclient.git
   cd multiverse-serverclient
   ```

3. Run the setup script:

   ```cmd
   .\setup.bat
   ```

---

## 📂 Output ##

- The compiled **executables** will be located in the `bin` directory.
- The generated **libraries** will be found in `lib/<os>` (e.g., `lib/linux` or `lib/windows`).

---

## ▶️ Running the Server ##

The Multiverse server can be launched using **either the C++ or Rust implementation**.
Both binaries are located in the `bin` directory.

---

### C++ Server (Linux/macOS) ###

```bash
./bin/multiverse_server_cpp \
  --transport zmq --bind "tcp://*:7000" \
  --transport tcp --bind 127.0.0.1:8000 \
  --transport udp --bind 127.0.0.1:9000
```

### Rust server (Linux/macOS) ###

```bash
./bin/multiverse_server_rust \
  --transport zmq --bind "tcp://*:7000" \
  --transport tcp --bind 127.0.0.1:8000 \
  --transport udp --bind 127.0.0.1:9000
```

### C++ Server (Windows) ###

```powershell
.\bin\multiverse_server_cpp.exe `
  --transport zmq --bind "tcp://*:7000" `
  --transport tcp --bind 127.0.0.1:8000 `
  --transport udp --bind 127.0.0.1:9000
```

### Rust server (Windows) ###

```powershell
.\bin\multiverse_server_rust.exe `
  --transport zmq --bind "tcp://*:7000" `
  --transport tcp --bind 127.0.0.1:8000 `
  --transport udp --bind 127.0.0.1:9000
```

---

### 💡 Notes ###

- You may enable **one or multiple transports** (`zmq`, `tcp`, `udp`) at the same time.
- Port numbers (`7000`, `8000`, `9000`, etc.) are examples—adjust them to match your deployment.
- The Rust and C++ servers expose the same runtime interface and can be used interchangeably.
