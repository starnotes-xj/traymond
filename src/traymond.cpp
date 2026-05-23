#include <Windows.h>
#include <windowsx.h>
#include <taskschd.h>
#include <comdef.h>
#include <wrl/client.h>
#include <string>
#include <vector>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "ole32.lib")

#define VK_Z_KEY 0x5A
// These keys are used to send windows to tray
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY MOD_WIN + MOD_SHIFT

#define WM_ICON     0x1C0A
#define WM_OURICON  0x1C0B
#define EXIT_ID     0x99
#define SHOW_ALL_ID 0x98
#define AUTOSTART_ID 0x97
#define MAXIMUM_WINDOWS 100

static const wchar_t TASK_NAME[] = L"Traymond";

// Stores hidden window record.
typedef struct HIDDEN_WINDOW {
  NOTIFYICONDATA icon;
  HWND window;
  LONG_PTR savedExStyle; // original extended style, restored on un-hide
} HIDDEN_WINDOW;

// Current execution context
typedef struct TRCONTEXT {
  HWND mainWindow;
  HIDDEN_WINDOW icons[MAXIMUM_WINDOWS];
  HMENU trayMenu;
  int iconIndex;         // How many windows are currently hidden
  UINT nextIconId;       // Monotonically increasing tray icon ID (starts at 2)
  NOTIFYICONDATA ownIcon; // Traymond's own tray icon; re-added on TaskbarCreated
} TRCONTEXT;

HANDLE saveFile;
char   g_datFilePath[MAX_PATH];  // Absolute path to traymond.dat
UINT   g_taskbarCreatedMsg;      // RegisterWindowMessage("TaskbarCreated")

// ---------------------------------------------------------------------------
// Task Scheduler helpers
// ---------------------------------------------------------------------------

static bool isAutoStartEnabled() {
  using Microsoft::WRL::ComPtr;
  ComPtr<ITaskService> svc;
  if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&svc))))
    return false;
  if (FAILED(svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t())))
    return false;
  ComPtr<ITaskFolder> folder;
  if (FAILED(svc->GetFolder(_bstr_t(L"\\"), &folder)))
    return false;
  ComPtr<IRegisteredTask> task;
  return SUCCEEDED(folder->GetTask(_bstr_t(TASK_NAME), &task));
}

static bool disableAutoStart() {
  using Microsoft::WRL::ComPtr;
  ComPtr<ITaskService> svc;
  if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&svc))))
    return false;
  if (FAILED(svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t())))
    return false;
  ComPtr<ITaskFolder> folder;
  if (FAILED(svc->GetFolder(_bstr_t(L"\\"), &folder)))
    return false;
  return SUCCEEDED(folder->DeleteTask(_bstr_t(TASK_NAME), 0));
}

