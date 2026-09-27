// ============================================================
//  main.cpp — точка входа языка ЯМО
//  Режимы: запуск файла .ямо и интерактивная оболочка (REPL)
// ============================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include "typechecker.h"

// ------------------------------------------------------------
//  Вспомогательные функции
// ------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
#endif

// Нужен ли ещё ввод (незакрытые скобки или строка)
static bool needsMoreInput(const std::string& src) {
    int depth = 0;
    bool inStr = false, inComment = false;
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (inComment) { if (c == '\n') inComment = false; continue; }
        if (inStr)     { if (c == '\\') { ++i; continue; } if (c == '"') inStr = false; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') { inComment = true; ++i; continue; }
        if (c == '"')  { inStr = true; continue; }
        if (c == '{') depth++;
        else if (c == '}') depth--;
    }
    return depth > 0 || inStr;
}

static bool replKeywordStart(const std::string& t) {
    static const std::string kws[] = {
        "функция ", "класс ", "если ", "иначе ", "для ", "пока ", "вывод ",
        "вернуть ", "текст ", "целое ", "дробное ", "логическое ", "словарь ",
        "импорт ", "попробовать ", "поймать ", "ошибка ", "прервать ",
        "продолжить ", "новый ", "этот "
    };
    for (const auto& k : kws) if (t.rfind(k, 0) == 0) return true;
    return false;
}

static bool replLooksAssign(const std::string& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '=') {
            char prev = i ? t[i - 1] : '\0';
            char next = i + 1 < t.size() ? t[i + 1] : '\0';
            if (prev != '=' && prev != '<' && prev != '>' && prev != '!' && next != '=')
                return true;
        }
    }
    return false;
}

static bool replIsExprLine(const std::string& t) {
    if (t.empty()) return false;
    if (replKeywordStart(t)) return false;
    char last = t.back();
    if (last == ';' || last == '{' || last == '}') return false;
    if (replLooksAssign(t)) return false;
    return true;
}

// ------------------------------------------------------------
//  Лексер -> парсер -> проверка типов -> интерпретатор
// ------------------------------------------------------------

static bool runSource(const std::string& code, bool printErrors) {
    try {
        Lexer lexer(code);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        std::shared_ptr<Program> program = parser.parseProgram();
        if (!program) return false;
        TypeChecker checker;
        std::vector<std::string> typeErrors = checker.check(program);
        if (!typeErrors.empty()) {
            if (printErrors) {
                std::cout << "=== ОШИБКИ ТИПОВ ===" << std::endl;
                for (const auto& e : typeErrors) std::cout << e << std::endl;
                std::cout << "Программа НЕ запущена: исправь ошибки типов." << std::endl;
            }
            return false;
        }
        Interpreter interpreter;
        interpreter.interpret(program);
        return true;
    } catch (const std::exception& e) {
        if (printErrors) std::cerr << "Критическая ошибка: " << e.what() << std::endl;
        return false;
    }
}

// ------------------------------------------------------------
//  REPL — интерактивная оболочка
// ------------------------------------------------------------

