#pragma once
#include <string>
#include <vector>

enum class YTokenType {
    TYPE_TSELOE, TYPE_DROBNOE, TYPE_TEKST, TYPE_LOGICHESKOE,
    KW_FUNKCIYA, KW_VYVOD, KW_PISHI, KW_ESLI, KW_INACHE,
    KW_INACHE_ESLI, KW_DLYA, KW_POKA, KW_VERNUT, KW_VVOD, 
    KW_BREAK, KW_CONTINUE, KW_CLASS, KW_NEW, KW_THIS, KW_EXTENDS, 
    KW_TRY, KW_CATCH, KW_ERROR, KW_IMPORT,
    DOT, KW_AND, KW_OR, KW_NOT, KW_TRUE, KW_FALSE,
    IDENTIFIER, NUMBER, STRING,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQUALS, EQ_EQ, BANG_EQUAL, LT, GT, LTE, GTE,
    L_PAREN, R_PAREN, L_BRACE, R_BRACE,
    L_BRACKET, R_BRACKET, SEMICOLON, COMMA,
    EOF_TOKEN,
    KW_KLASS,
    KW_ETOT, COLON,
    KW_NOVYI
};

struct Token { YTokenType type; std::string value; int line; };

class Lexer {
private:
    std::string source;
    std::vector<Token> tokens;
    size_t current;
    int line;
    char peek();
    char advance();
    bool isAtEnd();
    bool match(char expected);
    void skipWhitespace();
    void skipComment();
    Token stringToken();
    Token numberToken();
    Token identifierToken();
    bool isUtf8CyrillicStart(char c);
    std::string readUtf8Char();
public:
    Lexer(const std::string& source);
    std::vector<Token> tokenize();
};