static bool enableAutoStart() {
  using Microsoft::WRL::ComPtr;

  wchar_t exePath[MAX_PATH], exeDir[MAX_PATH];
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  wcscpy_s(exeDir, exePath);
  wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
  if (lastSlash) *lastSlash = L'\0';

  ComPtr<ITaskService> svc;
  if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&svc))))
    return false;
  if (FAILED(svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t())))
    return false;

  ComPtr<ITaskFolder> folder;
  if (FAILED(svc->GetFolder(_bstr_t(L"\\"), &folder)))
    return false;

  ComPtr<ITaskDefinition> def;
  if (FAILED(svc->NewTask(0, &def)))
    return false;

  // Author metadata
  ComPtr<IRegistrationInfo> regInfo;
  if (SUCCEEDED(def->get_RegistrationInfo(&regInfo)))
    regInfo->put_Author(_bstr_t(L"Traymond"));

  // Settings: no time limit, run even on battery
  ComPtr<ITaskSettings> settings;
  if (SUCCEEDED(def->get_Settings(&settings))) {
    settings->put_StartWhenAvailable(VARIANT_TRUE);
    settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
  }

  // Logon trigger with 30s delay so Shell_TrayWnd is ready when Traymond starts
  ComPtr<ITriggerCollection> triggers;
  if (FAILED(def->get_Triggers(&triggers))) return false;
  ComPtr<ITrigger> trigger;
  if (FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger))) return false;
  ComPtr<ILogonTrigger> logon;
  if (SUCCEEDED(trigger->QueryInterface(IID_PPV_ARGS(&logon))))
    logon->put_Delay(_bstr_t(L"PT30S"));

  // Exec action pointing at this exe, with working dir set to its directory
  // so traymond.dat is always written next to the exe regardless of CWD.
  ComPtr<IActionCollection> actions;
  if (FAILED(def->get_Actions(&actions))) return false;
  ComPtr<IAction> action;
  if (FAILED(actions->Create(TASK_ACTION_EXEC, &action))) return false;
  ComPtr<IExecAction> exec;
  if (FAILED(action->QueryInterface(IID_PPV_ARGS(&exec)))) return false;
  exec->put_Path(_bstr_t(exePath));
  exec->put_WorkingDirectory(_bstr_t(exeDir));

  // Run as the current interactive user with highest privileges (required for
  // requireAdministrator manifest; TASK_RUNLEVEL_HIGHEST = no UAC prompt at boot)
  ComPtr<IPrincipal> principal;
  if (FAILED(def->get_Principal(&principal))) return false;
  principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
  principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);

  ComPtr<IRegisteredTask> registered;
  return SUCCEEDED(folder->RegisterTaskDefinition(
    _bstr_t(TASK_NAME), def.Get(),
    TASK_CREATE_OR_UPDATE,
    _variant_t(), _variant_t(),
    TASK_LOGON_INTERACTIVE_TOKEN,
    _variant_t(L""),
    &registered));
}

// ---------------------------------------------------------------------------
// Core tray logic
// ---------------------------------------------------------------------------

// Saves our hidden windows so they can be restored in case of crashing.
void save(const TRCONTEXT *context) {
  DWORD numbytes;
  SetFilePointer(saveFile, 0, NULL, FILE_BEGIN);
  SetEndOfFile(saveFile);
  if (!context->iconIndex) return;
  for (int i = 0; i < context->iconIndex; i++) {
    if (context->icons[i].window) {
      std::string str = std::to_string((long)context->icons[i].window) + ',';
      WriteFile(saveFile, str.c_str(), (DWORD)str.size(), &numbytes, NULL);
    }
  }
}

// Restores a single hidden window.
void showWindow(TRCONTEXT *context, LPARAM lParam) {
  for (int i = 0; i < context->iconIndex; i++) {
    if (context->icons[i].icon.uID == HIWORD(lParam)) {
      if (context->icons[i].savedExStyle)
        SetWindowLongPtr(context->icons[i].window, GWL_EXSTYLE,
          context->icons[i].savedExStyle);
      ShowWindow(context->icons[i].window, SW_SHOW);
      Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
      SetForegroundWindow(context->icons[i].window);
      context->icons[i] = {};
      std::vector<HIDDEN_WINDOW> temp(context->iconIndex);
      for (int j = 0, x = 0; j < context->iconIndex; j++) {
        if (context->icons[j].window) temp[x++] = context->icons[j];
      }
      memcpy_s(context->icons, sizeof(context->icons),
        temp.data(), sizeof(HIDDEN_WINDOW) * context->iconIndex);
      context->iconIndex--;
      save(context);
      break;
    }
  }
}

