// ============================================================
//  browser.cpp — встроенный браузер WebView2 (ОДНОПОТОЧНЫЙ)
//  Никаких очередей и потоков: всё в главном потоке + прокачка сообщений
// ============================================================
#include "browser.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#if defined(_WIN32) && __has_include("WebView2.h")
#define YAMO_WEBVIEW2 1
#endif

#ifdef YAMO_WEBVIEW2
#include <windows.h>
#include "WebView2.h"

#ifdef __MINGW32__
template<> const GUID& __mingw_uuidof<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>() { return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler; }
template<> const GUID& __mingw_uuidof<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>() { return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler; }
template<> const GUID& __mingw_uuidof<ICoreWebView2ExecuteScriptCompletedHandler>() { return IID_ICoreWebView2ExecuteScriptCompletedHandler; }
#endif

typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

static HMODULE g_dll = nullptr;
static CreateEnvFn g_createEnv = nullptr;
static HWND g_hwnd = nullptr;
static ICoreWebView2Controller* g_ctrl = nullptr;
static ICoreWebView2* g_web = nullptr;
static bool g_ready = false;
static bool g_failed = false;
static bool g_jsDone = false;
static std::string g_jsResult;

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// ---------- Прокачка сообщений Windows ----------
static void pumpFor(int ms) {
    MSG msg;
    DWORD start = GetTickCount();
    while ((DWORD)(GetTickCount() - start) < (DWORD)ms) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
}
static void pumpUntil(bool& flag, int timeoutMs) {
    MSG msg;
    DWORD start = GetTickCount();
    while (!flag && (DWORD)(GetTickCount() - start) < (DWORD)timeoutMs) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
}

// ---------- COM-обработчики ----------
class ScriptHandler : public ICoreWebView2ExecuteScriptCompletedHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ICoreWebView2ExecuteScriptCompletedHandler)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = --m_ref; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, LPCWSTR json) override {
        g_jsResult = (SUCCEEDED(hr) && json) ? toUtf8(json) : "";
        g_jsDone = true;
        return S_OK;
    }
};

class CtrlHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = --m_ref; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* c) override {
        if (SUCCEEDED(hr) && c) {
            g_ctrl = c; g_ctrl->AddRef();
            ICoreWebView2* w = nullptr;
            if (SUCCEEDED(g_ctrl->get_CoreWebView2(&w)) && w) {
                g_web = w;
                RECT r; GetClientRect(g_hwnd, &r);
                g_ctrl->put_Bounds(r);
            }
        } else { g_failed = true; }
        g_ready = true;
        return S_OK;
    }
};

class EnvHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = --m_ref; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (SUCCEEDED(hr) && env) env->CreateCoreWebView2Controller(g_hwnd, new CtrlHandler());
        else { g_failed = true; g_ready = true; }
        return S_OK;
    }
};

// ---------- Окно ----------
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE && g_ctrl) { RECT r; GetClientRect(h, &r); g_ctrl->put_Bounds(r); return 0; }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) {
        g_hwnd = nullptr;
        if (g_ctrl) { g_ctrl->Close(); g_ctrl->Release(); g_ctrl = nullptr; }
        if (g_web) { g_web->Release(); g_web = nullptr; }
        g_ready = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------- Инициализация (синхронная, с прокачкой) ----------
static void init() {
    if (g_ready || g_failed) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_dll = LoadLibraryW(L"WebView2Loader.dll");
    if (g_dll) g_createEnv = (CreateEnvFn)GetProcAddress(g_dll, "CreateCoreWebView2EnvironmentWithOptions");
    if (!g_createEnv) { g_failed = true; std::cerr << "Браузер: WebView2Loader.dll не найдена рядом с yamo.exe" << std::endl; return; }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"YamoBrowser";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, L"YamoBrowser", L"ЯМО Браузер", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hwnd) { g_failed = true; return; }

    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring userData = std::wstring(tmp) + L"yamo_webview2";
    HRESULT hr = g_createEnv(nullptr, userData.c_str(), nullptr, new EnvHandler());
    if (FAILED(hr)) { g_failed = true; return; }
    pumpUntil(g_ready, 20000);
    if (g_web) ShowWindow(g_hwnd, SW_SHOW);
    else { g_failed = true; std::cerr << "Браузер: не удалось создать WebView2 (установлен ли WebView2 Runtime?)" << std::endl; }
}

// ---------- Публичный API ----------
namespace yamo_browser {

void pump(int ms) { pumpFor(ms); }

bool openUrl(const std::string& url) {
    init();
    if (!g_web) return false;
    g_web->Navigate(toWide(url).c_str());
    pumpFor(50);
    return true;
}

bool openHtml(const std::string& html) {
    init();
    if (!g_web) return false;
    g_web->NavigateToString(toWide(html).c_str());
    pumpFor(50);
    return true;
}

std::string runJs(const std::string& js) {
    init();
    if (!g_web) return "";
    g_jsDone = false; g_jsResult.clear();
    HRESULT hr = g_web->ExecuteScript(toWide(js).c_str(), new ScriptHandler());
    if (FAILED(hr)) return "";
    pumpUntil(g_jsDone, 10000);
    return g_jsResult;
}

void closeWindow() {
    if (g_hwnd) DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
}

bool isAvailable() { return true; }

} // namespace yamo_browser

#else  // нет WebView2.h или не Windows — заглушки

namespace yamo_browser {
    static void warnOnce() {
        static bool warned = false;
        if (!warned) { warned = true; std::cerr << "Браузер: недоступен (нужны WebView2.h и WebView2Loader.dll)." << std::endl; }
    }
    void pump(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    bool openUrl(const std::string&) { warnOnce(); return false; }
    bool openHtml(const std::string&) { warnOnce(); return false; }
    std::string runJs(const std::string&) { warnOnce(); return ""; }
    void closeWindow() {}
    bool isAvailable() { return false; }
}

#endif