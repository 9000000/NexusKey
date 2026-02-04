# Building NextKey

This project uses C++20 and targets Windows (specifically for TSF).

## Prerequisites
1. **Windows 10/11**
2. **Visual Studio 2022** (with "Desktop development with C++" and "ATL support")
3. **CMake 3.20+**
4. **toml++**: This project uses `toml++` for configuration. 
   - Download `toml.hpp` from [marzer/tomlplusplus](https://github.com/marzer/tomlplusplus/blob/master/toml.hpp).
   - Place it in `src/vendor/toml.hpp`.

## Build Instructions

1. Open a terminal (PowerShell or Developer Command Prompt).
2. Create a build directory:
   ```powershell
   mkdir build
   cd build
   ```
3. Generate the solution:
   ```powershell
   cmake ..
   ```
4. Build the project:
   ```powershell
   cmake --build . --config Release
   ```

## Verification & Installation

### 1. Run NextKey Core
The `NextKeyApp.exe` must be running for the system to work. It initializes the shared memory.

### 2. Register TSF DLL
To use NextKey as an input method, you must register the DLL:
1. Open an **Administrator** Command Prompt.
2. Navigate to your build output folder (e.g., `build/Release`).
3. Run:
   ```cmd
   regsvr32 NextKeyTSF.dll
   ```
4. You should now see "NextKey" in your Windows Language bar (Vietnamese language).

### 3. Uninstall
To remove:
```cmd
regsvr32 /u NextKeyTSF.dll
```

## Running Tests

After building, run the test suite:

```powershell
cd build
ctest --output-on-failure
```

Or run the test executable directly for verbose output:

```powershell
.\NextKeyTests.exe
```