// Minimizes the current window to tray.
// Uses currently focused window unless supplied a handle as the argument.
void minimizeToTray(TRCONTEXT *context, long restoreWindow) {
  // Taskbar, desktop, and invisible Chromium root/system windows are restricted.
  const char* restrictWins[] = {
    "WorkerW", "Shell_TrayWnd", "Chrome_WidgetWin_0",
    "Windows.UI.Core.CoreWindow", "Progman", "MSCTFIME UI", "Default IME"
  };

  HWND currWin = 0;
  if (!restoreWindow) {
    currWin = GetForegroundWindow();
    // Walk up to the true root; skips IME/PiP/overlay sub-windows in Chromium.
    if (currWin) currWin = GetAncestor(currWin, GA_ROOT);
  } else {
    currWin = reinterpret_cast<HWND>(restoreWindow);
  }

  if (!currWin || !IsWindowVisible(currWin)) return;
  if (GetWindowLongPtr(currWin, GWL_STYLE) & WS_CHILD) return;

  char className[256];
  if (!GetClassName(currWin, className, 256)) return;
  for (int i = 0; i < (int)(sizeof(restrictWins) / sizeof(*restrictWins)); i++) {
    if (strcmp(restrictWins[i], className) == 0) return;
  }

  if (context->iconIndex == MAXIMUM_WINDOWS) {
    MessageBox(NULL, "Error! Too many hidden windows. Please unhide some.",
      "Traymond", MB_OK | MB_ICONERROR);
    return;
  }

  // UWP / Microsoft Store apps use ApplicationFrameWindow as the outer shell;
  // the real app icon lives on the inner Windows.UI.Core.CoreWindow child.
  ULONG_PTR icon = 0;
  if (strcmp(className, "ApplicationFrameWindow") == 0) {
    HWND coreWin = FindWindowEx(currWin, NULL, "Windows.UI.Core.CoreWindow", NULL);
    if (coreWin) {
      icon = SendMessage(coreWin, WM_GETICON, 1, NULL); // ICON_BIG
      if (!icon) icon = SendMessage(coreWin, WM_GETICON, 0, NULL); // ICON_SMALL
    }
  }
  if (!icon) icon = GetClassLongPtr(currWin, GCLP_HICONSM);
  if (!icon) icon = SendMessage(currWin, WM_GETICON, 2, NULL); // ICON_SMALL2
  if (!icon) icon = SendMessage(currWin, WM_GETICON, 0, NULL); // ICON_SMALL
  if (!icon) icon = SendMessage(currWin, WM_GETICON, 1, NULL); // ICON_BIG
  if (!icon) icon = GetClassLongPtr(currWin, GCLP_HICON);
  if (!icon) icon = (ULONG_PTR)LoadIcon(NULL, IDI_APPLICATION);
  if (!icon) return;

  // Monotonic ID avoids LOWORD(HWND) collisions that silently fail NIM_ADD.
  if (context->nextIconId >= 0xFFFF) context->nextIconId = 1;
  UINT iconId = ++context->nextIconId;

  NOTIFYICONDATA nid = {};
  nid.cbSize = sizeof(NOTIFYICONDATA);
  nid.hWnd = context->mainWindow;
  nid.hIcon = (HICON)icon;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uVersion = NOTIFYICON_VERSION_4;
  nid.uID = iconId;
  nid.uCallbackMessage = WM_ICON;
  GetWindowText(currWin, nid.szTip, 128);

  if (!Shell_NotifyIcon(NIM_ADD, &nid)) return;
  Shell_NotifyIcon(NIM_SETVERSION, &nid);

  LONG_PTR savedExStyle = GetWindowLongPtr(currWin, GWL_EXSTYLE);
  ShowWindow(currWin, SW_HIDE);

  // Detect UIPI rejection (target runs at higher integrity level).
  if (IsWindowVisible(currWin)) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
    static bool shownUipiWarning = false;
    if (!shownUipiWarning) {
      shownUipiWarning = true;
      MessageBox(NULL,
        "Could not hide window.\n\nIf the target application runs as administrator,"
        " Traymond must also run as administrator.",
        "Traymond", MB_OK | MB_ICONWARNING);
    }
    return;
  }

  // Strip WS_EX_APPWINDOW / add WS_EX_TOOLWINDOW so apps that call show()
  // themselves (e.g. Electron 'hide' event handlers) stay off the taskbar.
  SetWindowLongPtr(currWin, GWL_EXSTYLE,
    (savedExStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);

  context->icons[context->iconIndex].icon = nid;
  context->icons[context->iconIndex].window = currWin;
  context->icons[context->iconIndex].savedExStyle = savedExStyle;
  context->iconIndex++;

  if (!restoreWindow) save(context);
}

