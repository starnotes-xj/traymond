# Traymond

将任意窗口最小化到系统托盘的轻量工具。在后台静默运行，不占用任务栏空间。

## 功能特性

- **Win + Shift + Z** 将当前活动窗口最小化到托盘
- 双击托盘图标还原窗口
- 支持开机自启动（通过任务计划程序，无 UAC 弹窗）
- 以管理员权限运行，可操作同样以管理员身份启动的程序（如 OCS 等刷课软件）
- 意外崩溃后自动恢复已隐藏的窗口
- 支持 Microsoft Store 应用

## 系统要求

Windows 7 及以上

## 下载安装

前往 [Releases](../../releases) 页面下载最新版 `Traymond.exe`，无需安装，直接运行即可。

> **首次运行**会弹出 UAC 提权提示。Traymond 需要管理员权限才能操作以管理员身份运行的第三方程序（如部分刷课软件）。

## 使用方法

### 快捷键

| 操作 | 说明 |
|------|------|
| **Win + Shift + Z** | 将当前活动窗口最小化到系统托盘 |
| **双击托盘图标** | 还原对应窗口并将其置于前台 |

### 托盘菜单（右键单击 Traymond 图标）

| 菜单项 | 说明 |
|-------|------|
| ☑ 开机自启动 / Auto-start at login | 开启 / 关闭开机自启动，带对勾表示已启用 |
| 语言：中文 / Language: English | 切换托盘菜单语言 |
| 恢复所有窗口 / Restore all windows | 还原所有已隐藏的窗口 |
| 退出 / Exit | 退出 Traymond 并还原所有隐藏的窗口 |

## 开机自启动

右键单击托盘图标，点击 **Auto-start at login** 即可切换。

Traymond 通过 **Windows 任务计划程序**（而非注册表 Run 键）实现开机自启动。这是因为：

- Traymond 带有 `requireAdministrator` manifest，需要管理员权限
- 注册表 Run 键启动的程序无法被自动提权，会直接报错
- 任务计划程序支持以最高权限运行，登录时无需任何 UAC 弹窗

任务在登录后**延迟 30 秒**启动，确保 Windows 任务栏完全就绪后再添加托盘图标（避免冷启动时图标丢失）。

## 支持的应用类型

| 应用类型 | 支持情况 | 备注 |
|---------|---------|------|
| 普通 Win32 应用 | ✅ | 完全支持 |
| Chromium 系（Chrome、Edge）| ✅ | 完全支持 |
| Electron 应用（OCS 等）| ✅ | 完全支持 |
| Microsoft Store 应用 | ✅ | 支持 |
| UWP 系统界面（开始菜单、搜索栏、操作中心）| ❌ | 系统保护限制 |
| 任务栏、桌面 | ❌ | 系统保护限制 |

> **管理员权限说明**：若目标程序以管理员权限运行，Traymond 也必须以管理员权限运行（默认已配置）。如果隐藏失败，Traymond 会弹出提示。

## 自定义快捷键

修改 `src/traymond.cpp` 顶部的宏，重新编译后生效：

```cpp
#define TRAY_KEY VK_Z_KEY          // 触发键（参考 MSDN 虚拟键码）
#define MOD_KEY  MOD_WIN + MOD_SHIFT  // 修饰键（参考 MSDN ModifierKeys）
```

- 虚拟键码参考：[Virtual-Key Codes](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes)
- 修饰键参考：[RegisterHotKey](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerhotkey)

## 构建

### MSBuild（推荐）

需要 Visual Studio 2019 / 2022 或 Build Tools for Visual Studio：

```
msbuild traymond.sln /p:Configuration=Release /p:Platform=x86
```

### NMake

```
nmake
```

### GitHub Actions 自动发布

推送 `v*` 格式的 tag，CI 将自动构建并在 Releases 页面发布可执行文件：

```
git tag v1.x.x
git push origin v1.x.x
```

## 与原版的区别

本 fork 在 [fcFn/traymond](https://github.com/fcFn/traymond) 基础上做了以下改进：

| 改进项 | 说明 |
|-------|------|
| Chromium / Electron 支持 | 多级图标回退链；`ShowWindow` 失败时回滚并提示 |
| Microsoft Store 应用支持 | 修复原版因图标获取失败而静默退出的问题 |
| UIPI 权限屏障处理 | 检测跨完整性级别的隐藏失败，显示明确错误提示 |
| 托盘图标 ID 不冲突 | 改用单调递增 ID，避免 HWND 低位碰撞导致静默失败 |
| 单窗口复原更稳健 | 同时兼容新版和旧版托盘回调 ID 格式，双击图标复原会命中与“恢复所有窗口”相同的隐藏窗口列表 |
| 中英文托盘菜单 | 新增托盘菜单语言切换，并记住已选择的语言 |
| 开机自启动 | 任务计划程序方式，管理员权限程序无 UAC 弹窗 |
| 崩溃后图标恢复 | 监听 `TaskbarCreated` 消息，Explorer 重启后自动恢复托盘图标 |
| 存档路径固定 | `traymond.dat` 始终写在 exe 旁边，任务计划程序启动时不再写入 System32 |

## 贡献

欢迎提交 Issue 和 Pull Request，详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可

[MIT License](LICENSE.md)
