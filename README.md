![Traymond](https://github.com/fcFn/fcFn.github.io/blob/master/images/logos/traymond_logo.png) Traymond
=======

[中文文档](README.zh.md)

A lightweight tool that minimizes any window to the system tray. Runs silently in the background without occupying taskbar space.

## Features

- **Win + Shift + Z** minimizes the active window to the tray
- Double-click a tray icon to restore the corresponding window
- Auto-start at login via Task Scheduler — no UAC prompt at boot
- Runs as administrator, enabling it to manage other elevated processes (e.g. course automation tools like OCS)
- Automatically recovers hidden windows after an unexpected crash
- Supports Microsoft Store (UWP) apps

## Requirements

Windows 7 or later

## Download

Head to the [Releases](../../releases) page and download the latest `Traymond.exe`. No installation required — just run it.

> **First launch** will show a UAC elevation prompt. Administrator rights are required so Traymond can hide windows belonging to other elevated processes.

## Usage

### Hotkey

| Action | Description |
|--------|-------------|
| **Win + Shift + Z** | Minimize the active window to the system tray |
| **Double-click tray icon** | Restore the corresponding window and bring it to the foreground |

### Tray menu (right-click the Traymond icon)

| Item | Description |
|------|-------------|
| ☑ Auto-start at login | Toggle auto-start; a checkmark means it is enabled |
| Restore all windows | Restore every hidden window at once |
| Exit | Exit Traymond and restore all hidden windows |

## Auto-start at Login

Right-click the tray icon and click **Auto-start at login** to toggle.

Traymond uses the **Windows Task Scheduler** instead of the registry `Run` key because:

- Traymond embeds a `requireAdministrator` manifest and needs elevated rights
- Programs in the registry `Run` key cannot be auto-elevated at logon and will error out
- Task Scheduler supports `TASK_RUNLEVEL_HIGHEST`, launching at full admin rights with no UAC prompt

The task starts **30 seconds after login** to ensure the Windows taskbar is fully ready before the tray icon is added (prevents icon loss on cold boot or slow machines).

## Supported Application Types

| Type | Supported | Notes |
|------|-----------|-------|
| Standard Win32 apps | ✅ | Fully supported |
| Chromium-based (Chrome, Edge) | ✅ | Fully supported |
| Electron apps (OCS, etc.) | ✅ | Fully supported |
| Microsoft Store / UWP apps | ✅ | Supported |
| UWP system UI (Start menu, Search, Action Center) | ❌ | Protected by the OS |
| Taskbar and desktop | ❌ | Protected by the OS |

> **Privilege note**: if the target app runs as administrator, Traymond must also run as administrator (configured by default). If hiding fails, Traymond will show a diagnostic message.

## Customizing the Hotkey

Edit the macros at the top of `src/traymond.cpp` and recompile:

```cpp
#define TRAY_KEY VK_Z_KEY             // trigger key (virtual-key code)
#define MOD_KEY  MOD_WIN + MOD_SHIFT  // modifier keys
```

- Virtual-key codes: [MSDN reference](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes)
- Modifier keys: [RegisterHotKey reference](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerhotkey)

## Building

### MSBuild (recommended)

Requires Visual Studio 2019 / 2022 or Build Tools for Visual Studio:

```
msbuild traymond.sln /p:Configuration=Release /p:Platform=x86
```

### NMake

```
nmake
```

### GitHub Actions automated release

Push a `v*` tag and CI will build and publish `Traymond.exe` to the Releases page automatically:

```
git tag v1.x.x
git push origin v1.x.x
```

## Changes from Upstream

This fork extends [fcFn/traymond](https://github.com/fcFn/traymond) with the following improvements:

| Improvement | Description |
|-------------|-------------|
| Chromium / Electron support | Multi-step icon fallback chain; rolls back tray icon and shows a message if `ShowWindow` is rejected |
| Microsoft Store app support | Fixes silent early-exit caused by missing icons on `ApplicationFrameWindow`; reads icon from inner `CoreWindow` child |
| UIPI privilege barrier | Detects cross-integrity-level hide failures and shows a clear diagnostic instead of silently doing nothing |
| Tray icon ID collisions fixed | Monotonically increasing IDs replace `LOWORD(HWND)`, preventing silent `NIM_ADD` failures when multiple HWNDs share the same low 16 bits |
| Auto-start at login | Task Scheduler approach works for elevated apps; registry `Run` key does not |
| Tray icon recovery | Listens for `TaskbarCreated` and re-adds the icon if explorer.exe restarts |
| Stable save-file path | `traymond.dat` is always written next to the exe, not in whatever the current working directory happens to be |

## Contributing

Issues and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT License](LICENSE.md)
