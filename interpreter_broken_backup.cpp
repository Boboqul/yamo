// ============================================================
//  interpreter.cpp — реализация интерпретатора ЯМО
//  Кроссплатформенный: Windows / Linux / macOS
// ============================================================
#include <thread>
#include <chrono>
#include <cmath>
#include <random>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cctype>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
#else
    #include <curl/curl.h>
#endif

#include "browser.h"
#include "sqlite3.h"
#include "interpreter.h"
#include "yamomath.h"

// ---- Браузер: подключаем, если создан; иначе заглушки ----
#if __has_include("browser.h")
#include "browser.h"
#else
#include <thread>
#include <chrono>
namespace yamo_browser {
    inline void pump(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    inline bool openUrl(const std::string&) { return false; }
    inline bool openHtml(const std::string&) { return false; }
    inline std::string runJs(const std::string&) { return ""; }
    inline void closeWindow() {}
}
#endif

// ------------------------------------------------------------
//  Платформенные хелперы
// ------------------------------------------------------------
#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], size);
    return w;
}
static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &result[0], size, NULL, NULL);
    return result;
}
#endif

// Универсальные файловые хелперы (UTF-8 пути на всех ОС)
static std::ifstream open_read_utf8(const std::string& p) {
    return std::ifstream(std::filesystem::u8path(p), std::ios::binary);
}
static std::ofstream open_write_utf8(const std::string& p, bool append = false) {
    auto flags = std::ios::binary | (append ? std::ios::app : std::ios::trunc);
    return std::ofstream(std::filesystem::u8path(p), flags);
}
static bool file_exists_utf8(const std::string& p) {
    std::ifstream f(std::filesystem::u8path(p));
    return f.good();
}
static int remove_file_utf8(const std::string& p) {
    std::error_code ec;
    bool r = std::filesystem::remove(std::filesystem::u8path(p), ec);
    return (r && !ec) ? 0 : 1;
}
static bool create_dir_utf8(const std::string& p) {
    std::error_code ec;
    std::filesystem::create_directory(std::filesystem::u8path(p), ec);
    return !ec;
}
static bool path_exists_utf8(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::u8path(p), ec);
}
static void remove_path_utf8(const std::string& p) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(p), ec);
}

// ------------------------------------------------------------
//  Сеть: единый интерфейс net_request()
// ------------------------------------------------------------
#ifdef _WIN32
static std::string winhttp_request(const std::string& url, const std::string* body,
                                   const std::string& method, const std::string& token) {
    std::wstring wurl = utf8_to_wide(url);
    URL_COMPONENTS uc = {}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        throw std::runtime_error("Неверный URL: " + url);
    HINTERNET hSession = WinHttpOpen(L"YAMO/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) throw std::runtime_error("Ошибка WinHttpOpen");
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); throw std::runtime_error("Не удалось подключиться к " + url); }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wmethod = utf8_to_wide(method);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
                     throw std::runtime_error("Ошибка создания запроса"); }
    if (body || !token.empty()) {
        std::wstring headers;
        if (body && body->find("=") != std::string::npos && body->find("{") == std::string::npos) {
            headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
        } else {
            headers = L"Content-Type: application/json\r\n";
        }
        if (!token.empty()) headers += L"Authorization: Bearer " + utf8_to_wide(token) + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)headers.size(), WINHTTP_ADDREQ_FLAG_ADD);
    }
    bool ok;
    if (body) {
        ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                (LPVOID)body->c_str(), (DWORD)body->size(), (DWORD)body->size(), 0)
             && WinHttpReceiveResponse(hRequest, NULL);
    } else {
        ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hRequest, NULL);
    }
    if (!ok) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
               throw std::runtime_error("Сервер не ответил: " + url); }
    std::string result; char buf[65536]; DWORD readed = 0;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &readed) && readed > 0) result.append(buf, readed);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return result;
}
static std::string net_request(const std::string& url, const std::string* body,
                               const std::string& method, const std::string& token) {
    return winhttp_request(url, body, method, token);
}
#else
static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    ((std::string*)userdata)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}
static std::string curl_request(const std::string& url, const std::string* body,
                                const std::string& method, const std::string& token) {
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("Ошибка curl_easy_init");
    std::string result;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "YAMO/1.0");
    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    std::string auth;
    if (!token.empty()) { auth = "Authorization: Bearer " + token; headers = curl_slist_append(headers, auth.c_str()); }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body->size());
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) throw std::runtime_error(std::string("Сеть: ") + curl_easy_strerror(rc));
    return result;
}
static std::string net_request(const std::string& url, const std::string* body,
                               const std::string& method, const std::string& token) {
    return curl_request(url, body, method, token);
}
#endif

// ------------------------------------------------------------
//  GUI: диалог ввода (только Windows)
// ------------------------------------------------------------
#ifdef _WIN32
static std::wstring g_guiInputResult;
static bool g_guiInputOk;
static HWND g_guiHEdit;

static LRESULT CALLBACK YamoInputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                wchar_t buf[1024] = {0};
                GetWindowTextW(g_guiHEdit, buf, 1024);
                g_guiInputResult = buf;
                g_guiInputOk = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == 2) {
                g_guiInputOk = false;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            g_guiInputOk = false;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool yamoInputDialog(const std::wstring& title, const std::wstring& prompt, std::wstring& result) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = YamoInputDlgProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"YamoInputDlgClass";
        wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        classRegistered = true;
    }
    g_guiInputOk = false;
    g_guiInputResult.clear();
    int width = 340, height = 150;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"YamoInputDlgClass", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, width, height,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd) return false;
    CreateWindowExW(0, L"STATIC", prompt.c_str(), WS_CHILD | WS_VISIBLE, 12, 12, 300, 20, hwnd, NULL, NULL, NULL);
    g_guiHEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 12, 38, 300, 24, hwnd, NULL, NULL, NULL);
    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 140, 75, 80, 26, hwnd, (HMENU)1, NULL, NULL);
    CreateWindowExW(0, L"BUTTON", utf8_to_wide("Отмена").c_str(), WS_CHILD | WS_VISIBLE, 230, 75, 80, 26, hwnd, (HMENU)2, NULL, NULL);
    SetFocus(g_guiHEdit);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    result = g_guiInputResult;
    return g_guiInputOk;
}
#endif

// ------------------------------------------------------------
//  Утилиты языка
// ------------------------------------------------------------
static std::string sanitize_path(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if (i + 2 < s.size() && c == 0xE2 && (unsigned char)s[i+1] == 0x80) { i += 3; continue; }
        if (i + 1 < s.size() && c == 0xC2 && (unsigned char)s[i+1] == 0xA0) { i += 2; continue; }
        if (i + 2 < s.size() && c == 0xEF && (unsigned char)s[i+1] == 0xBB && (unsigned char)s[i+2] == 0xBF) { i += 3; continue; }
        if (c == '\r' || c == '\n' || c == '\t') { i++; continue; }
        out += c; i++;
    }
    return out;
}

static std::string utf8_change_case(const std::string& s, bool to_upper) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        if (c < 0x80) {
            if (to_upper && c >= 'a' && c <= 'z') out += (char)(c - 32);
            else if (!to_upper && c >= 'A' && c <= 'Z') out += (char)(c + 32);
            else out += (char)c;
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            unsigned char c2 = s[i + 1];
            int cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
            if (to_upper) {
                if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
                else if (cp == 0x451) cp = 0x401;
            } else {
                if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
                else if (cp == 0x401) cp = 0x451;
            }
            out += (char)(0xC0 | ((cp >> 6) & 0x1F));
            out += (char)(0x80 | (cp & 0x3F));
            i += 2;
        } else {
            out += (char)c;
            i++;
        }
    }
    return out;
}

struct ReturnException : public std::exception { std::shared_ptr<Value> value; ReturnException(std::shared_ptr<Value> v) : value(v) {} const char* what() const noexcept override { return "return"; } };
struct BreakException {};
struct ContinueException {};

