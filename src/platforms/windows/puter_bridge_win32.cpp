#include "WebView2.h"
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <shlobj.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <windows.h>

// Convert a wide string to a UTF-8 std::string using the Win32 API.
// This sidesteps wofstream's locale-dependent codecvt entirely.
static std::string WideToUtf8(const std::wstring &wide) {
  if (wide.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                 nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                      &result[0], size, nullptr, nullptr);
  return result;
}

// Signature for the Edge WebView2 loader function
typedef HRESULT(STDAPICALLTYPE *CreateCoreWebView2EnvironmentWithOptionsFn)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
        *environmentCreatedHandler);

static ICoreWebView2 *webviewWindow = nullptr;
static ICoreWebView2Controller *webviewController = nullptr;
static std::wstring requestJsonData;
static bool operationCompleted = false;

#include <filesystem>

// Global debugging logger
inline void LogDebug(const std::wstring &msg) {
  std::ofstream logFile("puter_debug.txt", std::ios::app);
  if (logFile.is_open()) {
    logFile << WideToUtf8(msg) << "\n";
  }
}


// Write final output and exit
void SaveResponseAndExit(const std::wstring &status,
                         const std::wstring &responseText) {
  LogDebug(L"SaveResponseAndExit: status=" + status + L" response=" +
           responseText);

  // Write puter_response.json
  std::ofstream responseFile("puter_response.json");
  if (responseFile.is_open()) {
    std::wstring escapedResponse = L"";
    for (wchar_t c : responseText) {
      if (c == L'\\')
        escapedResponse += L"\\\\";
      else if (c == L'"')
        escapedResponse += L"\\\"";
      else if (c == L'\n')
        escapedResponse += L"\\n";
      else if (c == L'\r')
        escapedResponse += L"\\r";
      else if (c == L'\t')
        escapedResponse += L"\\t";
      else if (c == L'\b')
        escapedResponse += L"\\b";
      else if (c == L'\f')
        escapedResponse += L"\\f";
      else if (c < 32) {
        wchar_t buf[8];
        swprintf_s(buf, L"\\u%04x", c);
        escapedResponse += buf;
      }
      else
        escapedResponse += c;
    }

    responseFile << "{\n";
    responseFile << "  \"status\": \"" << WideToUtf8(status) << "\",\n";
    responseFile << "  \"response\": \"" << WideToUtf8(escapedResponse) << "\"\n";
    responseFile << "}\n";
    responseFile.close();
  }
  operationCompleted = true;
  PostQuitMessage(0);
}

// Helper to robustly unescape standard JSON escape sequences in a wstring
inline std::wstring UnescapeJsonString(const std::wstring &input) {
  std::wstring result = L"";
  size_t i = 0;
  while (i < input.length()) {
    if (input[i] == L'\\' && i + 1 < input.length()) {
      wchar_t next = input[i + 1];
      if (next == L'\\') {
        result += L'\\';
        i += 2;
      } else if (next == L'"') {
        result += L'"';
        i += 2;
      } else if (next == L'n') {
        result += L'\n';
        i += 2;
      } else if (next == L'r') {
        result += L'\r';
        i += 2;
      } else if (next == L't') {
        result += L'\t';
        i += 2;
      } else if (next == L'/') {
        result += L'/';
        i += 2;
      } else if (next == L'b') {
        result += L'\b';
        i += 2;
      } else if (next == L'f') {
        result += L'\f';
        i += 2;
      } else if (next == L'u' && i + 5 < input.length()) {
        std::wstring hexStr = input.substr(i + 2, 4);
        wchar_t val = (wchar_t)std::wcstoul(hexStr.c_str(), nullptr, 16);
        result += val;
        i += 6;
      } else {
        result += input[i];
        i++;
      }
    } else {
      result += input[i];
      i++;
    }
  }
  return result;
}

