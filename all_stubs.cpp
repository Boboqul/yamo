#include <string>
#include <vector>
#include <memory>

// --- ЗАГЛУШКИ ДЛЯ CURL ---
// Объявляем типы как void*, чтобы не тянуть заголовки curl
typedef void CURL;
typedef struct curl_slist { char *data; struct curl_slist *next; } curl_slist;

extern "C" {
    CURL* curl_easy_init() { return nullptr; }
    int curl_easy_setopt(CURL *handle, int option, ...) { return 0; }
    int curl_easy_perform(CURL *handle) { return 0; }
    void curl_easy_cleanup(CURL *handle) {}
    const char* curl_easy_strerror(int errornum) { return "Stubbed Curl Error"; }
    curl_slist* curl_slist_append(curl_slist *list, const char *string) { return list; }
    void curl_slist_free_all(curl_slist *chunk) {}
}

// Функция из interpreter.cpp, которую мы хотим заменить
// Предполагаем сигнатуру из ошибок линковки
std::string curl_request(const std::string& url, const std::string* headers, const std::string& body, const std::string& method) {
    return ""; // Возвращаем пустую строку вместо реального запроса
}

// --- ЗАГЛУШКИ ДЛЯ БРАУЗЕРА ---
namespace yamo_browser {
    void pump(int ms) {}
    void openUrl(const std::string& url) {}
    void openHtml(const std::string& html) {}
    void runJs(const std::string& js) {}
    void closeWindow() {}
}