class Environment {
public:
    std::unordered_map<std::string, std::shared_ptr<Value>> values;
    std::shared_ptr<Environment> parent;
    Environment() : parent(nullptr) {}
    Environment(std::shared_ptr<Environment> p) : parent(p) {}
    void define(const std::string& name, std::shared_ptr<Value> value) { values[name] = value; }
    std::shared_ptr<Value> get(const std::string& name) {
        if (values.find(name) != values.end()) return values[name];
        if (parent) return parent->get(name);
        throw std::runtime_error("Переменная '" + name + "' не определена");
    }
    void assign(const std::string& name, std::shared_ptr<Value> value) {
        if (values.find(name) != values.end()) { values[name] = value; return; }
        if (parent) { parent->assign(name, value); return; }
        throw std::runtime_error("Переменная '" + name + "' не определена");
    }
};

// ------------------------------------------------------------
//  Интерпретатор: запуск и модули
// ------------------------------------------------------------
void Interpreter::interpret(std::shared_ptr<Program> program) {
    importedModules.clear();
    if (db) { sqlite3_close(db); db = nullptr; dbOpen = false; }
    if (!program) return;
    environment = std::make_shared<Environment>();
    for (const auto& stmt : program->statements) {
        if (auto f = std::dynamic_pointer_cast<FunctionDecl>(stmt)) functions[f->name] = f;
        if (auto c = std::dynamic_pointer_cast<ClassDecl>(stmt)) classes[c->name] = c;
    }
    for (const auto& stmt : program->statements) {
        if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) importModule(imp->path);
    }
    if (functions.find("главная") != functions.end()) {
        try { executeBlock(functions["главная"]->body); } catch (const ReturnException&) {}
    } else {
        executeBlock(program->statements);
    }
}

void Interpreter::importModule(const std::string& path) {
    std::string clean = sanitize_path(path);
    if (importedModules.count(clean)) return;
    importedModules.insert(clean);

    std::ifstream f = open_read_utf8(clean);
    if (!f.is_open()) {
        std::error_code ec;
        auto abs = std::filesystem::absolute(std::filesystem::u8path(clean), ec);
        if (!ec) f = std::ifstream(abs, std::ios::binary);
    }
    if (!f.is_open()) throw std::runtime_error("Не удалось импортировать модуль: " + clean);

    std::stringstream b; b << f.rdbuf();
    std::string source = b.str();
    if (source.size() >= 3 && (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF) {
        source = source.substr(3);
    }
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto mod = parser.parseProgram();
    for (const auto& stmt : mod->statements) {
        if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(stmt)) functions[fn->name] = fn;
        if (auto c = std::dynamic_pointer_cast<ClassDecl>(stmt)) classes[c->name] = c;
        if (auto imp = std::dynamic_pointer_cast<ImportStmt>(stmt)) importModule(imp->path);
    }
}

void Interpreter::executeBlock(const std::vector<std::shared_ptr<ASTNode>>& block) {
for (const auto& s : block) {
    if (s) {
        std::string tn = "unknown";
        if (std::dynamic_pointer_cast<ReturnStmt>(s)) tn = "ReturnStmt";
        else if (std::dynamic_pointer_cast<VarDeclaration>(s)) tn = "VarDeclaration";
        else if (std::dynamic_pointer_cast<Assignment>(s)) tn = "Assignment";
        else if (std::dynamic_pointer_cast<PrintStmt>(s)) tn = "PrintStmt";
        else if (std::dynamic_pointer_cast<IfStmt>(s)) tn = "IfStmt";
        else if (std::dynamic_pointer_cast<ForStmt>(s)) tn = "ForStmt";
        else if (std::dynamic_pointer_cast<WhileStmt>(s)) tn = "WhileStmt";
        else if (std::dynamic_pointer_cast<CallExpr>(s)) tn = "CallExpr";
        else if (std::dynamic_pointer_cast<BreakStmt>(s)) tn = "BreakStmt";
        else if (std::dynamic_pointer_cast<ContinueStmt>(s)) tn = "ContinueStmt";
        else if (std::dynamic_pointer_cast<TryCatchStmt>(s)) tn = "TryCatchStmt";
        else if (std::dynamic_pointer_cast<ErrorStmt>(s)) tn = "ErrorStmt";
        else if (std::dynamic_pointer_cast<ClassDecl>(s)) tn = "ClassDecl";
        else if (std::dynamic_pointer_cast<FunctionDecl>(s)) tn = "FunctionDecl";
        else if (std::dynamic_pointer_cast<ImportStmt>(s)) tn = "ImportStmt";
        else if (std::dynamic_pointer_cast<MemberAssignment>(s)) tn = "MemberAssignment";
        else if (std::dynamic_pointer_cast<MethodCall>(s)) tn = "MethodCall";
        else if (std::dynamic_pointer_cast<ArrayAssignStmt>(s)) tn = "ArrayAssignStmt";
        else if (std::dynamic_pointer_cast<PrintNoNewlineStmt>(s)) tn = "PrintNoNewlineStmt";
        if (tn == "unknown") tn = std::string("unknown(") + typeid(*s).name() + ")";
    }
    execute(s);
}
}

void Interpreter::executeTryCatch(std::shared_ptr<TryCatchStmt> tc) {
    try {
        executeBlock(tc->tryBody);
    } catch (const ErrorException& e) {
        auto env = std::make_shared<Environment>(environment);
        env->define(tc->errorName, e.value);
        executeBlockWithEnv(tc->catchBody, env);
    }
}

void Interpreter::executeBlockWithEnv(const std::vector<std::shared_ptr<ASTNode>>& block, std::shared_ptr<Environment> env) {
    auto prev = environment;
    environment = env;
    for (const auto& s : block) execute(s);
    environment = prev;
}

