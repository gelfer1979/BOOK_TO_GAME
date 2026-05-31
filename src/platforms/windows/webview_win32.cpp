#include <windows.h>
#include <objbase.h>
#include <string>
#include <tchar.h>
#include <fstream>
#include "WebView2.h"

// Signature for the Edge WebView2 loader function
typedef HRESULT (STDAPICALLTYPE *CreateCoreWebView2EnvironmentWithOptionsFn)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler
);

#include <vector>

static std::string Base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    }
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Global WebView references using raw COM pointers
static ICoreWebView2* webviewWindow = nullptr;
static ICoreWebView2Controller* webviewController = nullptr;

// Custom COM event handler to receive file write requests from the sandboxed WebAssembly client
class WebMessageReceivedHandler : public ICoreWebView2WebMessageReceivedEventHandler {
private:
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
            return 0;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR jsonStr = nullptr;
        HRESULT hr = args->get_WebMessageAsJson(&jsonStr);
        if (SUCCEEDED(hr) && jsonStr != nullptr) {
            std::wstring msg(jsonStr);
            CoTaskMemFree(jsonStr);
            
            // Look for the "action":"write_file" payload
            if (msg.find(L"\"action\":\"write_file\"") != std::wstring::npos) {
                size_t fnTag = msg.find(L"\"filename\":\"");
                if (fnTag != std::wstring::npos) {
                    size_t fnStart = fnTag + 12;
                    size_t fnEnd = msg.find(L"\"", fnStart);
                    if (fnEnd != std::wstring::npos) {
                        std::wstring wFilename = msg.substr(fnStart, fnEnd - fnStart);
                        
                        size_t ctTag = msg.find(L"\"content\":\"");
                        if (ctTag != std::wstring::npos) {
                            size_t ctStart = ctTag + 11;
                            size_t ctEnd = msg.find(L"\"", ctStart);
                            if (ctEnd != std::wstring::npos) {
                                std::wstring wContent = msg.substr(ctStart, ctEnd - ctStart);
                                
                                std::string filename(wFilename.begin(), wFilename.end());
                                std::string base64Content(wContent.begin(), wContent.end());
                                
                                // Decode base64 payload to get the exact file contents safely
                                std::string content = Base64Decode(base64Content);
                                
                                std::ofstream file(filename, std::ios::binary);
                                if (file.is_open()) {
                                    file.write(content.c_str(), content.length());
                                    file.close();
                                }
                            }
                        }
                    }
                }
            }
        }
        return S_OK;
    }
};

// Standard COM implementation of ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
private:
    HWND m_hWnd;
    ULONG m_refCount = 1;

public:
    ControllerCompletedHandler(HWND hWnd) : m_hWnd(hWnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
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

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (FAILED(result) || controller == nullptr) return result;

        webviewController = controller;
        webviewController->AddRef(); // Keep reference
        
        webviewController->get_CoreWebView2(&webviewWindow);
        if (webviewWindow != nullptr) {
            webviewWindow->AddRef(); // Keep reference

            // Configure settings: Allow scripts, local DOM access, sessions
            ICoreWebView2Settings* settings = nullptr;
            webviewWindow->get_Settings(&settings);
            if (settings != nullptr) {
                settings->put_IsScriptEnabled(TRUE);
                settings->put_AreDefaultContextMenusEnabled(TRUE);
                settings->Release();
            }

            // Configure window boundaries
            RECT bounds;
            GetClientRect(m_hWnd, &bounds);
            webviewController->put_Bounds(bounds);

            // Get absolute path of the executable to load the local WebAssembly game client securely
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring exeDir = exePath;
            size_t pos = exeDir.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                exeDir = exeDir.substr(0, pos);
            }
            EventRegistrationToken token;
            WebMessageReceivedHandler* msgHandler = new WebMessageReceivedHandler();
            webviewWindow->add_WebMessageReceived(msgHandler, &token);
            msgHandler->Release();

            ICoreWebView2_3* webviewWindow3 = nullptr;
            HRESULT hr = webviewWindow->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(&webviewWindow3));
            if (SUCCEEDED(hr) && webviewWindow3 != nullptr) {
                webviewWindow3->SetVirtualHostNameToFolderMapping(
                    L"app.local",
                    exeDir.c_str(),
                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                );
                webviewWindow3->Release();
                webviewWindow->Navigate(L"https://app.local/BOOK_TO_GAME.html");
            } else {
                std::wstring htmlPath = L"file:///" + exeDir + L"/BOOK_TO_GAME.html";
                // Replace backslashes with forward slashes for standard file URL format
                for (size_t i = 8; i < htmlPath.length(); ++i) {
                    if (htmlPath[i] == L'\\') {
                        htmlPath[i] = L'/';
                    }
                }
                webviewWindow->Navigate(htmlPath.c_str());
            }
        }
        return S_OK;
    }
};

// Standard COM implementation of ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
class EnvironmentCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
private:
    HWND m_hWnd;
    ULONG m_refCount = 1;

public:
    EnvironmentCompletedHandler(HWND hWnd) : m_hWnd(hWnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
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

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (FAILED(result) || env == nullptr) return result;

        ControllerCompletedHandler* controllerHandler = new ControllerCompletedHandler(m_hWnd);
        env->CreateCoreWebView2Controller(m_hWnd, controllerHandler);
        controllerHandler->Release();
        return S_OK;
    }
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
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

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize COM Apartment
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = _T("WebView2Win32Wrapper");
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindow(
        _T("WebView2Win32Wrapper"), _T("BOOK_TO_GAME Native Client (Windows)"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720, NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Create Environment completed handler
    EnvironmentCompletedHandler* envHandler = new EnvironmentCompletedHandler(hWnd);

    // Dynamically load Microsoft's WebView2Loader.dll at runtime to bypass linker dependencies under MinGW
    HMODULE hLoader = LoadLibrary(_T("WebView2Loader.dll"));
    if (hLoader != nullptr) {
        CreateCoreWebView2EnvironmentWithOptionsFn pCreateEnv = 
            (CreateCoreWebView2EnvironmentWithOptionsFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
        if (pCreateEnv != nullptr) {
            pCreateEnv(nullptr, nullptr, nullptr, envHandler);
        } else {
            MessageBox(hWnd, _T("Failed to locate CreateCoreWebView2EnvironmentWithOptions in WebView2Loader.dll"), _T("Error"), MB_ICONERROR);
        }
    } else {
        MessageBox(hWnd, _T("WebView2Loader.dll not found. Please ensure it is present in the application directory."), _T("Error"), MB_ICONERROR);
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
