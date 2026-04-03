Spektrometr — build & deploy notes
=================================

Short summary
-------------
This repo contains a Qt/C++ application (Spektrometr). The README lists commands and scripts to build and prepare distribution for Windows and Linux.

Important
---------
- Build `Release` (do not distribute Debug). Debug CRT (MSVCP140D.dll, ucrtbased.dll, etc.) must NOT be distributed.
- Pixelink camera requires Pixelink SDK/driver installed on target PC (or shipping vendor installer and running it during install).
- Visual C++ Redistributable must be present on Windows target machines (install the official VC++ redistributable).

Windows — build & prepare Release
---------------------------------
1. Build Release (Visual Studio or CMake)
   - Visual Studio: set configuration to `Release|x64` and `Build -> Rebuild Solution`.
   - CMake: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --config Release`.

2. Collect Qt runtime
   - Run `windeployqt` for your Qt installation to copy Qt DLLs and plugins into the Release folder.
   - Example (Qt 6 msvc):
     "C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe" "C:\path\to\Release\Spektrometr.exe" --release

3. Copy Pixelink runtime DLLs
   - Use the provided script to copy Pixelink SDK DLLs into the Release folder:
     - `tools\copy_pixelink_runtime.ps1 -SdkBinPath "C:\Program Files\Pixelink\bin" -TargetDir "C:\path\to\Release" -Arch x64`
   - Use `-WhatIf` first to simulate.
   - Alternatively, include (and run) the official Pixelink SDK/driver installer on the target machine.

4. (Optional) Copy MSVC CRT — RECOMMENDED: run VC++ redistributable installer on target
   - It's better to run the official installer `vc_redist.x64.exe` on the target machine.
   - The helper script `tools\deploy_release.ps1` can copy MSVCP140.dll/VCRUNTIME140.dll from the local system into Release (but packaging the redistributable installer is preferable).

5. Deploy / Installer
   - Use the Inno Setup template `installer\SpektrometrInstaller.iss` to create an installer.
   - Example ISCC invocation (edit paths or pass via /D preprocessor):
     ISCC.exe /DSourceReleaseDir="C:\path\to\Release" /DVcRedistPath="C:\path\to\vc_redist.x64.exe" /DPixelinkInstallerPath="C:\path\to\PixelinkInstaller.exe" installer\SpektrometrInstaller.iss

Scripts
-------
- `tools\copy_pixelink_runtime.ps1` — copy Pixelink DLLs from SDK `bin` into target dir (has -WhatIf simulation).
- `tools\deploy_release.ps1` — wrapper: optionally runs `windeployqt`, calls copy_pixelink_runtime, tries to copy CRTs and writes `deploy_report.txt`.

Common checks (Windows)
------------------------
- Use Dependencies (https://github.com/lucasg/Dependencies) or `dumpbin /DEPENDENTS Spektrometr.exe` to list required DLLs.
- If app fails to start on a fresh PC: check missing dependency message ("X.dll is missing") and install the corresponding redistributable / SDK / Qt plugin.

Linux — build & package
-----------------------
1. Install build dependencies (example Ubuntu):
   - `sudo apt install build-essential cmake qt6-base-dev libqt6serialport-dev`
   - Adjust package names for Qt5 if using Qt5.

2. Build
   - `mkdir build && cd build`
   - `cmake .. -DCMAKE_BUILD_TYPE=Release [-DPIXELINK_SDK_ROOT=/path/to/pixelink]`
   - `cmake --build . -j$(nproc)`

3. Bundle as AppImage
   - Script: `tools/deploy_linux.sh -b build -p /path/to/pixelink` will build, stage exe and optional Pixelink .so and call `linuxdeployqt` to create an AppImage.
   - Install `linuxdeployqt` and ensure it's in PATH.

Notes about Pixelink on Linux
-----------------------------
- If Pixelink provides a Linux SDK (shared libs .so), give its root path to CMake via `-DPIXELINK_SDK_ROOT` so the app links to `libPxLApi.so`.
- If no Linux SDK is available, the program will compile but Pixelink camera features will be unavailable — app can still open CSV/heatmap files.

Testing
-------
- Always test the final installer/AppImage on a clean VM to catch missing system libs/redistributables.

License / redistribution
------------------------
- Check Qt license requirements for redistribution.
- Do not redistribute Pixelink DLLs/driver installers unless allowed — prefer to include the vendor installer and run it during installation.

If you forget steps
-------------------
- The `tools` scripts and `installer\SpektrometrInstaller.iss` contain most commands.
- Key sequence to remember: Build Release -> `windeployqt` -> copy Pixelink DLLs -> include VC++ redistributable or run it on target -> create installer.

Questions / help
----------------
If anything fails on a target machine, paste the exact error message from Windows or the output from `ldd`/`strace` on Linux and I'll help debug.