// ------------------------------------------------------------
//  execute
// ------------------------------------------------------------
void Interpreter::execute(std::shared_ptr<ASTNode> node) {
    if (!node) return;
    if (auto c = std::dynamic_pointer_cast<ClassDecl>(node)) {
        environment->define(c->name, std::make_shared<ClassValue>(c->name, c));
        return;
    }
    if (auto imp = std::dynamic_pointer_cast<ImportStmt>(node)) {
        importModule(imp->path);
        return;
    }
    if (auto ma = std::dynamic_pointer_cast<MemberAssignment>(node)) {
        auto objVal = environment->get(ma->object);
        auto obj = std::dynamic_pointer_cast<ObjectValue>(objVal);
        if (!obj) throw std::runtime_error("Присваивание полю не-объекта '" + ma->object + "'");
        obj->fields[ma->member] = evaluate(ma->value);
        return;
    }
    if (auto mc = std::dynamic_pointer_cast<MethodCall>(node)) {
        auto objVal = evaluate(mc->object);
        auto obj = std::dynamic_pointer_cast<ObjectValue>(objVal);
        if (!obj) throw std::runtime_error("Вызов метода не-объекта");
        callMethod(obj, mc->method, mc->args);
        return;
    }
    if (auto c = std::dynamic_pointer_cast<CallExpr>(node)) {
        callFunction(c->callee, c->arguments);
        return;
    }
    if (auto a = std::dynamic_pointer_cast<ArrayAssignStmt>(node)) {
        if (a->indices.size() == 1) {
            auto m = std::dynamic_pointer_cast<MapValue>(environment->get(a->arrayName));
            if (m) {
                std::string key = evaluate(a->indices[0])->toString();
                m->set(key, evaluate(a->value));
                return;
            }
        }
        std::shared_ptr<Value> currentArr = environment->get(a->arrayName);
        for (size_t i = 0; i + 1 < a->indices.size(); i++) {
            int idx = (int)evaluate(a->indices[i])->toNumber();
            if (auto arr = std::dynamic_pointer_cast<ArrayValue>(currentArr)) {
                if (idx >= 0 && idx < (int)arr->elements.size()) currentArr = arr->elements[idx];
                else throw std::runtime_error("Индекс вне границ (вложенный): " + std::to_string(idx));
            } else throw std::runtime_error("Попытка индексации не-массива (вложенный)");
        }
        int lastIdx = (int)evaluate(a->indices.back())->toNumber();
        auto newVal = evaluate(a->value);
        if (auto arr = std::dynamic_pointer_cast<ArrayValue>(currentArr)) {
            if (lastIdx >= 0 && lastIdx < (int)arr->elements.size()) arr->elements[lastIdx] = newVal;
            else throw std::runtime_error("Индекс вне границ при записи: " + std::to_string(lastIdx));
        } else throw std::runtime_error("Попытка записи в не-массив");
        return;
    }
    if (auto a = std::dynamic_pointer_cast<Assignment>(node)) {
        environment->assign(a->name, evaluate(a->value));
        return;
    }
    if (auto v = std::dynamic_pointer_cast<VarDeclaration>(node)) {
        std::shared_ptr<Value> val;
        if (v->value) {
            val = evaluate(v->value);
            if (!v->isArray) {
                if (v->type == "целое" || v->type == "дробное") val = std::make_shared<NumberValue>(val->toNumber());
                else if (v->type == "текст") val = std::make_shared<StringValue>(val->toString());
                else if (v->type == "логическое") val = std::make_shared<BoolValue>(val->toBool());
            }
        } else if (v->isArray) {
            val = std::make_shared<ArrayValue>();
        } else if (v->type == "целое" || v->type == "дробное") {
            val = std::make_shared<NumberValue>(0.0);
        } else if (v->type == "текст") {
            val = std::make_shared<StringValue>("");
        } else if (v->type == "логическое") {
            val = std::make_shared<BoolValue>(false);
        } else if (v->type == "словарь") {
            val = std::make_shared<MapValue>();
        } else {
            val = std::make_shared<NumberValue>(0.0);
        }
        environment->define(v->name, val);
        return;
    }
    if (auto p = std::dynamic_pointer_cast<PrintStmt>(node)) { std::cout << evaluate(p->value)->toString() << "\n"; return; }
    if (auto p = std::dynamic_pointer_cast<PrintNoNewlineStmt>(node)) { std::cout << evaluate(p->value)->toString(); return; }
    if (auto i = std::dynamic_pointer_cast<IfStmt>(node)) {
        if (evaluate(i->condition)->toBool()) {
            executeBlock(i->thenBranch);
        } else if (!i->elseBranch.empty()) {
            if (i->elseBranch.size() == 1) {
                if (auto elif = std::dynamic_pointer_cast<IfStmt>(i->elseBranch[0])) { execute(elif); return; }
            }
            executeBlock(i->elseBranch);
        }
        return;
    }
    if (auto f = std::dynamic_pointer_cast<ForStmt>(node)) {
        if (f->init) execute(f->init);
        while (true) {
            if (f->condition && !evaluate(f->condition)->toBool()) break;
            try { executeBlock(f->body); }
            catch (const BreakException&) { break; }
            catch (const ContinueException&) {}
            if (f->increment) execute(f->increment);
        }
        return;
    }
    if (auto w = std::dynamic_pointer_cast<WhileStmt>(node)) {
        while (true) {
            if (w->condition && !evaluate(w->condition)->toBool()) break;
            try { executeBlock(w->body); }
            catch (const BreakException&) { break; }
            catch (const ContinueException&) {}
        }
        return;
    }
    if (auto e = std::dynamic_pointer_cast<ErrorStmt>(node)) {
        throw ErrorException{e->value ? evaluate(e->value) : std::make_shared<StringValue>("неизвестная ошибка")};
    }
    if (std::dynamic_pointer_cast<BreakStmt>(node)) throw BreakException();
    if (std::dynamic_pointer_cast<ContinueStmt>(node)) throw ContinueException();
    if (auto tc = std::dynamic_pointer_cast<TryCatchStmt>(node)) { executeTryCatch(tc); return; }
    if (auto r = std::dynamic_pointer_cast<ReturnStmt>(node)) { auto rv = r->value ? evaluate(r->value) : std::make_shared<NumberValue>(0.0); throw ReturnException(rv); }
    if (auto ma = std::dynamic_pointer_cast<MemberAccess>(node)) { return; }
}

