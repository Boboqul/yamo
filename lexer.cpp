#include "lexer.h"
#include <iostream>

Lexer::Lexer(const std::string& source) : source(source), current(0), line(1) {}
char Lexer::peek() { return isAtEnd() ? '\0' : source[current]; }
char Lexer::advance() { char c = source[current++]; if (c == '\n') line++; return c; }
bool Lexer::isAtEnd() { return current >= source.length(); }
bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) return false;
    current++; return true;
}
bool Lexer::isUtf8CyrillicStart(char c) { unsigned char uc = (unsigned char)c; return uc == 0xD0 || uc == 0xD1; }
std::string Lexer::readUtf8Char() {
    std::string result; char first = advance(); result += first;
    unsigned char uc = (unsigned char)first; int extra = 0;
    if ((uc & 0xE0) == 0xC0) extra = 1; else if ((uc & 0xF0) == 0xE0) extra = 2;
    for (int i = 0; i < extra && !isAtEnd(); i++) result += advance();
    return result;
}
void Lexer::skipWhitespace() {
    while (!isAtEnd()) { char c = peek(); if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance(); else break; }
}
void Lexer::skipComment() {
    if (peek() == '/' && current + 1 < source.length() && source[current + 1] == '/') {
        while (!isAtEnd() && peek() != '\n') advance(); return;
    }
    if (peek() == '/' && current + 1 < source.length() && source[current + 1] == '*') {
        advance(); advance();
        while (!isAtEnd()) {
            if (peek() == '*' && current + 1 < source.length() && source[current + 1] == '/') { advance(); advance(); return; }
            advance();
        }
    }
}
Token Lexer::stringToken() {
    advance(); std::string value;
    while (!isAtEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            if (isAtEnd()) break; char next = advance();
            if (next == 'n') value += '\n'; else if (next == 't') value += '\t';
            else if (next == '\\') value += '\\'; else if (next == '"') value += '"';
            else if (next == 'r') value += '\r'; else { value += '\\'; value += next; }
        } else value += c;
    }
    if (!isAtEnd()) advance();
    return {YTokenType::STRING, value, line};
}
Token Lexer::numberToken() {
    std::string value;
    while (!isAtEnd() && ((peek() >= '0' && peek() <= '9') || peek() == '.')) value += advance();
    return {YTokenType::NUMBER, value, line};
}
Token Lexer::identifierToken() {
    std::string value;
    while (!isAtEnd()) {
        char c = peek();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') value += advance();
        else if (isUtf8CyrillicStart(c)) value += readUtf8Char();
        else break;
    }
    if (value == "целое") return {YTokenType::TYPE_TSELOE, value, line};
    if (value == "дробное") return {YTokenType::TYPE_DROBNOE, value, line};
    if (value == "текст") return {YTokenType::TYPE_TEKST, value, line};
    if (value == "логическое") return {YTokenType::TYPE_LOGICHESKOE, value, line};
    if (value == "функция") return {YTokenType::KW_FUNKCIYA, value, line};
    if (value == "вывод") return {YTokenType::KW_VYVOD, value, line};
    if (value == "пиши") return {YTokenType::KW_PISHI, value, line};
    if (value == "если") return {YTokenType::KW_ESLI, value, line};
    if (value == "иначе_если") return {YTokenType::KW_INACHE_ESLI, value, line};
    if (value == "иначе") return {YTokenType::KW_INACHE, value, line};
    if (value == "для") return {YTokenType::KW_DLYA, value, line};
    if (value == "пока") return {YTokenType::KW_POKA, value, line};
    if (value == "вернуть") return {YTokenType::KW_VERNUT, value, line};
    if (value == "ввод") return {YTokenType::KW_VVOD, value, line};
    if (value == "прервать") return {YTokenType::KW_BREAK, value, line};
    if (value == "продолжить") return {YTokenType::KW_CONTINUE, value, line};
    if (value == "класс") return {YTokenType::KW_CLASS, value, line};
    if (value == "новый") return {YTokenType::KW_NEW, value, line};
    if (value == "этот") return {YTokenType::KW_THIS, value, line};
    if (value == "расширяет") return {YTokenType::KW_EXTENDS, value, line};
    if (value == "попробовать") return {YTokenType::KW_TRY, value, line};
    if (value == "поймать") return {YTokenType::KW_CATCH, value, line};
    if (value == "ошибка") return {YTokenType::KW_ERROR, value, line};
    if (value == "импорт") return {YTokenType::KW_IMPORT, value, line};
    if (value == "и") return {YTokenType::KW_AND, value, line};
    if (value == "или") return {YTokenType::KW_OR, value, line};
    if (value == "не") return {YTokenType::KW_NOT, value, line};
    if (value == "истина") return {YTokenType::KW_TRUE, value, line};
    if (value == "ложь") return {YTokenType::KW_FALSE, value, line};
    return {YTokenType::IDENTIFIER, value, line};
}
std::vector<Token> Lexer::tokenize() {
    if (source.length() >= 3 && (unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF) current = 3;
    int tokenCount = 0;
    const int maxTokens = 50000;
    while (!isAtEnd()) {
        skipWhitespace(); if (isAtEnd()) break;
        skipComment(); skipWhitespace(); if (isAtEnd()) break;
        if (tokenCount >= maxTokens) throw std::runtime_error("Превышен лимит токенов (" + std::to_string(maxTokens) + ")");
        char c = peek();
        if (c == '"') { tokens.push_back(stringToken()); tokenCount++; continue; }
        if (c >= '0' && c <= '9') { tokens.push_back(numberToken()); tokenCount++; continue; }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || isUtf8CyrillicStart(c)) { tokens.push_back(identifierToken()); tokenCount++; continue; }
        
        auto push = [&](YTokenType t, std::string v) { tokens.push_back({t, v, line}); };
        switch (c) {
            case '(': push(YTokenType::L_PAREN, "("); advance(); break;
            case ')': push(YTokenType::R_PAREN, ")"); advance(); break;
            case '{': push(YTokenType::L_BRACE, "{"); advance(); break;
            case '}': push(YTokenType::R_BRACE, "}"); advance(); break;
            case '[': push(YTokenType::L_BRACKET, "["); advance(); break;
            case ']': push(YTokenType::R_BRACKET, "]"); advance(); break;
            case ';': push(YTokenType::SEMICOLON, ";"); advance(); break;

            case ':': push(YTokenType::COLON, ":"); advance(); break;            case ',': push(YTokenType::COMMA, ","); advance(); break;
            case '.': push(YTokenType::DOT, "."); advance(); break;
            case '+': push(YTokenType::PLUS, "+"); advance(); break;
            case '-': push(YTokenType::MINUS, "-"); advance(); break;
            case '*': push(YTokenType::STAR, "*"); advance(); break;
            case '/': push(YTokenType::SLASH, "/"); advance(); break;
            case '%': push(YTokenType::PERCENT, "%"); advance(); break;
            case '=':
            advance();
            if (!isAtEnd() && peek() == '=') { advance(); push(YTokenType::EQ_EQ, "=="); }
            else push(YTokenType::EQUALS, "=");
            break;
        case '!':
            advance();
            if (!isAtEnd() && peek() == '=') { advance(); push(YTokenType::BANG_EQUAL, "!="); }
            else std::cerr << "Неизвестный символ: ! в строке " << line << "\n";
            break;
        case '<':
            advance();
            if (!isAtEnd() && peek() == '=') { advance(); push(YTokenType::LTE, "<="); }
            else push(YTokenType::LT, "<");
            break;
        case '>':
            advance();
            if (!isAtEnd() && peek() == '=') { advance(); push(YTokenType::GTE, ">="); }
            else push(YTokenType::GT, ">");
            break;
            default: advance(); break;
        }
        tokenCount++;
    }
    tokens.push_back({YTokenType::EOF_TOKEN, "", line});
    return tokens;
}