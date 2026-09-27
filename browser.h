#pragma once
#include <string>

namespace yamo_browser {
    void pump(int ms);                      // прокачка сообщений (держит окно живым)
    bool openUrl(const std::string& url);
    bool openHtml(const std::string& html);
    std::string runJs(const std::string& js);
    void closeWindow();
    bool isAvailable();
}