// ------------------------------------------------------------
//  evaluate
// ------------------------------------------------------------
std::shared_ptr<Value> Interpreter::evaluate(std::shared_ptr<ASTNode> node) {
    if (!node) return std::make_shared<NumberValue>(0.0);
    if (auto n = std::dynamic_pointer_cast<NumberExpr>(node)) return std::make_shared<NumberValue>(n->value);
    if (auto s = std::dynamic_pointer_cast<StringExpr>(node)) return std::make_shared<StringValue>(s->value);
    if (auto b = std::dynamic_pointer_cast<BoolLiteral>(node)) return std::make_shared<BoolValue>(b->value);
    if (auto i = std::dynamic_pointer_cast<IdentifierExpr>(node)) { auto v = environment->get(i->name); return v; }
    if (auto c = std::dynamic_pointer_cast<CallExpr>(node)) { auto rv = callFunction(c->callee, c->arguments); return rv; }
    if (auto a = std::dynamic_pointer_cast<ArrayExpr>(node)) {
        auto arr = std::make_shared<ArrayValue>();
        for (auto& e : a->elements) arr->elements.push_back(evaluate(e));
        return arr;
    }
    if (auto ac = std::dynamic_pointer_cast<ArrayAccessExpr>(node)) {
        auto arrVal = evaluate(ac->array);
        if (auto m = std::dynamic_pointer_cast<MapValue>(arrVal)) {
            std::string key = evaluate(ac->index)->toString();
            auto it = m->pairs.find(key);
            if (it == m->pairs.end()) throw std::runtime_error("Ключ '" + key + "' не найден");
            return it->second;
        }
        int idx = (int)evaluate(ac->index)->toNumber();
        if (auto arr = std::dynamic_pointer_cast<ArrayValue>(arrVal)) {
            if (idx >= 0 && idx < (int)arr->elements.size()) return arr->elements[idx];
            throw std::runtime_error("Индекс вне границ при чтении: " + std::to_string(idx));
        }
        throw std::runtime_error("Попытка индексации не-массива");
    }
    if (auto l = std::dynamic_pointer_cast<LengthExpr>(node)) {
        auto v = evaluate(l->value);
        if (auto a = std::dynamic_pointer_cast<ArrayValue>(v)) return std::make_shared<NumberValue>(a->elements.size());
        if (auto m = std::dynamic_pointer_cast<MapValue>(v)) return std::make_shared<NumberValue>(m->pairs.size());
        if (auto s = std::dynamic_pointer_cast<StringValue>(v)) {
            int cnt = 0;
            for (unsigned char c : s->value) if ((c & 0xC0) != 0x80) cnt++;
            return std::make_shared<NumberValue>(cnt);
        }
        throw std::runtime_error("длина() требует массив или строку");
    }
    if (auto le = std::dynamic_pointer_cast<LogicalExpr>(node)) {
        auto l = evaluate(le->left); auto r = evaluate(le->right);
        if (le->op == "и") return std::make_shared<BoolValue>(l->toBool() && r->toBool());
        if (le->op == "или") return std::make_shared<BoolValue>(l->toBool() || r->toBool());
        return std::make_shared<BoolValue>(false);
    }
    if (auto ne = std::dynamic_pointer_cast<NotExpr>(node)) {
        auto v = evaluate(ne->operand);
        return std::make_shared<BoolValue>(!v->toBool());
    }
    if (auto b = std::dynamic_pointer_cast<BinaryExpr>(node)) {
        auto l = evaluate(b->left); auto r = evaluate(b->right);
        auto ln = std::dynamic_pointer_cast<NumberValue>(l); auto rn = std::dynamic_pointer_cast<NumberValue>(r);
        if (ln && rn) {
            double lv = ln->value, rv = rn->value;
            if (b->op == "+") return std::make_shared<NumberValue>(lv + rv);
            if (b->op == "-") return std::make_shared<NumberValue>(lv - rv);
            if (b->op == "*") return std::make_shared<NumberValue>(lv * rv);
            if (b->op == "/") { if (rv == 0) throw std::runtime_error("Деление на ноль"); return std::make_shared<NumberValue>(lv / rv); }
            if (b->op == "%") { if (rv == 0) throw std::runtime_error("Деление на ноль"); return std::make_shared<NumberValue>((int)lv % (int)rv); }
            if (b->op == "==") return std::make_shared<BoolValue>(lv == rv);
            if (b->op == "!=") return std::make_shared<BoolValue>(lv != rv);
            if (b->op == "<") return std::make_shared<BoolValue>(lv < rv);
            if (b->op == ">") return std::make_shared<BoolValue>(lv > rv);
            if (b->op == "<=") return std::make_shared<BoolValue>(lv <= rv);
            if (b->op == ">=") return std::make_shared<BoolValue>(lv >= rv);
        }
        std::string ls = l->toString(); std::string rs = r->toString();
        bool lsIsStr = std::dynamic_pointer_cast<StringValue>(l) != nullptr;
        bool rsIsStr = std::dynamic_pointer_cast<StringValue>(r) != nullptr;
        auto la = std::dynamic_pointer_cast<ArrayValue>(l);
        auto ra = std::dynamic_pointer_cast<ArrayValue>(r);
        if (b->op == "+") {
            if (la && ra) {
                // Конкатенация двух массивов
                auto result = std::make_shared<ArrayValue>();
                for (auto& e : la->elements) result->elements.push_back(e);
                for (auto& e : ra->elements) result->elements.push_back(e);
                return result;
            }
            if (lsIsStr || rsIsStr) return std::make_shared<StringValue>(ls + rs);
            return std::make_shared<NumberValue>(l->toNumber() + r->toNumber());
        }
        if (b->op == "==") return std::make_shared<BoolValue>(ls == rs);
        if (b->op == "!=") return std::make_shared<BoolValue>(ls != rs);
        if (lsIsStr && rsIsStr) {
            if (b->op == "<") return std::make_shared<BoolValue>(ls < rs);
            if (b->op == ">") return std::make_shared<BoolValue>(ls > rs);
            if (b->op == "<=") return std::make_shared<BoolValue>(ls <= rs);
            if (b->op == ">=") return std::make_shared<BoolValue>(ls >= rs);
        }
        if (lsIsStr || rsIsStr) {
            if (b->op == "<") return std::make_shared<BoolValue>(l->toNumber() < r->toNumber());
            if (b->op == ">") return std::make_shared<BoolValue>(l->toNumber() > r->toNumber());
            if (b->op == "<=") return std::make_shared<BoolValue>(l->toNumber() <= r->toNumber());
            if (b->op == ">=") return std::make_shared<BoolValue>(l->toNumber() >= r->toNumber());
        }
        throw std::runtime_error("Неподдерживаемая операция: " + b->op);
    }
    if (auto n = std::dynamic_pointer_cast<NewExpr>(node)) {
        if (classes.find(n->className) == classes.end()) throw std::runtime_error("Класс '" + n->className + "' не найден");
        auto cls = classes[n->className];
        std::vector<std::shared_ptr<ClassDecl>> chain;
        auto cur = cls;
        int guard = 0;
        while (cur && guard++ < 100) {
            chain.push_back(cur);
            if (cur->parentName.empty() || classes.find(cur->parentName) == classes.end()) break;
            cur = classes[cur->parentName];
        }
        auto obj = std::make_shared<ObjectValue>(n->className, cls);
        obj->chain = chain;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (auto& f : (*it)->fields) {
                if (f->type == "целое" || f->type == "дробное") obj->fields[f->name] = std::make_shared<NumberValue>(0.0);
                else if (f->type == "текст") obj->fields[f->name] = std::make_shared<StringValue>("");
                else if (f->type == "логическое") obj->fields[f->name] = std::make_shared<BoolValue>(false);
                else obj->fields[f->name] = std::make_shared<NumberValue>(0.0);
            }
        }
        for (auto& c : chain) {
            for (auto& m : c->methods) {
                if (m->name == n->className || m->name == "инициализация") { callMethod(obj, m->name, n->args); return obj; }
            }
        }
        return obj;
    }
    if (std::dynamic_pointer_cast<ThisExpr>(node)) return environment->get("этот");
    if (auto m = std::dynamic_pointer_cast<MemberAccess>(node)) {
        auto objVal = evaluate(m->object);
        auto obj = std::dynamic_pointer_cast<ObjectValue>(objVal);
        if (!obj) throw std::runtime_error("Обращение к полю не-объекта");
        if (obj->fields.find(m->member) == obj->fields.end())
            throw std::runtime_error("Поле '" + m->member + "' не найдено в объекте '" + obj->className + "'");
        return obj->fields[m->member];
    }
    if (auto m = std::dynamic_pointer_cast<MethodCall>(node)) {
        auto objVal = evaluate(m->object);
        auto obj = std::dynamic_pointer_cast<ObjectValue>(objVal);
        if (!obj) throw std::runtime_error("Вызов метода не-объекта");
        return callMethod(obj, m->method, m->args);
    }
    return std::make_shared<NumberValue>(0.0);
}

// ------------------------------------------------------------
//  JSON
// ------------------------------------------------------------
static std::string json_escape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    return out + "\"";
}

static std::string json_serialize(const Value* v) {
    if (!v) return "null";
    if (auto n = dynamic_cast<const NumberValue*>(v)) {
        double d = n->value;
        char b[64];
        if (d == (double)(long long)d) snprintf(b, sizeof b, "%lld", (long long)d);
        else snprintf(b, sizeof b, "%.17g", d);
        return b;
    }
    if (auto s = dynamic_cast<const StringValue*>(v)) return json_escape(s->value);
    if (auto b = dynamic_cast<const BoolValue*>(v)) return b->value ? "true" : "false";
    if (dynamic_cast<const NullValue*>(v)) return "null";
    if (auto a = dynamic_cast<const ArrayValue*>(v)) {
        std::string out = "[";
        for (size_t i = 0; i < a->elements.size(); i++) {
            if (i) out += ",";
            out += json_serialize(a->elements[i].get());
        }
        return out + "]";
    }
    if (auto m = dynamic_cast<const MapValue*>(v)) {
        std::string out = "{";
        bool first = true;
        for (auto& k : m->order) {
            if (!first) out += ",";
            first = false;
            out += json_escape(k) + ":" + json_serialize(m->pairs.at(k).get());
        }
        return out + "}";
    }
    return "null";
}

