# Multiverse-ServerClient

This repository contains the C++ implementations of both `multiverse_server` and `multiverse_client`.

---

## 🔧 Prerequisites

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
   ```
   C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
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

## Project structure

```bash
.
├── bin
│   └── multiverse_server
├── ext
│   └── include
├── lib
│   ├── linux
│   └── windows
├── LICENSE
├── multiverse_client
│   ├── Makefile
│   ├── multiverse_client.cpp
│   ├── multiverse_client.h
│   ├── tcp
│   │   ├── multiverse_client_tcp.cpp
│   │   ├── multiverse_client_tcp.h
│   │   └── multiverse_client_tcp_pybind.cpp
│   ├── udp
│   │   ├── multiverse_client_udp.cpp
│   │   ├── multiverse_client_udp.h
│   │   └── multiverse_client_udp_pybind.cpp
│   └── zmq
│       ├── multiverse_client_zmq.cpp
│       ├── multiverse_client_zmq.h
│       └── multiverse_client_zmq_pybind.cpp
├── multiverse_server
│   ├── main.cpp
│   ├── Makefile
│   ├── multiverse_server.cpp
│   └── multiverse_server.h
├── README.md
├── setup.bat
└── setup.sh
```
