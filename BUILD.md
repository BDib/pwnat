# Building pwnat

pwnat uses **CMake** as its build system, which provides a consistent way to build the project across Linux, macOS, and Windows.

## Prerequisites

- **CMake** (version 3.10 or higher)
- **C compiler** with C11 support (e.g., GCC, Clang, or MSVC)
- **Make** (on Unix-like systems)

## Build Steps (Linux and macOS)

1. **Create a build directory:**
   ```bash
   mkdir build
   cd build
   ```

2. **Configure the project:**
   ```bash
   cmake ..
   ```

3. **Build the executable:**
   ```bash
   make
   ```

The `pwnat` binary will be located in the `build` directory.

## Build Steps (Windows)

1. **Open a command prompt or PowerShell window.**
2. **Create a build directory:**
   ```powershell
   mkdir build
   cd build
   ```
3. **Configure the project:**
   ```powershell
   cmake ..
   ```
4. **Build the project:**
   ```powershell
   cmake --build . --config Release
   ```

The `pwnat.exe` binary will be located in the `build/Release` directory.

## Permissions

**Important:** Running pwnat requires **root** (on Linux/macOS) or **Administrator** (on Windows) privileges. This is because pwnat needs to create raw ICMP sockets to facilitate NAT-to-NAT communication.

### Example (Linux/macOS):
```bash
sudo ./pwnat -s
```

### Example (Windows):
Run the command prompt as an Administrator, then:
```powershell
.\pwnat.exe -s
```