std::string MapValue::toString() const { return json_serialize(this); }

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    JsonParser(const std::string& str) : s(str) {}
    void skip() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; }
    bool eof() const { return i >= s.size(); }
    void expect(char c) {
        if (i >= s.size() || s[i] != c)
            throw std::runtime_error(std::string("JSON: ожидался '") + c + "' на позиции " + std::to_string(i));
        i++;
    }
    static void utf8_encode(int cp, std::string& out) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
        else { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
    }
    std::string str() {
        expect('"');
        std::string out;
        while (!eof() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (eof()) throw std::runtime_error("JSON: обрыв строки");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (i + 4 > s.size()) throw std::runtime_error("JSON: плохой \\u");
                    int cp = (int)strtol(s.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i]=='\\' && s[i+1]=='u') {
                        int lo = (int)strtol(s.substr(i+2, 4).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { i += 6; cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00); }
                    }
                    utf8_encode(cp, out);
                    break;
                }
                default: out += e; break;
            }
        }
        expect('"');
        return out;
    }
    std::shared_ptr<Value> value() {
        skip();
        if (eof()) throw std::runtime_error("JSON: неожиданный конец");
        char c = s[i];
        if (c == '{') {
            i++;
            auto m = std::make_shared<MapValue>();
            skip();
            if (!eof() && s[i] == '}') { i++; return m; }
            while (true) {
                skip();
                std::string k = str();
                skip();
                expect(':');
                m->set(k, value());
                skip();
                if (!eof() && s[i] == ',') { i++; continue; }
                expect('}');
                break;
            }
            return m;
        }
        if (c == '[') {
            i++;
            auto a = std::make_shared<ArrayValue>();
            skip();
            if (!eof() && s[i] == ']') { i++; return a; }
            while (true) {
                a->elements.push_back(value());
                skip();
                if (!eof() && s[i] == ',') { i++; continue; }
                expect(']');
                break;
            }
            return a;
        }
        if (c == '"') return std::make_shared<StringValue>(str());
        if (s.compare(i, 4, "true") == 0)  { i += 4; return std::make_shared<BoolValue>(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return std::make_shared<BoolValue>(false); }
        if (s.compare(i, 4, "null") == 0)  { i += 4; return std::make_shared<NullValue>(); }
        size_t st = i;
        if (!eof() && (s[i]=='-'||s[i]=='+')) i++;
        while (!eof() && isdigit((unsigned char)s[i])) i++;
        if (!eof() && s[i]=='.') { i++; while (!eof() && isdigit((unsigned char)s[i])) i++; }
        if (!eof() && (s[i]=='e'||s[i]=='E')) { i++; if (!eof()&&(s[i]=='+'||s[i]=='-')) i++; while (!eof() && isdigit((unsigned char)s[i])) i++; }
        if (st == i) throw std::runtime_error("JSON: не понимаю токен на позиции " + std::to_string(i));
        return std::make_shared<NumberValue>(std::stod(s.substr(st, i - st)));
    }
    std::shared_ptr<Value> parse() { skip(); auto v = value(); skip(); return v; }
};

// ============================================================
//  Внутренние хелперы браузера (программист их не видит)
// ============================================================
// Экранирование строки для вставки в JS в одинарных кавычках
static std::string js_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\'') out += "\\'";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

// WebView2 возвращает результат как JSON. Снимаем кавычки со строк.
static std::string unwrapJsonString(const std::string& s) {
    if (s == "null" || s == "undefined" || s.empty()) return "";
    if (s.front() == '"') {
        try { JsonParser p(s); return p.str(); } catch (...) { return s; }
    }
    return s;
}