static void runRepl() {
    std::cout << "╔══════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   ЯМО 1.0 — интерактивная оболочка           ║" << std::endl;
    std::cout << "║   помощь — справка, выход — завершить        ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════╝" << std::endl;
    std::cout.flush();

    std::string sessDecls;
    std::string sessVars;
    std::string buffer;

    while (true) {
        std::cout << (buffer.empty() ? "ямо> " : "...  ") << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) { std::cout << "\nПока!" << std::endl; break; }
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (buffer.empty()) {
            std::string t = trim(line);
            if (t == "выход" || t == "exit" || t == "quit") { std::cout << "Пока!" << std::endl; break; }
            if (t == "помощь" || t == "help") {
                std::cout << "  Объявления (функция/класс/переменная) запоминаются между строками." << std::endl;
                std::cout << "  Операторы выполняются сразу. Голое выражение печатает результат." << std::endl;
                std::cout << "  Команды: помощь, выход." << std::endl;
                std::cout.flush();
                continue;
            }
            if (t.empty()) continue;
        }

        buffer += line + "\n";
        if (needsMoreInput(buffer)) continue;

        std::string input = buffer;
        buffer.clear();
        std::string trimmed = trim(input);

        std::shared_ptr<Program> probe;
        {
            std::streambuf* old = std::cout.rdbuf();
            std::ostringstream suppress;
            std::cout.rdbuf(suppress.rdbuf());
            try {
                Lexer lx(input);
                auto toks = lx.tokenize();
                Parser pr(toks);
                probe = pr.parseProgram();
            } catch (...) { probe = nullptr; }
            std::cout.rdbuf(old);
        }

        bool hasMain = false;
        bool allVar = probe && !probe->statements.empty();
        bool allFuncClass = probe && !probe->statements.empty();

        if (probe) {
            for (const auto& st : probe->statements) {
                auto fd = std::dynamic_pointer_cast<FunctionDecl>(st);
                auto cd = std::dynamic_pointer_cast<ClassDecl>(st);
                auto im = std::dynamic_pointer_cast<ImportStmt>(st);
                auto vd = std::dynamic_pointer_cast<VarDeclaration>(st);
                if (fd && fd->name == "главная") hasMain = true;
                bool isFuncClass = (fd != nullptr) || (cd != nullptr) || (im != nullptr);
                bool isVar = (vd != nullptr);
                if (!isVar) allVar = false;
                if (!isFuncClass) allFuncClass = false;
            }
        }

        if (allFuncClass) {
            std::string candidate = sessDecls + input + "\n";
            if (runSource(candidate + "функция главная() {\n}\n", true)) {
                sessDecls = candidate;
                std::cout << "  [определено]" << std::endl;
            }
        } else if (allVar) {
            std::string candidate = sessVars + input + "\n";
            if (runSource(sessDecls + "функция главная() {\n" + candidate + "}\n", true)) {
                sessVars = candidate;
                std::cout << "  [ок]" << std::endl;
            }
        } else if (hasMain) {
            runSource(sessDecls + sessVars + input, true);
        } else if (replIsExprLine(trimmed)) {
            runSource(sessDecls + "функция главная() {\n" + sessVars +
                    "вывод (" + input + ");\n}\n", true);
        } else {
            runSource(sessDecls + "функция главная() {\n" + sessVars + input + "}\n", true);
        }
        std::cout.flush();
    }
}

// ------------------------------------------------------------
//  main
// ------------------------------------------------------------

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    auto w2u = [](const std::wstring& w) -> std::string {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    };
    
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        if (wargc >= 2) { static std::string a1 = w2u(wargv[1]); argv[1] = const_cast<char*>(a1.c_str()); }
        if (wargc >= 3) { static std::string a2 = w2u(wargv[2]); argv[2] = const_cast<char*>(a2.c_str()); }
        LocalFree(wargv);
    }
#endif

    if (argc >= 2) {
        std::string a = argv[1];
        if (a == "--version" || a == "-v") {
#ifdef _WIN32
            std::cout << "ЯМО 1.1 (Windows x64)\n";
#elif defined(__APPLE__)
            std::cout << "ЯМО 1.1 (macOS)\n";
#elif defined(__linux__)
            std::cout << "ЯМО 1.1 (Linux)\n";
#else
            std::cout << "ЯМО 1.1 (неизвестная платформа)\n";
#endif
            return 0; 
        }
        if (a == "--help" || a == "-h") {
            std::cout << "Использование: yamo [файл.ямо] [pause]\n"
                    << "  без аргументов — интерактивная оболочка\n"
                    << "  --version — версия\n";
            return 0;
        }
    }

    if (argc < 2) { runRepl(); return 0; }

    std::string filename = argv[1];
    
#ifdef _WIN32
    std::ifstream file(std::filesystem::path(utf8_to_wide(filename)), std::ios::binary);
#else
    std::ifstream file(filename, std::ios::binary);
#endif
    
    if (!file.is_open()) {
        std::cerr << "Ошибка: не удалось открыть файл '" << filename << "'" << std::endl;
        return 1;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string code = ss.str();

    if (code.size() >= 3 &&
        (unsigned char)code[0] == 0xEF &&
        (unsigned char)code[1] == 0xBB &&
        (unsigned char)code[2] == 0xBF) {
        code.erase(0, 3);
    }

    std::cout << "=== Запуск файла: " << filename << " ===" << std::endl;
    std::cout << std::endl;

    bool ok = runSource(code, true);

    if (ok) {
        std::cout << std::endl;
        std::cout << "=== Программа завершена ===" << std::endl;
    }

    return ok ? 0 : 1;
}