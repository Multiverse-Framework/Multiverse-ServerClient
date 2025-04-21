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

### Linux

- Build tools: `make`, `g++` (usually found in `/usr/bin/`)
- Libraries: `libzmq`
  Install them using:  
  - For **Ubuntu 24.04**:  
    ```bash
    sudo apt-get install -y libzmqpp-dev
    ```
  - For **Ubuntu 22.04**:  
    ```bash
    sudo apt-get install -y libzmq3-dev
    ```

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