// ------------------------------------------------------------
//  callFunction — встроенные функции
// ------------------------------------------------------------
std::shared_ptr<Value> Interpreter::callFunction(const std::string& name, const std::vector<std::shared_ptr<ASTNode>>& arguments) {
    if (name == "ввод") {
        std::cout.flush();
        std::string input;
        std::cin >> input;
        return std::make_shared<StringValue>(input);
    }
    if (name == "читать_файл") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        auto p = evaluate(arguments[0])->toString();
        std::ifstream f = open_read_utf8(p);
        if (!f.is_open()) throw std::runtime_error("Не удалось открыть файл: " + p);
        std::stringstream b; b << f.rdbuf();
        std::string text = b.str(), normalized;
        for (char c : text) { if (c != '\r') normalized += c; }
        return std::make_shared<StringValue>(normalized);
    }
    if (name == "запись_файл") {
        if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0);
        auto p = evaluate(arguments[0])->toString();
        auto c = evaluate(arguments[1])->toString();
        std::ofstream f = open_write_utf8(p, false);
        if (!f.is_open()) throw std::runtime_error("Не удалось создать файл: " + p);
        f << c; return std::make_shared<NumberValue>(1.0);
    }
    if (name == "дописать_файл") {
        if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0);
        auto p = evaluate(arguments[0])->toString();
        auto c = evaluate(arguments[1])->toString();
        std::ofstream f = open_write_utf8(p, true);
        if (!f.is_open()) throw std::runtime_error("Не удалось открыть файл: " + p);
        f << c; return std::make_shared<NumberValue>(1.0);
    }
    if (name == "существует_файл") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        return std::make_shared<BoolValue>(file_exists_utf8(evaluate(arguments[0])->toString()));
    }
    if (name == "удалить_файл") {
        if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
        return std::make_shared<NumberValue>(remove_file_utf8(evaluate(arguments[0])->toString()) == 0 ? 1.0 : 0.0);
    }
    if (name == "в_число") {
        if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
        try { return std::make_shared<NumberValue>(std::stod(evaluate(arguments[0])->toString())); }
        catch (...) { throw std::runtime_error("Не удалось преобразовать в число"); }
    }
    if (name == "разделить") {
        if (arguments.size() < 2) return std::make_shared<ArrayValue>();
        std::string text = evaluate(arguments[0])->toString();
        std::string sep = evaluate(arguments[1])->toString();
        auto result = std::make_shared<ArrayValue>();
        if (sep.empty()) { result->elements.push_back(std::make_shared<StringValue>(text)); return result; }
        size_t start = 0, pos = text.find(sep);
        while (pos != std::string::npos) {
            result->elements.push_back(std::make_shared<StringValue>(text.substr(start, pos - start)));
            start = pos + sep.length(); pos = text.find(sep, start);
        }
        result->elements.push_back(std::make_shared<StringValue>(text.substr(start)));
        return result;
    }
    if (name == "случайное") {
        static std::mt19937 gen(std::random_device{}());
        if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0);
        int минимум = (int)evaluate(arguments[0])->toNumber();
        int максимум = (int)evaluate(arguments[1])->toNumber();
        if (минимум > максимум) std::swap(минимум, максимум);
        std::uniform_int_distribution<int> dist(минимум, максимум);
        return std::make_shared<NumberValue>((double)dist(gen));
    }
    if (name == "модуль") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(std::abs(evaluate(arguments[0])->toNumber())); }
    if (name == "округлить") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(std::round(evaluate(arguments[0])->toNumber())); }
    if (name == "степень") { if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(std::pow(evaluate(arguments[0])->toNumber(), evaluate(arguments[1])->toNumber())); }
    if (name == "корень") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); double v = evaluate(arguments[0])->toNumber(); if (v < 0) throw std::runtime_error("корень из отрицательного числа"); return std::make_shared<NumberValue>(std::sqrt(v)); }
    if (name == "мин") { if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0); double a = evaluate(arguments[0])->toNumber(); double b = evaluate(arguments[1])->toNumber(); return std::make_shared<NumberValue>(a < b ? a : b); }
    if (name == "макс") { if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0); double a = evaluate(arguments[0])->toNumber(); double b = evaluate(arguments[1])->toNumber(); return std::make_shared<NumberValue>(a > b ? a : b); }
    if (name == "верхний_регистр") { if (arguments.empty()) return std::make_shared<StringValue>(""); return std::make_shared<StringValue>(utf8_change_case(evaluate(arguments[0])->toString(), true)); }
    if (name == "нижний_регистр") { if (arguments.empty()) return std::make_shared<StringValue>(""); return std::make_shared<StringValue>(utf8_change_case(evaluate(arguments[0])->toString(), false)); }
    if (name == "содержит") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        return std::make_shared<BoolValue>(evaluate(arguments[0])->toString().find(evaluate(arguments[1])->toString()) != std::string::npos);
    }
    if (name == "заменить") {
        if (arguments.size() < 3) return std::make_shared<StringValue>("");
        std::string s = evaluate(arguments[0])->toString();
        std::string what = evaluate(arguments[1])->toString();
        std::string repl = evaluate(arguments[2])->toString();
        if (what.empty()) return std::make_shared<StringValue>(s);
        std::string result; size_t pos = 0, prev = 0;
        while ((pos = s.find(what, prev)) != std::string::npos) {
            result += s.substr(prev, pos - prev) + repl; prev = pos + what.size();
        }
        result += s.substr(prev); return std::make_shared<StringValue>(result);
    }
    if (name == "начинается_с") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string s = evaluate(arguments[0])->toString(), p = evaluate(arguments[1])->toString();
        return std::make_shared<BoolValue>(s.size() >= p.size() && s.substr(0, p.size()) == p);
    }
    if (name == "заканчивается_на") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string s = evaluate(arguments[0])->toString(), suf = evaluate(arguments[1])->toString();
        return std::make_shared<BoolValue>(s.size() >= suf.size() && s.substr(s.size() - suf.size()) == suf);
    }
    if (name == "часть") {
        if (arguments.size() < 3) return std::make_shared<StringValue>("");
        std::string s = evaluate(arguments[0])->toString();
        int from = (int)evaluate(arguments[1])->toNumber();
        int to = (int)evaluate(arguments[2])->toNumber();
        if (from < 0) from = 0;
        int byteFrom = 0, charCount = 0;
        while (byteFrom < (int)s.size() && charCount < from) {
            unsigned char c = s[byteFrom];
            if (c < 0x80) byteFrom += 1;
            else if ((c & 0xE0) == 0xC0) byteFrom += 2;
            else if ((c & 0xF0) == 0xE0) byteFrom += 3;
            else if ((c & 0xF8) == 0xF0) byteFrom += 4;
            else byteFrom += 1;
            charCount++;
        }
        int byteTo = byteFrom;
        while (byteTo < (int)s.size() && charCount < to) {
            unsigned char c = s[byteTo];
            if (c < 0x80) byteTo += 1;
            else if ((c & 0xE0) == 0xC0) byteTo += 2;
            else if ((c & 0xF0) == 0xE0) byteTo += 3;
            else if ((c & 0xF8) == 0xF0) byteTo += 4;
            else byteTo += 1;
            charCount++;
        }
        if (byteFrom >= byteTo) return std::make_shared<StringValue>("");
        return std::make_shared<StringValue>(s.substr(byteFrom, byteTo - byteFrom));
    }
    if (name == "символ_из_кода") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        int code = (int)evaluate(arguments[0])->toNumber();
        std::string result;
        if (code < 0x80) {
            result += (char)code;
        } else if (code < 0x800) {
            result += (char)(0xC0 | (code >> 6));
            result += (char)(0x80 | (code & 0x3F));
        } else {
            result += (char)(0xE0 | (code >> 12));
            result += (char)(0x80 | ((code >> 6) & 0x3F));
            result += (char)(0x80 | (code & 0x3F));
        }
        return std::make_shared<StringValue>(result);
    }
    if (name == "тип_значения") {
        if (arguments.empty()) return std::make_shared<StringValue>("ничто");
        auto v = evaluate(arguments[0]);
        if (std::dynamic_pointer_cast<NumberValue>(v)) return std::make_shared<StringValue>("число");
        if (std::dynamic_pointer_cast<StringValue>(v)) return std::make_shared<StringValue>("текст");
        if (std::dynamic_pointer_cast<BoolValue>(v)) return std::make_shared<StringValue>("логическое");
        if (std::dynamic_pointer_cast<ArrayValue>(v)) return std::make_shared<StringValue>("массив");
        if (std::dynamic_pointer_cast<MapValue>(v)) return std::make_shared<StringValue>("словарь");
        return std::make_shared<StringValue>("ничто");
    }
    if (name == "в_число") {
        if (arguments.empty()) return std::make_shared<NumberValue>(0);
        std::string s = evaluate(arguments[0])->toString();
        try { return std::make_shared<NumberValue>(std::stod(s)); }
        catch (...) { return std::make_shared<NumberValue>(0); }
    }
    if (name == "в_текст") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        return std::make_shared<StringValue>(evaluate(arguments[0])->toString());
    }
    if (name == "найти") {
        if (arguments.size() < 2) return std::make_shared<NumberValue>(-1);
        size_t pos = evaluate(arguments[0])->toString().find(evaluate(arguments[1])->toString());
        return std::make_shared<NumberValue>(pos == std::string::npos ? -1 : (double)pos);
    }
    if (name == "повторить") {
        if (arguments.size() < 2) return std::make_shared<StringValue>("");
        std::string s = evaluate(arguments[0])->toString(); int n = (int)evaluate(arguments[1])->toNumber();
        if (n <= 0) return std::make_shared<StringValue>("");
        std::string result; for (int i = 0; i < n; i++) result += s; return std::make_shared<StringValue>(result);
    }

    // === Сеть (кроссплатформенная) ===
    if (name == "http_получить") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        std::string url = evaluate(arguments[0])->toString();
        return std::make_shared<StringValue>(net_request(url, nullptr, "GET", ""));
    }
    if (name == "http_отправить") {
        if (arguments.size() < 2) return std::make_shared<StringValue>("");
        std::string url = evaluate(arguments[0])->toString();
        std::string body = evaluate(arguments[1])->toString();
        std::string method = arguments.size() >= 3 ? evaluate(arguments[2])->toString() : "POST";
        std::string token = arguments.size() >= 4 ? evaluate(arguments[3])->toString() : "";
        return std::make_shared<StringValue>(net_request(url, &body, method, token));
    }
    if (name == "http_скачать") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string url = evaluate(arguments[0])->toString();
        std::string savePath = evaluate(arguments[1])->toString();
        try {
            std::string data = net_request(url, nullptr, "GET", "");
            std::ofstream f = open_write_utf8(savePath, false);
            if (!f.is_open()) return std::make_shared<BoolValue>(false);
            f.write(data.data(), (std::streamsize)data.size());
            f.close();
            return std::make_shared<BoolValue>(true);
        } catch (...) {
            return std::make_shared<BoolValue>(false);
        }
    }
    if (name == "импорт_из_сети") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string url = evaluate(arguments[0])->toString();
        std::string filename;
        size_t slash = url.find_last_of('/');
        if (slash != std::string::npos) filename = url.substr(slash + 1);
        else filename = "downloaded_module.yamo";
        std::string cacheDir = ".yamo_cache";
        create_dir_utf8(cacheDir);
        std::string localPath = cacheDir + "/" + filename;
        if (!path_exists_utf8(localPath)) {
            std::vector<std::shared_ptr<ASTNode>> dlArgs;
            dlArgs.push_back(std::make_shared<StringExpr>(url));
            dlArgs.push_back(std::make_shared<StringExpr>(localPath));
            auto result = callFunction("http_скачать", dlArgs);
            if (!result->toBool()) {
                remove_path_utf8(localPath);
                throw std::runtime_error("Не удалось скачать модуль: " + url);
            }
        }
        importModule(localPath);
        return std::make_shared<BoolValue>(true);
    }
    if (name == "переменная_среды") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        std::string n = evaluate(arguments[0])->toString();
#ifdef _WIN32
        std::wstring wname = utf8_to_wide(n);
        DWORD len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
        if (len == 0) return std::make_shared<StringValue>("");
        std::wstring wval(len, L'\0');
        GetEnvironmentVariableW(wname.c_str(), &wval[0], len);
        wval.resize(len - 1);
        return std::make_shared<StringValue>(wide_to_utf8(wval));
#else
        const char* v = std::getenv(n.c_str());
        return std::make_shared<StringValue>(v ? std::string(v) : std::string(""));
#endif
    }

    // === База данных ===
    if (name == "бд_открыть") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string path = evaluate(arguments[0])->toString();
        if (db) { sqlite3_close(db); db = nullptr; }
        int rc = sqlite3_open(path.c_str(), &db);
        if (rc != SQLITE_OK) { if (db) { sqlite3_close(db); db = nullptr; } return std::make_shared<BoolValue>(false); }
        dbOpen = true; return std::make_shared<BoolValue>(true);
    }
    if (name == "бд_закрыть") {
        if (db) { sqlite3_close(db); db = nullptr; dbOpen = false; }
        return std::make_shared<BoolValue>(true);
    }
    if (name == "бд_запрос") {
        if (!db || arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string sql = evaluate(arguments[0])->toString();
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) { if (errMsg) sqlite3_free(errMsg); return std::make_shared<BoolValue>(false); }
        return std::make_shared<BoolValue>(true);
    }
    if (name == "бд_выбрать") {
        if (!db || arguments.empty()) return std::make_shared<ArrayValue>();
        std::string sql = evaluate(arguments[0])->toString();
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::make_shared<ArrayValue>();
        auto result = std::make_shared<ArrayValue>();
        int cols = sqlite3_column_count(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto row = std::make_shared<ArrayValue>();
            for (int i = 0; i < cols; i++) {
                const unsigned char* val = sqlite3_column_text(stmt, i);
                row->elements.push_back(std::make_shared<StringValue>(val ? (const char*)val : ""));
            }
            result->elements.push_back(row);
        }
        sqlite3_finalize(stmt); return result;
    }

    // === GUI (Windows — окна, Unix — консоль) ===
    if (name == "сообщение") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string text = evaluate(arguments[0])->toString();
