# Multiverse-ServerClient

This repository contains the C++ implementations of both `multiverse_server` and `multiverse_client`.

---

## 🔧 Prerequisites

### Linux

- Build tools: `make`, `g++` (usually found in `/usr/bin/`)
- Libraries: `libzmqcpp3-dev`, `libjsoncpp-dev`  
  Install them using:  
  ```bash
  sudo apt-get install -y libzmqcpp3-dev libjsoncpp-dev
  ```

### Windows

- Build tools: `mingw32-make.exe`, `g++.exe` (must be in your `%PATH%`)
- Libraries: `mingw-w64-x86_64-zeromq`, `mingw-w64-x86_64-cppzmq`, `mingw-w64-x86_64-jsoncpp`  
  Install them using:
  ```bash
  pacman -Sy --noconfirm mingw-w64-x86_64-zeromq mingw-w64-x86_64-cppzmq mingw-w64-x86_64-jsoncpp
  ```

---

## 🚀 Getting Started

### On Linux

1. Open a terminal (`Ctrl + Alt + T`)
2. Run the setup script:

   ```bash
   ./setup.sh
   ```

### On Windows

1. Open a Command Prompt (`Win + R` → type `cmd`)
2. Run the setup script:

   ```cmd
   .\setup.bat
   ```

---

## 📂 Output

- The compiled **executables** will be located in the `bin` directory.
- The generated **libraries** will be found in `lib/<os>` (e.g., `lib/linux` or `lib/windows`).

---

## ▶️ Running the Server

To run the `multiverse_server`, simply execute the corresponding binary from the `bin` directory:

```bash
./bin/multiverse_server
```

On Windows:

```cmd
.\bin\multiverse_server.exe
```
