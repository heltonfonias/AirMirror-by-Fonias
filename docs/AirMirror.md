# AirMirror by Fonias — unified Windows app

The current AirMirror release is the compiled `AirMirror.exe` and its MSI installer. UxPlay is linked into that executable as a library; you do not install or launch a second UxPlay tray app. AirMirror owns one tray icon, one receiver lifecycle, the branded controller, and the settings dialog. It uses local-network discovery through Bonjour and does not launch the upstream Bluetooth beacon helper. UxPlay still opens a separate video window during mirroring.

## Install and use

1. Run `AirMirror-by-Fonias-x64.msi` from `dist/`. The installer includes the receiver engine, required media runtime files, the AirMirror icon, Start menu shortcut, and optional desktop shortcut. It installs for the PC and asks for administrator approval. Bonjour service setup may also ask for approval if Bonjour is missing.
2. Open **AirMirror by Fonias**. The receiver starts in the same app process. From your iPhone, open Control Center, tap **Screen Mirroring**, and choose the receiver name shown in AirMirror.
3. Use **Settings** for the receiver name, renderer, fullscreen, resolution, frame rate, HEVC, audio, connection, startup, and additional UxPlay arguments. Saving applies changes to the running receiver without launching another tray app.
4. Closing the window keeps AirMirror in the tray. Right-click its icon to start or stop receiving, open settings, view the log, or quit.

For a portable copy, extract `AirMirror-by-Fonias-x64-portable.zip` as a complete folder and run `AirMirror.exe`. The DLLs and subfolders next to it are part of the app. No separate UxPlay installation is needed.

The app copies an existing user `arguments.txt` and display settings from the prior UxPlay Windows location on first launch. A machine-wide `%ProgramData%\uxplay-windows\arguments.txt` still takes priority when present. Installing AirMirror does not uninstall a separately installed UxPlay app; quit or uninstall that old app if it is still running.

## Source and license

AirMirror is built from the [UxPlay Windows source](https://github.com/leapbtw/uxplay-windows) with the [libuxplay submodule](https://github.com/leapbtw/libuxplay). The AirPlay engine remains GPL-3.0 software. `upstream-source/` holds the modified build source; `upstream-source/docs/LICENSE.rtf` and the installed `LICENSE.rtf` carry its license. The app icon was generated for AirMirror by Fonias; its prompt and provenance are in `assets/ICON-PROVENANCE.md`.

To reproduce the x64 installer on Windows, install MSYS2, Visual Studio C++ Build Tools, and the .NET SDK, then run `upstream-source/build.ps1 package -Architecture x64`. The build script installs required MSYS2 packages, compiles the app, bundles its runtime, runs a self-test, and builds the portable ZIP and MSI.