// Adds Traymond's own icon to the tray.
void createTrayIcon(HWND mainWindow, HINSTANCE hInstance, TRCONTEXT* context) {
  NOTIFYICONDATA& icon = context->ownIcon;
  icon.cbSize = sizeof(NOTIFYICONDATA);
  icon.hWnd = mainWindow;
  icon.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
  icon.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
  icon.uVersion = NOTIFYICON_VERSION_4;
  icon.uID = 1; // Reserved; hidden-window IDs start at 2
  icon.uCallbackMessage = WM_OURICON;
  strcpy_s(icon.szTip, "Traymond");
  Shell_NotifyIcon(NIM_ADD, &icon);
  Shell_NotifyIcon(NIM_SETVERSION, &icon);
}

// Creates the tray icon right-click menu.
void createTrayMenu(HMENU* trayMenu) {
  *trayMenu = CreatePopupMenu();
  AppendMenu(*trayMenu, MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED),
    AUTOSTART_ID, "Auto-start at login");
  AppendMenu(*trayMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenu(*trayMenu, MF_STRING, SHOW_ALL_ID, "Restore all windows");
  AppendMenu(*trayMenu, MF_STRING, EXIT_ID, "Exit");
}

// Shows all hidden windows.
void showAllWindows(TRCONTEXT *context) {
  for (int i = 0; i < context->iconIndex; i++) {
    if (context->icons[i].savedExStyle)
      SetWindowLongPtr(context->icons[i].window, GWL_EXSTYLE,
        context->icons[i].savedExStyle);
    ShowWindow(context->icons[i].window, SW_SHOW);
    Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
    context->icons[i] = {};
  }
  save(context);
  context->iconIndex = 0;
}

void exitApp() {
  PostQuitMessage(0);
}

