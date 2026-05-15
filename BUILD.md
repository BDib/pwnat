# Building pwnat

pwnat supports both **CMake** and **Meson** build systems, providing consistent cross-platform builds for Linux, macOS, and Windows.

## Prerequisites

- **C compiler** with C11 support (e.g., GCC, Clang, or MSVC)
- **CMake** (version 3.10 or higher) **OR** **Meson** (and **Ninja**)
- **libnice-dev** (Optional but recommended for ICE/STUN/TURN support)
  - On Ubuntu/Debian: `sudo apt-get install libnice-dev`

## Build Steps (Linux and macOS)

### Using CMake
1. **Create a build directory:**
   ```bash
   mkdir build && cd build
   ```
2. **Configure and build:**
   ```bash
   cmake ..
   make
   ```

### Using Meson
1. **Configure the project:**
   ```bash
   meson setup build
   ```
2. **Build the executable:**
   ```bash
   meson compile -C build
   ```

## Build Steps (Windows)

1. **Open a command prompt or PowerShell window.**
2. **Create a build directory:**
   ```powershell
   mkdir build
   cd build
   ```
3. **Configure and build (CMake):**
   ```powershell
   cmake ..
   cmake --build . --config Release
   ```

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