#ifdef _WIN32
        MessageBoxW(NULL, utf8_to_wide(text).c_str(), L"ЯМО", MB_OK | MB_ICONINFORMATION);
#else
        std::cout << "[ОКНО] " << text << std::endl;
#endif
        return std::make_shared<BoolValue>(true);
    }
    if (name == "сообщение_с_заголовком") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string title = evaluate(arguments[0])->toString();
        std::string text = evaluate(arguments[1])->toString();
#ifdef _WIN32
        MessageBoxW(NULL, utf8_to_wide(text).c_str(), utf8_to_wide(title).c_str(), MB_OK | MB_ICONINFORMATION);
#else
        std::cout << "[" << title << "] " << text << std::endl;
#endif
        return std::make_shared<BoolValue>(true);
    }
    if (name == "вопрос") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string text = evaluate(arguments[0])->toString();
#ifdef _WIN32
        int r = MessageBoxW(NULL, utf8_to_wide(text).c_str(), L"ЯМО", MB_YESNO | MB_ICONQUESTION);
        return std::make_shared<BoolValue>(r == IDYES);
#else
        std::cout << text << " (д/н): " << std::flush;
        std::string ans; std::getline(std::cin, ans);
        return std::make_shared<BoolValue>(ans == "д" || ans == "да" || ans == "y" || ans == "yes");
#endif
    }
    if (name == "ввод_окно") {
        if (arguments.size() < 2) return std::make_shared<StringValue>("");
        std::string title = evaluate(arguments[0])->toString();
        std::string prompt = evaluate(arguments[1])->toString();
#ifdef _WIN32
        std::wstring wResult;
        bool ok = yamoInputDialog(utf8_to_wide(title), utf8_to_wide(prompt), wResult);
        if (!ok) return std::make_shared<StringValue>("");
        return std::make_shared<StringValue>(wide_to_utf8(wResult));
#else
        std::cout << "[" << title << "] " << prompt << " " << std::flush;
        std::string ans; std::getline(std::cin, ans);
        return std::make_shared<StringValue>(ans);
#endif
    }

        // === Браузер / DOM ===
    if (name == "браузер_показать") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string html = evaluate(arguments[0])->toString();

        // Гарантируем UTF-8, чтобы кириллица не ломалась
        std::string lower = html;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("charset") == std::string::npos) {
            std::string tag = "<meta charset=\"utf-8\">\n";
            size_t headPos = lower.find("<head>");
            if (headPos != std::string::npos)
                html = html.substr(0, headPos + 6) + "\n" + tag + html.substr(headPos + 6);
            else
                html = tag + html;
        }

        // Пишем во временный файл с BOM
        auto tempPath = std::filesystem::temp_directory_path() / "yamo_page.html";
        std::ofstream f(tempPath, std::ios::binary);
        if (!f.is_open()) return std::make_shared<BoolValue>(false);
        f << "\xEF\xBB\xBF";   // UTF-8 BOM
        f << html;
        f.close();

#ifdef _WIN32
        HINSTANCE r = ShellExecuteW(NULL, L"open", utf8_to_wide(tempPath.string()).c_str(),
                                    NULL, NULL, SW_SHOWNORMAL);
        return std::make_shared<BoolValue>((intptr_t)r > 32);
#else
        std::string cmd = "xdg-open \"" + tempPath.string() + "\" >/dev/null 2>&1 &";
        return std::make_shared<BoolValue>(system(cmd.c_str()) == 0);
#endif
    }

    if (name == "браузер_файл") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string path = evaluate(arguments[0])->toString();
#ifdef _WIN32
        HINSTANCE r = ShellExecuteW(NULL, L"open", utf8_to_wide(path).c_str(),
                                    NULL, NULL, SW_SHOWNORMAL);
        return std::make_shared<BoolValue>((intptr_t)r > 32);
#else
        std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
        return std::make_shared<BoolValue>(system(cmd.c_str()) == 0);