class WebMessageReceivedHandler
    : public ICoreWebView2WebMessageReceivedEventHandler {
private:
  HWND m_hWnd;
  ULONG m_refCount = 1;

public:
  WebMessageReceivedHandler(HWND hWnd) : m_hWnd(hWnd) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
      *ppvObject = this;
      AddRef();
      return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&m_refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
      delete this;
      return 0;
    }
    return count;
  }

  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *sender,
         ICoreWebView2WebMessageReceivedEventArgs *args) override {
    LPWSTR jsonStr = nullptr;
    HRESULT hr = args->get_WebMessageAsJson(&jsonStr);
    if (SUCCEEDED(hr) && jsonStr != nullptr) {
      std::wstring msg(jsonStr);
      CoTaskMemFree(jsonStr);
      LogDebug(L"WebMessageReceived: " + msg);

      // Check for ready action to send payload (immediate invisible execution)
      if (msg.find(L"\"action\":\"ready_invisible\"") != std::wstring::npos) {
        if (webviewWindow != nullptr) {
          webviewWindow->PostWebMessageAsJson(requestJsonData.c_str());
        }
      }
      // Check for login window trigger (user needs to sign in)
      else if (msg.find(L"\"action\":\"show_login_window\"") !=
               std::wstring::npos) {
        // Show the pre-created overlapped window.
        // SW_RESTORE is intentional: it overrides any residual SW_HIDE startup
        // flag that Windows may have cached from the parent process's
        // STARTUPINFO.
        if (webviewController != nullptr) {
          webviewController->put_IsVisible(TRUE);
        }
        ShowWindow(m_hWnd, SW_RESTORE);
        UpdateWindow(m_hWnd);
        // Bring to front even if the game window is covering it
        SetForegroundWindow(m_hWnd);
        BringWindowToTop(m_hWnd);
        SetFocus(m_hWnd);
        // No timeout when waiting for login — the user can take as long as
        // needed.
        KillTimer(m_hWnd, 1);
      }
      // Check for successful login
      else if (msg.find(L"\"action\":\"login_success\"") !=
               std::wstring::npos) {
        // Login complete, hide the window and post payload
        ShowWindow(m_hWnd, SW_HIDE);
        if (webviewWindow != nullptr) {
          webviewWindow->PostWebMessageAsJson(requestJsonData.c_str());
        }
      }
      // Check for success response
      else if (msg.find(L"\"action\":\"response\"") != std::wstring::npos) {
        size_t txtTag = msg.find(L"\"text\":\"");
        if (txtTag != std::wstring::npos) {
          size_t start = txtTag + 8;
          size_t end = std::wstring::npos;
          size_t backslashes = 0;
          for (size_t i = start; i < msg.length(); ++i) {
            if (msg[i] == L'\\') {
              backslashes++;
            } else if (msg[i] == L'"') {
              if (backslashes % 2 == 0) {
                end = i;
                break;
              }
              backslashes = 0;
            } else {
              backslashes = 0;
            }
          }
          if (end != std::wstring::npos && end > start) {
            std::wstring escapedText = msg.substr(start, end - start);
            std::wstring wText = UnescapeJsonString(escapedText);
            SaveResponseAndExit(L"success", wText);
          } else {
            SaveResponseAndExit(L"success", L"");
          }
        } else {
          SaveResponseAndExit(L"success", L"");
        }
      }
      // Check for error response
      else if (msg.find(L"\"action\":\"error\"") != std::wstring::npos) {
        size_t txtTag = msg.find(L"\"text\":\"");
        std::wstring errText = L"Unknown Puter AI error";
        if (txtTag != std::wstring::npos) {
          size_t start = txtTag + 8;
          size_t end = std::wstring::npos;
          size_t backslashes = 0;
          for (size_t i = start; i < msg.length(); ++i) {
            if (msg[i] == L'\\') {
              backslashes++;
            } else if (msg[i] == L'"') {
              if (backslashes % 2 == 0) {
                end = i;
                break;
              }
              backslashes = 0;
            } else {
              backslashes = 0;
            }
          }
          if (end != std::wstring::npos && end > start) {
            std::wstring escapedText = msg.substr(start, end - start);
            errText = UnescapeJsonString(escapedText);
          }
        }
        SaveResponseAndExit(L"error", errText);
      }
    }
    return S_OK;
  }
};

class ControllerCompletedHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
private:
  HWND m_hWnd;
  ULONG m_refCount = 1;