// Creates and reads the save file to restore hidden windows in case of unexpected termination.
void startup(TRCONTEXT *context) {
  if ((saveFile = CreateFile(g_datFilePath, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
    MessageBox(NULL, "Error! Traymond could not create a save file.", "Traymond",
      MB_OK | MB_ICONERROR);
    exitApp();
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    DWORD numbytes;
    DWORD fileSize = GetFileSize(saveFile, NULL);
    if (!fileSize) return;

    FILETIME saveFileWriteTime;
    GetFileTime(saveFile, NULL, NULL, &saveFileWriteTime);
    uint64_t writeTime = ((uint64_t)saveFileWriteTime.dwHighDateTime << 32 |
      (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000;
    GetSystemTimeAsFileTime(&saveFileWriteTime);
    writeTime = (((uint64_t)saveFileWriteTime.dwHighDateTime << 32 |
      (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000) - writeTime;
    if (GetTickCount64() < writeTime) return;

    std::vector<char> contents(fileSize);
    ReadFile(saveFile, contents.data(), fileSize, &numbytes, NULL);
    char handle[10] = {};
    int index = 0;
    for (size_t i = 0; i < fileSize; i++) {
      if (contents[i] != ',') {
        handle[index++] = contents[i];
      } else {
        index = 0;
        minimizeToTray(context, std::stoi(std::string(handle)));
        memset(handle, 0, sizeof(handle));
      }
    }
    std::string msg = "Traymond had previously been terminated unexpectedly.\n\nRestored " +
      std::to_string(context->iconIndex) +
      (context->iconIndex > 1 ? " icons." : " icon.");
    MessageBox(NULL, msg.c_str(), "Traymond", MB_OK);
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  TRCONTEXT* context = reinterpret_cast<TRCONTEXT*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  // Re-add the tray icon when the taskbar is recreated (explorer crash/restart).
  if (uMsg == g_taskbarCreatedMsg && context) {
    Shell_NotifyIcon(NIM_ADD, &context->ownIcon);
    Shell_NotifyIcon(NIM_SETVERSION, &context->ownIcon);
    return 0;
  }

  POINT pt;
  switch (uMsg) {
  case WM_ICON:
    if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
      showWindow(context, lParam);
    break;
  case WM_OURICON:
    if (LOWORD(lParam) == WM_RBUTTONUP) {
      // Sync checkmark with actual task scheduler state before showing.
      CheckMenuItem(context->trayMenu, AUTOSTART_ID,
        MF_BYCOMMAND | (isAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED));
      SetForegroundWindow(hwnd);
      GetCursorPos(&pt);
      TrackPopupMenuEx(context->trayMenu,
        (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN,
        pt.x, pt.y, hwnd, NULL);
    }
    break;
  case WM_COMMAND:
    if (HIWORD(wParam) == 0) {
      switch (LOWORD(wParam)) {
      case SHOW_ALL_ID:
        showAllWindows(context);
        break;
      case EXIT_ID:
        exitApp();
        break;
      case AUTOSTART_ID:
        if (isAutoStartEnabled()) {
          if (!disableAutoStart())
            MessageBox(NULL, "Failed to disable auto-start.", "Traymond",
              MB_OK | MB_ICONERROR);
        } else {
          if (!enableAutoStart())
            MessageBox(NULL, "Failed to enable auto-start.", "Traymond",
              MB_OK | MB_ICONERROR);
        }
        break;
      }
    }
    break;
  case WM_HOTKEY:
    minimizeToTray(context, NULL);
    break;
  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

#pragma warning(push)
#pragma warning(disable : 4100)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
#pragma warning(pop)

  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

  // Build absolute path for the save file so it's always next to the exe,
  // regardless of the current working directory (critical for Task Scheduler launch).
  GetModuleFileNameA(NULL, g_datFilePath, MAX_PATH);
  char* lastSlash = strrchr(g_datFilePath, '\\');
  if (lastSlash) strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - g_datFilePath), "traymond.dat");
  else strcpy_s(g_datFilePath, "traymond.dat");

  g_taskbarCreatedMsg = RegisterWindowMessage(TEXT("TaskbarCreated"));

  TRCONTEXT context = {};
  context.nextIconId = 1; // 1 is reserved for Traymond's own icon; hidden windows start at 2

  // Mutex to allow only one instance
  const char szUniqueNamedMutex[] = "traymond_mutex";
  HANDLE mutex = CreateMutex(NULL, TRUE, szUniqueNamedMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBox(NULL, "Error! Another instance of Traymond is already running.",
      "Traymond", MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  BOOL bRet;
  MSG msg;

  const char CLASS_NAME[] = "Traymond";
  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  if (!RegisterClass(&wc)) { CoUninitialize(); return 1; }

  context.mainWindow = CreateWindow(CLASS_NAME, NULL, NULL, 0, 0, 0, 0,
    HWND_MESSAGE, NULL, hInstance, NULL);
  if (!context.mainWindow) { CoUninitialize(); return 1; }

  SetWindowLongPtr(context.mainWindow, GWLP_USERDATA, reinterpret_cast<LONG>(&context));

  if (!RegisterHotKey(context.mainWindow, 0, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
    MessageBox(NULL, "Error! Could not register the hotkey.", "Traymond",
      MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  createTrayIcon(context.mainWindow, hInstance, &context);
  createTrayMenu(&context.trayMenu);
  startup(&context);

  while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0) {
    if (bRet != -1) DispatchMessage(&msg);
  }

  showAllWindows(&context);
  Shell_NotifyIcon(NIM_DELETE, &context.ownIcon);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  CloseHandle(saveFile);
  DestroyMenu(context.trayMenu);
  DestroyWindow(context.mainWindow);
  DeleteFile(g_datFilePath);
  UnregisterHotKey(context.mainWindow, 0);
  CoUninitialize();
  return msg.wParam;
}