#endif
    }

    // === JSON ===
    if (name == "json_разобрать") {
        if (arguments.empty()) return std::make_shared<NullValue>();
        std::string text = evaluate(arguments[0])->toString();
        if (text.size() >= 3 && (unsigned char)text[0]==0xEF && (unsigned char)text[1]==0xBB && (unsigned char)text[2]==0xBF) text = text.substr(3);
        try { JsonParser p(text); return p.parse(); }
        catch (const std::exception& e) { throw std::runtime_error(std::string("Ошибка JSON: ") + e.what()); }
    }
    if (name == "json_собрать") {
        if (arguments.empty()) return std::make_shared<StringValue>("null");
        return std::make_shared<StringValue>(json_serialize(evaluate(arguments[0]).get()));
    }
    if (name == "ключи") {
        auto a = std::make_shared<ArrayValue>();
        if (!arguments.empty()) {
            if (auto m = std::dynamic_pointer_cast<MapValue>(evaluate(arguments[0])))
                for (auto& k : m->order) a->elements.push_back(std::make_shared<StringValue>(k));
        }
        return a;
    }
    if (name == "значения") {
        auto a = std::make_shared<ArrayValue>();
        if (!arguments.empty()) {
            if (auto m = std::dynamic_pointer_cast<MapValue>(evaluate(arguments[0])))
                for (auto& k : m->order) a->elements.push_back(m->pairs.at(k));
        }
        return a;
    }
    if (name == "содержит_ключ") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        auto m = std::dynamic_pointer_cast<MapValue>(evaluate(arguments[0]));
        if (!m) return std::make_shared<BoolValue>(false);
        return std::make_shared<BoolValue>(m->pairs.count(evaluate(arguments[1])->toString()) > 0);
    }

    // === Математические примитивы (реализация в yamomath.h) ===
    if (name == "синус_рад")   { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::sinus_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "косинус_рад") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::cosinus_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "тангенс_рад") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::tangens_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "котангенс_рад") { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::cotangens_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "секанс_рад")    { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::secans_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "косеканс_рад")  { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::cosecans_rad(evaluate(arguments[0])->toNumber())); }
    if (name == "арксинус")      { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::arcsinus(evaluate(arguments[0])->toNumber())); }
    if (name == "арккосинус")    { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::arccosinus(evaluate(arguments[0])->toNumber())); }
    if (name == "арктангенс")    { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::arctangens(evaluate(arguments[0])->toNumber())); }
    if (name == "логарифм")    { if (arguments.empty()) return std::make_shared<NumberValue>(0.0); return std::make_shared<NumberValue>(yamo_math::logarithm(evaluate(arguments[0])->toNumber())); }
    if (name == "экспонента")  { if (arguments.empty()) return std::make_shared<NumberValue>(1.0); return std::make_shared<NumberValue>(yamo_math::exponens(evaluate(arguments[0])->toNumber())); }
    if (name == "в_текст")     { if (arguments.empty()) return std::make_shared<StringValue>(""); return std::make_shared<StringValue>(evaluate(arguments[0])->toString()); }
    if (name == "добавить") {
        if (arguments.size() < 2) return std::make_shared<NumberValue>(0.0);
        auto arr = std::dynamic_pointer_cast<ArrayValue>(evaluate(arguments[0]));
        if (!arr) throw std::runtime_error("добавить: первый аргумент должен быть массивом");
        arr->elements.push_back(evaluate(arguments[1]));
        return std::make_shared<NumberValue>((double)arr->elements.size());
    }

    // ============================================================
    //  Браузер / DOM — только чистый ЯМО (без JS наружу)
    // ============================================================
    if (name == "ждать") {
        if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
        int ms = (int)evaluate(arguments[0])->toNumber();
        yamo_browser::pump(ms);   // прокачка сообщений: окно остаётся живым
        return std::make_shared<NumberValue>(0.0);
    }

    // --- Навигация ---
    if (name == "браузер_открыть") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        return std::make_shared<BoolValue>(yamo_browser::openUrl(evaluate(arguments[0])->toString()));
    }
    if (name == "браузер_страница") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        return std::make_shared<BoolValue>(yamo_browser::openHtml(evaluate(arguments[0])->toString()));
    }
    if (name == "браузер_назад")    { yamo_browser::runJs("history.back()");    return std::make_shared<BoolValue>(true); }
    if (name == "браузер_вперёд")   { yamo_browser::runJs("history.forward()"); return std::make_shared<BoolValue>(true); }
    if (name == "браузер_обновить") { yamo_browser::runJs("location.reload()"); return std::make_shared<BoolValue>(true); }
    if (name == "браузер_закрыть")  { yamo_browser::closeWindow();              return std::make_shared<BoolValue>(true); }

    // --- Чтение ---
    if (name == "браузер_текст") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) + "');return e?e.textContent:'';})()";
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs(js)));
    }
    if (name == "браузер_значение") {
        if (arguments.empty()) return std::make_shared<StringValue>("");
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) + "');return e?e.value:'';})()";
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs(js)));
    }
    if (name == "браузер_атрибут") {
        if (arguments.size() < 2) return std::make_shared<StringValue>("");
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');return e?(e.getAttribute('" + js_escape(evaluate(arguments[1])->toString()) + "')||''):'';})()";
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs(js)));
    }
    if (name == "браузер_стиль") {
        if (arguments.size() < 2) return std::make_shared<StringValue>("");
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');return e?getComputedStyle(e).getPropertyValue('" + js_escape(evaluate(arguments[1])->toString()) + "')||'':'';})()";
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs(js)));
    }
    if (name == "браузер_заголовок") {
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs("document.title")));
    }
        if (name == "браузер_хтмл") {
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs("document.documentElement.outerHTML")));
    }
    if (name == "браузер_адрес") {
        return std::make_shared<StringValue>(unwrapJsonString(yamo_browser::runJs("location.href")));
    }
    if (name == "браузер_существует") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){return document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) + "')!==null;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_количество") {
        if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
        std::string js = "document.querySelectorAll('" + js_escape(evaluate(arguments[0])->toString()) + "').length";
        std::string r = yamo_browser::runJs(js);
        try { return std::make_shared<NumberValue>(std::stod(r)); } catch (...) { return std::make_shared<NumberValue>(0.0); }
    }

    // --- Запись ---
    if (name == "браузер_установить_текст") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.textContent='" + js_escape(evaluate(arguments[1])->toString()) + "';return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_установить_значение") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.value='" + js_escape(evaluate(arguments[1])->toString()) + "';return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_установить_атрибут") {
        if (arguments.size() < 3) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.setAttribute('" + js_escape(evaluate(arguments[1])->toString()) + "','" +
                        js_escape(evaluate(arguments[2])->toString()) + "');return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_установить_стиль") {
        if (arguments.size() < 3) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.style.setProperty('" + js_escape(evaluate(arguments[1])->toString()) + "','" +
                        js_escape(evaluate(arguments[2])->toString()) + "');return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_добавить_класс") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.classList.add('" + js_escape(evaluate(arguments[1])->toString()) + "');return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_удалить_класс") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.classList.remove('" + js_escape(evaluate(arguments[1])->toString()) + "');return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_установить_заголовок") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "document.title='" + js_escape(evaluate(arguments[0])->toString()) + "'";
        yamo_browser::runJs(js);
        return std::make_shared<BoolValue>(true);
    }

    // --- Действия ---
    if (name == "браузер_нажать") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.click();return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_фокус") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.focus();return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_очистить") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.value='';return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }

    // --- Структура ---
    if (name == "браузер_вставить_хтмл") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.innerHTML='" + js_escape(evaluate(arguments[1])->toString()) + "';return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_добавить_хтмл") {
        if (arguments.size() < 2) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.insertAdjacentHTML('beforeend','" + js_escape(evaluate(arguments[1])->toString()) + "');return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }
    if (name == "браузер_удалить_элемент") {
        if (arguments.empty()) return std::make_shared<BoolValue>(false);
        std::string js = "(function(){var e=document.querySelector('" + js_escape(evaluate(arguments[0])->toString()) +
                        "');if(e){e.remove();return true;}return false;})()";
        return std::make_shared<BoolValue>(yamo_browser::runJs(js) == "true");
    }

    // === Мета-программирование (шаг к самохостингу) ===
    if (name == "выполнить") {
    if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
    std::string code = evaluate(arguments[0])->toString();
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto program = parser.parseProgram();
    if (program) {
        for (const auto& stmt : program->statements) {
            if (auto f = std::dynamic_pointer_cast<FunctionDecl>(stmt)) functions[f->name] = f;
            if (auto cl = std::dynamic_pointer_cast<ClassDecl>(stmt)) classes[cl->name] = cl;
        }
        executeBlock(program->statements);
    }
    return std::make_shared<NumberValue>(0.0);
}
if (name == "вычислить") {
    if (arguments.empty()) return std::make_shared<NumberValue>(0.0);
    std::string code = "функция __tmp__() { вернуть (" + evaluate(arguments[0])->toString() + "); }";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto program = parser.parseProgram();
    if (program) {
        for (const auto& stmt : program->statements) {
            if (auto f = std::dynamic_pointer_cast<FunctionDecl>(stmt)) functions[f->name] = f;
        }
        return callFunction("__tmp__", {});
    }
    return std::make_shared<NumberValue>(0.0);
}
    // Пользовательские функции
    if (functions.find(name) == functions.end()) throw std::runtime_error("Функция '" + name + "' не найдена");
    auto func = functions[name];
    if (arguments.size() != func->params.size()) throw std::runtime_error("Неверное кол-во аргументов для '" + name + "'");
    auto env = std::make_shared<Environment>(environment);
    for (size_t i = 0; i < func->params.size(); i++) env->define(func->params[i].name, evaluate(arguments[i]));
    auto prev = environment; environment = env;
    std::shared_ptr<Value> res = std::make_shared<NumberValue>(0.0);
     try { executeBlock(func->body); }
    catch (const ReturnException& e) { res = e.value ? e.value : std::make_shared<NumberValue>(0.0); }
    catch (const std::exception& e) {
         environment = prev; throw;
     }
     environment = prev; return res;
}

// ------------------------------------------------------------
//  callMethod
// ------------------------------------------------------------
std::shared_ptr<Value> Interpreter::callMethod(std::shared_ptr<ObjectValue> obj, const std::string& method, const std::vector<std::shared_ptr<ASTNode>>& arguments) {
    for (auto& c : obj->chain) {
        for (auto& m : c->methods) {
            if (m->name == method) {
                if (arguments.size() != m->params.size())
                    throw std::runtime_error("Неверное кол-во аргументов для метода '" + method + "'");
                auto env = std::make_shared<Environment>(environment);
                env->define("этот", obj);
                for (size_t i = 0; i < m->params.size(); i++) env->define(m->params[i].name, evaluate(arguments[i]));
                auto prev = environment; environment = env;
                std::shared_ptr<Value> res = std::make_shared<NumberValue>(0.0);
                try { executeBlock(m->body); }
                catch (const ReturnException& e) { res = e.value ? e.value : std::make_shared<NumberValue>(0.0); }
                catch (const std::exception& e) {
         environment = prev; throw;
     }
     environment = prev; return res;
            }
        }
    }
    throw std::runtime_error("Метод '" + method + "' не найден в классе '" + obj->className + "'");
}