public:
  ControllerCompletedHandler(HWND hWnd) : m_hWnd(hWnd) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
      *ppvObject = this;
      AddRef();
      return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&m_refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
      delete this;
      return 0;
    }
    return count;
  }

  HRESULT STDMETHODCALLTYPE
  Invoke(HRESULT result, ICoreWebView2Controller *controller) override {
    if (FAILED(result) || controller == nullptr) {
      SaveResponseAndExit(L"error", L"Failed to create WebView2 controller");
      return result;
    }

    webviewController = controller;
    webviewController->AddRef();

    webviewController->get_CoreWebView2(&webviewWindow);
    if (webviewWindow != nullptr) {
      webviewWindow->AddRef();

      ICoreWebView2Settings *settings = nullptr;
      webviewWindow->get_Settings(&settings);
      if (settings != nullptr) {
        settings->put_IsScriptEnabled(TRUE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->Release();
      }

      RECT bounds;
      GetClientRect(m_hWnd, &bounds);
      webviewController->put_Bounds(bounds);

      wchar_t exePath[MAX_PATH];
      GetModuleFileNameW(nullptr, exePath, MAX_PATH);
      std::wstring exeDir = exePath;
      size_t pos = exeDir.find_last_of(L"\\/");
      if (pos != std::wstring::npos) {
        exeDir = exeDir.substr(0, pos);
      }

      EventRegistrationToken token;
      WebMessageReceivedHandler *msgHandler =
          new WebMessageReceivedHandler(m_hWnd);
      webviewWindow->add_WebMessageReceived(msgHandler, &token);
      msgHandler->Release();

      ICoreWebView2_3 *webviewWindow3 = nullptr;
      HRESULT hr = webviewWindow->QueryInterface(
          IID_ICoreWebView2_3, reinterpret_cast<void **>(&webviewWindow3));
      if (SUCCEEDED(hr) && webviewWindow3 != nullptr) {
        webviewWindow3->SetVirtualHostNameToFolderMapping(
            L"app.local", exeDir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        webviewWindow3->Release();
        webviewWindow->Navigate(L"https://app.local/assets/puter_bridge.html");
      } else {
        std::wstring htmlPath =
            L"file:///" + exeDir + L"/assets/puter_bridge.html";
        for (size_t i = 8; i < htmlPath.length(); ++i) {
          if (htmlPath[i] == L'\\') {
            htmlPath[i] = L'/';
          }
        }
        webviewWindow->Navigate(htmlPath.c_str());
      }
    } else {
      SaveResponseAndExit(L"error", L"Failed to obtain CoreWebView2 instance");
    }
    return S_OK;
  }
};

class EnvironmentCompletedHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
private:
  HWND m_hWnd;
  ULONG m_refCount = 1;

public:
  EnvironmentCompletedHandler(HWND hWnd) : m_hWnd(hWnd) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid ==
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
      *ppvObject = this;
      AddRef();
      return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&m_refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
      delete this;
      return 0;
    }
    return count;
  }

  HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                   ICoreWebView2Environment *env) override {
    if (FAILED(result) || env == nullptr) {
      SaveResponseAndExit(L"error",
                          L"Failed to initialize WebView2 environment");
      return result;
    }

    ControllerCompletedHandler *controllerHandler =
        new ControllerCompletedHandler(m_hWnd);
    env->CreateCoreWebView2Controller(m_hWnd, controllerHandler);
    controllerHandler->Release();
    return S_OK;
  }
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  switch (message) {
  case WM_SIZE:
    if (webviewController != nullptr) {
      RECT bounds;
      GetClientRect(hWnd, &bounds);
      webviewController->put_Bounds(bounds);
    }
    break;
  case WM_DESTROY:
    if (webviewWindow != nullptr) {
      webviewWindow->Release();
      webviewWindow = nullptr;
    }
    if (webviewController != nullptr) {
      webviewController->Release();
      webviewController = nullptr;
    }
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
  std::error_code ec;
  std::filesystem::remove("puter_debug.txt", ec);
  LogDebug(L"PuterBridge: WinMain started.");

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  // Read the puter_request.json file
  std::ifstream file("puter_request.json");
  if (!file.is_open()) {
    SaveResponseAndExit(L"error",
                        L"Failed to open puter_request.json input file");
    CoUninitialize();
    return 0;
  }

  std::string requestStr((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  file.close();

  requestJsonData = std::wstring(requestStr.begin(), requestStr.end());

  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = nullptr;
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = _T("PuterBridgeWebView");
  wcex.hIconSm = nullptr;

  RegisterClassEx(&wcex);

  // Calculate centered coordinates for a nice 600x700 popup window if login is
  // required
  int screenW = GetSystemMetrics(SM_CXSCREEN);
  int screenH = GetSystemMetrics(SM_CYSCREEN);
  int winW = 600;
  int winH = 700;
  int winX = (screenW - winW) / 2;
  int winY = (screenH - winH) / 2;

  // Create window INVISIBLY as a standard overlapped window from the start
  HWND hWnd = CreateWindow(_T("PuterBridgeWebView"),
                           _T("Puter AI Authentication"), WS_OVERLAPPEDWINDOW,
                           winX, winY, winW, winH, NULL, NULL, hInstance, NULL);

  if (!hWnd) {
    SaveResponseAndExit(L"error",
                        L"Failed to create overlapped Windows host window");
    CoUninitialize();
    return 0;
  }

  // Consume the STARTF_USESHOWWINDOW flag that the parent process may have set.
  // Windows applies this flag only to the *first* ShowWindow call for any
  // window in the process. If the parent launched us with SW_HIDE in
  // STARTUPINFO and we never call ShowWindow here, the first call in the login
  // handler will be silently overridden to SW_HIDE — making the login window
  // invisible. Calling SW_HIDE here burns the flag so that subsequent
  // ShowWindow(SW_RESTORE) calls work normally.
  ShowWindow(hWnd, SW_HIDE);
  UpdateWindow(hWnd);

  // Set a fallback timeout timer (e.g. 180 seconds) in case network is down or
  // API hangs
  SetTimer(hWnd, 1, 180000, [](HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    if (!operationCompleted) {
      SaveResponseAndExit(L"error",
                          L"Puter bridge request timed out after 180 seconds.");
    }
  });

  EnvironmentCompletedHandler *envHandler =
      new EnvironmentCompletedHandler(hWnd);

  // Resolve a shared persistent UserDataFolder in
  // %LOCALAPPDATA%\PuterBridge\WebView2 This ensures that Puter login sessions
  // survive across different build directories (build/, build_native/, etc.) so
  // the user only has to log in once.
  std::wstring userDataFolder;
  {
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL,
                                   SHGFP_TYPE_CURRENT, localAppData))) {
      userDataFolder = std::wstring(localAppData) + L"\\PuterBridge\\WebView2";
      // Ensure the directory exists
      std::wstring parentDir = std::wstring(localAppData) + L"\\PuterBridge";
      CreateDirectoryW(parentDir.c_str(), NULL);
      CreateDirectoryW(userDataFolder.c_str(), NULL);
    }
  }
  LogDebug(L"UserDataFolder: " +
           (userDataFolder.empty() ? L"(default)" : userDataFolder));

  HMODULE hLoader = LoadLibrary(_T("WebView2Loader.dll"));
  if (hLoader != nullptr) {
    CreateCoreWebView2EnvironmentWithOptionsFn pCreateEnv =
        (CreateCoreWebView2EnvironmentWithOptionsFn)GetProcAddress(
            hLoader, "CreateCoreWebView2EnvironmentWithOptions");
    if (pCreateEnv != nullptr) {
      pCreateEnv(nullptr,
                 userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
                 nullptr, envHandler);
    } else {
      SaveResponseAndExit(
          L"error", L"Failed to locate entry point in WebView2Loader.dll");
    }
  } else {
    SaveResponseAndExit(
        L"error", L"WebView2Loader.dll not found in the executable directory");
  }

  envHandler->Release();

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  if (hLoader != nullptr) {
    FreeLibrary(hLoader);
  }

  CoUninitialize();
  return (int)msg.wParam;
}
