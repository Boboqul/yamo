#include "parser.h"
#include <stdexcept>
#include <iostream>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), current(0) {}
Token& Parser::peek() { if (current >= tokens.size()) { static Token eof = {YTokenType::EOF_TOKEN, "", 0}; return eof; } return tokens[current]; }
Token& Parser::advance() { Token& prev = peek(); if (!isAtEnd()) current++; return prev; }
bool Parser::isAtEnd() { return current >= tokens.size() || peek().type == YTokenType::EOF_TOKEN; }
bool Parser::check(YTokenType type) { return !isAtEnd() && peek().type == type; }
bool Parser::match(YTokenType type) { if (check(type)) { advance(); return true; } return false; }
Token& Parser::consume(YTokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(peek(), message);
    throw std::runtime_error(message);
}
void Parser::error(const Token& token, const std::string& message) { std::cerr << "Ошибка [строка " << token.line << "]: " << message << "\n"; }
void Parser::synchronize() { advance(); while (!isAtEnd()) { if (peek().type == YTokenType::SEMICOLON) { advance(); return; } if (peek().type == YTokenType::KW_FUNKCIYA || peek().type == YTokenType::KW_ESLI || peek().type == YTokenType::KW_DLYA || peek().type == YTokenType::KW_POKA || peek().type == YTokenType::KW_CLASS) return; advance(); } }

std::shared_ptr<Program> Parser::parseProgram() {
    auto program = std::make_shared<Program>();
    while (!isAtEnd()) { auto stmt = declaration(); if (stmt) program->statements.push_back(stmt); }
    return program;
}
std::shared_ptr<ASTNode> Parser::parse() { return declaration(); }
std::shared_ptr<ASTNode> Parser::declaration() {
    if (check(YTokenType::KW_FUNKCIYA)) { advance(); return functionDeclaration(); }
    if (check(YTokenType::KW_CLASS)) { advance(); return classDeclaration(); }
    if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) || check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE)) return varDeclaration();
    if (check(YTokenType::IDENTIFIER) && current + 1 < tokens.size() && tokens[current + 1].type == YTokenType::IDENTIFIER) return varDeclaration();
    return statement();
}
std::shared_ptr<ASTNode> Parser::functionDeclaration() {
    Token nameToken = advance(); auto func = std::make_shared<FunctionDecl>(nameToken.value);
    if (!match(YTokenType::L_PAREN)) error(peek(), "Ожидается '('");
    if (!check(YTokenType::R_PAREN)) {
        do {
            Param param;
            if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) || check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE)) {
                param.type = advance().value;
                while (match(YTokenType::L_BRACKET)) { if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'"); param.type += "[]"; }
            } else if (check(YTokenType::IDENTIFIER)) {
                param.type = advance().value;   // словарь, массив или имя класса
                while (match(YTokenType::L_BRACKET)) { if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'"); param.type += "[]"; }
            } else { error(peek(), "Ожидается тип"); param.type = "неизвестный"; }
            if (check(YTokenType::IDENTIFIER)) param.name = advance().value;
            else { error(peek(), "Ожидается имя"); param.name = "неизвестно"; }
            func->params.push_back(param);
        } while (match(YTokenType::COMMA));
    }
    match(YTokenType::R_PAREN);
    if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
    while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto stmt = declaration(); if (stmt) func->body.push_back(stmt); else advance(); }
    match(YTokenType::R_BRACE); return func;
}
std::shared_ptr<ASTNode> Parser::varDeclaration() {
    Token typeToken = advance(); std::string typeName = typeToken.value;
    bool isArray = false;
    while (match(YTokenType::L_BRACKET)) { 
        if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'"); 
        typeName += "[]";   // накапливаем размерность прямо в типе
        isArray = true; 
    }
    Token nameToken = advance(); std::shared_ptr<ASTNode> value = nullptr;
    if (match(YTokenType::EQUALS)) value = expression();
    match(YTokenType::SEMICOLON); return std::make_shared<VarDeclaration>(typeName, nameToken.value, value, isArray);
}

std::shared_ptr<ClassDecl> Parser::classDeclaration() {
    std::string className = consume(YTokenType::IDENTIFIER, "Ожидается имя класса").value;
    std::string parentName = "";
    if (match(YTokenType::KW_EXTENDS) || match(YTokenType::COLON)) {
        parentName = consume(YTokenType::IDENTIFIER, "Имя родительского класса").value;
    }
    consume(YTokenType::L_BRACE, "Ожидается '{'");
    
    std::vector<std::shared_ptr<FieldDecl>> fields;
    std::vector<std::shared_ptr<MethodDecl>> methods;
    
    while (!check(YTokenType::R_BRACE) && !isAtEnd()) {
        // Поле: тип имя;
        if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) ||
            check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE)) {
            std::string type = advance().value;
            // Поля тоже могут быть массивами: целое[] поле;
            while (match(YTokenType::L_BRACKET)) {
                if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'");
                type += "[]";
            }
            if (check(YTokenType::IDENTIFIER)) {
                std::string name = advance().value;
                if (check(YTokenType::SEMICOLON)) {
                    advance();
                    fields.push_back(std::make_shared<FieldDecl>(type, name));
                    continue;
                }
                if (check(YTokenType::L_PAREN)) {
                    // Метод с типом возврата (обрабатываем как обычный метод)
                    advance(); // (
                    std::vector<Param> params;
                    if (!check(YTokenType::R_PAREN)) {
                        do {
                            Param p;
                            if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) || check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE)) {
                                p.type = advance().value;
                                while (match(YTokenType::L_BRACKET)) {
                                    if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'");
                                    p.type += "[]";
                                }
                            } else p.type = "неизвестный";
                            if (check(YTokenType::IDENTIFIER)) p.name = advance().value;
                            else p.name = "неизвестно";
                            params.push_back(p);
                        } while (match(YTokenType::COMMA));
                    }
                    consume(YTokenType::R_PAREN, "Ожидается ')'");
                    consume(YTokenType::L_BRACE, "Ожидается '{'");
                    std::vector<std::shared_ptr<ASTNode>> body;
                    while (!check(YTokenType::R_BRACE) && !isAtEnd()) {
                        auto s = declaration();
                        if (s) body.push_back(s); else advance();
                    }
                    consume(YTokenType::R_BRACE, "Ожидается '}'");
                    methods.push_back(std::make_shared<MethodDecl>(name, params, body));
                    continue;
                }
            }
        }
        
        // Метод: "функция" имя(параметры) { ... }
        if (match(YTokenType::KW_FUNKCIYA)) {
            std::string mname = consume(YTokenType::IDENTIFIER, "Имя метода").value;
            consume(YTokenType::L_PAREN, "Ожидается '('");
            std::vector<Param> params;
            if (!check(YTokenType::R_PAREN)) {
                do {
                    Param p;
                    if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) || check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE))
                        p.type = advance().value;
                    else p.type = "неизвестный";
                    if (check(YTokenType::IDENTIFIER)) p.name = advance().value;
                    else p.name = "неизвестно";
                    params.push_back(p);
                } while (match(YTokenType::COMMA));
            }
            consume(YTokenType::R_PAREN, "Ожидается ')'");
            consume(YTokenType::L_BRACE, "Ожидается '{'");
            std::vector<std::shared_ptr<ASTNode>> body;
            while (!check(YTokenType::R_BRACE) && !isAtEnd()) {
                auto s = declaration();
                if (s) body.push_back(s); else advance();
            }
            consume(YTokenType::R_BRACE, "Ожидается '}'");
            methods.push_back(std::make_shared<MethodDecl>(mname, params, body));
            continue;
        }
        throw std::runtime_error("Неожиданный токен в классе: " + peek().value);
    }
    
    consume(YTokenType::R_BRACE, "Ожидается '}'");
    auto cls = std::make_shared<ClassDecl>(className, fields, methods);
    cls->parentName = parentName;
    return cls;
}

std::shared_ptr<ASTNode> Parser::statement() {
    auto tok = peek();
    
    if (match(YTokenType::KW_VERNUT)) {
        return returnStatement();
    }
    if (match(YTokenType::TYPE_TSELOE) || match(YTokenType::TYPE_DROBNOE) || match(YTokenType::TYPE_TEKST) || match(YTokenType::TYPE_LOGICHESKOE)) {
        return varDeclaration();
    }
    if (check(YTokenType::IDENTIFIER) || check(YTokenType::KW_THIS)) {
        if (current + 1 < tokens.size() && tokens[current + 1].type == YTokenType::L_PAREN) return expressionStatement();
        auto s = assignment();
        match(YTokenType::SEMICOLON);
        return s;
    }
    if (match(YTokenType::KW_VYVOD)) return printStatement();
    if (match(YTokenType::KW_PISHI)) return printNoNewlineStatement();
    if (match(YTokenType::KW_ESLI)) return ifStatement();
    if (match(YTokenType::KW_DLYA)) return forStatement();
    if (match(YTokenType::KW_POKA)) return whileStatement();
    if (match(YTokenType::KW_ERROR)) {
        auto v = expression();
        match(YTokenType::SEMICOLON);
        return std::make_shared<ErrorStmt>(v);
    }
    if (match(YTokenType::KW_TRY)) {
        auto tc = std::make_shared<TryCatchStmt>();
        consume(YTokenType::L_BRACE, "Ожидается '{' после 'попробовать'");
        while (!check(YTokenType::R_BRACE) && !isAtEnd()) {
            auto s = declaration();
            if (s) tc->tryBody.push_back(s); else advance();
        }
        consume(YTokenType::R_BRACE, "Ожидается '}'");
        if (match(YTokenType::KW_CATCH)) {
            consume(YTokenType::L_PAREN, "Ожидается '('");
            // тип необязателен: принимаем и "поймать (e)", и "поймать (текст e)"
            if (current + 1 < tokens.size() &&
                tokens[current + 1].type == YTokenType::IDENTIFIER) advance();
            tc->errorName = consume(YTokenType::IDENTIFIER, "Имя переменной ошибки").value;
            consume(YTokenType::R_PAREN, "Ожидается ')'");
            consume(YTokenType::L_BRACE, "Ожидается '{'");
            while (!check(YTokenType::R_BRACE) && !isAtEnd()) {
                auto s = declaration();
                if (s) tc->catchBody.push_back(s); else advance();
            }
            consume(YTokenType::R_BRACE, "Ожидается '}'");
        }
        return tc;
    }
    if (match(YTokenType::KW_IMPORT)) {
        auto path = consume(YTokenType::STRING, "Путь к модулю").value;
        match(YTokenType::SEMICOLON);
        return std::make_shared<ImportStmt>(path);
    }
    if (match(YTokenType::KW_VERNUT)) return returnStatement();
    if (match(YTokenType::KW_BREAK)) { match(YTokenType::SEMICOLON); return std::make_shared<BreakStmt>(); }
    if (match(YTokenType::KW_CONTINUE)) { match(YTokenType::SEMICOLON); return std::make_shared<ContinueStmt>(); }
    if (check(YTokenType::IDENTIFIER) || check(YTokenType::KW_THIS)) {
        if (current + 1 < tokens.size() && tokens[current + 1].type == YTokenType::L_PAREN) return expressionStatement();
        auto s = assignment();
        match(YTokenType::SEMICOLON);
        return s;
    }
    if (match(YTokenType::SEMICOLON)) return nullptr;
    advance(); return nullptr;
}
std::shared_ptr<ASTNode> Parser::printStatement() { auto v = expression(); match(YTokenType::SEMICOLON); return std::make_shared<PrintStmt>(v); }
std::shared_ptr<ASTNode> Parser::printNoNewlineStatement() { auto v = expression(); match(YTokenType::SEMICOLON); return std::make_shared<PrintNoNewlineStmt>(v); }
std::shared_ptr<ASTNode> Parser::returnStatement() { std::shared_ptr<ASTNode> v = nullptr; if (!check(YTokenType::SEMICOLON) && !check(YTokenType::R_BRACE)) v = expression(); match(YTokenType::SEMICOLON); return std::make_shared<ReturnStmt>(v); }

std::shared_ptr<ASTNode> Parser::ifStatement() {
    if (!match(YTokenType::L_PAREN)) error(peek(), "Ожидается '('");
    auto cond = expression(); auto ifStmt = std::make_shared<IfStmt>(cond);
    match(YTokenType::R_PAREN);
    if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
    while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto s = declaration(); if (s) ifStmt->thenBranch.push_back(s); else advance(); }
    match(YTokenType::R_BRACE);
    
    IfStmt* currentElse = ifStmt.get();
    while (check(YTokenType::KW_INACHE_ESLI)) {
        match(YTokenType::KW_INACHE_ESLI);
        if (!match(YTokenType::L_PAREN)) error(peek(), "Ожидается '('");
        auto ec = expression(); match(YTokenType::R_PAREN);
        if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
        std::vector<std::shared_ptr<ASTNode>> eb;
        while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto s = declaration(); if (s) eb.push_back(s); else advance(); }
        match(YTokenType::R_BRACE);
        auto elif = std::make_shared<IfStmt>(ec); elif->thenBranch = eb;
        currentElse->elseBranch.push_back(elif);
        currentElse = elif.get();
    }
    
    if (match(YTokenType::KW_INACHE)) {
        if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
        while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto s = declaration(); if (s) currentElse->elseBranch.push_back(s); else advance(); }
        match(YTokenType::R_BRACE);
    }
    return ifStmt;
}

std::shared_ptr<ASTNode> Parser::forStatement() {
    if (!match(YTokenType::L_PAREN)) error(peek(), "Ожидается '('");
    std::shared_ptr<ASTNode> init = nullptr;
    if (!check(YTokenType::SEMICOLON)) {
        if (check(YTokenType::TYPE_TSELOE) || check(YTokenType::TYPE_DROBNOE) || check(YTokenType::TYPE_TEKST) || check(YTokenType::TYPE_LOGICHESKOE)) {
            Token tt = advance(); std::string tn = tt.value;
            while (match(YTokenType::L_BRACKET)) { if (!match(YTokenType::R_BRACKET)) error(peek(), "Ожидается ']'"); }
            Token nt = advance(); std::shared_ptr<ASTNode> v = nullptr;
            if (match(YTokenType::EQUALS)) v = expression();
            init = std::make_shared<VarDeclaration>(tn, nt.value, v, false);
        } else { init = assignment(); }
    }
    match(YTokenType::SEMICOLON);
    std::shared_ptr<ASTNode> cond = nullptr;
    if (!check(YTokenType::SEMICOLON) && !check(YTokenType::R_PAREN)) cond = expression();
    match(YTokenType::SEMICOLON);
    std::shared_ptr<ASTNode> inc = nullptr;
    if (!check(YTokenType::R_PAREN)) inc = assignmentNoSemicolon();
    match(YTokenType::R_PAREN);
    auto forStmt = std::make_shared<ForStmt>(init, cond, inc);
    if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
    while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto s = declaration(); if (s) forStmt->body.push_back(s); else advance(); }
    match(YTokenType::R_BRACE); return forStmt;
}

std::shared_ptr<ASTNode> Parser::whileStatement() {
    if (!match(YTokenType::L_PAREN)) error(peek(), "Ожидается '('");
    auto cond = expression(); match(YTokenType::R_PAREN);
    auto ws = std::make_shared<WhileStmt>(cond);
    if (!match(YTokenType::L_BRACE)) error(peek(), "Ожидается '{'");
    while (!check(YTokenType::R_BRACE) && !isAtEnd()) { auto s = declaration(); if (s) ws->body.push_back(s); else advance(); }
    match(YTokenType::R_BRACE); return ws;
}

std::shared_ptr<ASTNode> Parser::assignment() {
    std::shared_ptr<ASTNode> target;
    std::string baseName;
    bool isThis = false;
    
    if (match(YTokenType::KW_THIS)) {
        target = std::make_shared<ThisExpr>();
        baseName = "этот";
        isThis = true;
    } else {
        Token name = advance();
        baseName = name.value;
        target = std::make_shared<IdentifierExpr>(name.value);
    }
    
    // Цепочки точек: obj.поле, obj.метод(), этот.поле
    while (match(YTokenType::DOT)) {
        std::string member = consume(YTokenType::IDENTIFIER, "Имя поля/метода").value;
        
        if (match(YTokenType::EQUALS)) {
            // obj.поле = значение  ИЛИ  этот.поле = значение
            auto v = expression();
            match(YTokenType::SEMICOLON);
            return std::make_shared<MemberAssignment>(baseName, member, v);
        }
        
        if (check(YTokenType::L_PAREN)) {
            // obj.метод(аргументы)
            advance();
            std::vector<std::shared_ptr<ASTNode>> args;
            if (!check(YTokenType::R_PAREN)) {
                do { args.push_back(expression()); } while (match(YTokenType::COMMA));
            }
            consume(YTokenType::R_PAREN, "Ожидается ')'");
            target = std::make_shared<MethodCall>(target, member, args);
        } else {
            // obj.поле (чтение)
            target = std::make_shared<MemberAccess>(target, member);
        }
    }
    
    // Массивы
    if (check(YTokenType::L_BRACKET)) {
        std::vector<std::shared_ptr<ASTNode>> indices;
        while (match(YTokenType::L_BRACKET)) { indices.push_back(expression()); match(YTokenType::R_BRACKET); }
        if (match(YTokenType::EQUALS)) {
            auto v = expression(); match(YTokenType::SEMICOLON);
            return std::make_shared<ArrayAssignStmt>(baseName, indices, v);
        }
        for (auto& idx : indices) target = std::make_shared<ArrayAccessExpr>(target, idx);
        return target;
    }
    
    // Простое присваивание
    if (!isThis && match(YTokenType::EQUALS)) {
        auto v = expression(); match(YTokenType::SEMICOLON);
        return std::make_shared<Assignment>(baseName, v);
    }
    
    return target;
}

std::shared_ptr<ASTNode> Parser::assignmentNoSemicolon() {
    Token name = advance();
    if (check(YTokenType::L_BRACKET)) {
        std::vector<std::shared_ptr<ASTNode>> indices;
        while (match(YTokenType::L_BRACKET)) { indices.push_back(expression()); match(YTokenType::R_BRACKET); }
        if (match(YTokenType::EQUALS)) { auto v = expression(); return std::make_shared<ArrayAssignStmt>(name.value, indices, v); }
        std::shared_ptr<ASTNode> arr = std::make_shared<IdentifierExpr>(name.value);
        for (auto& idx : indices) arr = std::make_shared<ArrayAccessExpr>(arr, idx);
        return arr;
    }
    if (!match(YTokenType::EQUALS)) return std::make_shared<IdentifierExpr>(name.value);
    auto v = expression();
    return std::make_shared<Assignment>(name.value, v);
}

std::shared_ptr<ASTNode> Parser::expressionStatement() { auto e = expression(); match(YTokenType::SEMICOLON); return e; }
std::shared_ptr<ASTNode> Parser::expression() { return logicalOr(); }
std::shared_ptr<ASTNode> Parser::logicalOr() { auto l = logicalAnd(); while (match(YTokenType::KW_OR)) { auto r = logicalAnd(); l = std::make_shared<LogicalExpr>("или", l, r); } return l; }
std::shared_ptr<ASTNode> Parser::logicalAnd() { auto l = equality(); while (match(YTokenType::KW_AND)) { auto r = equality(); l = std::make_shared<LogicalExpr>("и", l, r); } return l; }
std::shared_ptr<ASTNode> Parser::equality() { auto l = comparison(); while (match(YTokenType::EQ_EQ) || match(YTokenType::BANG_EQUAL)) { std::string op = tokens[current - 1].value; auto r = comparison(); l = std::make_shared<BinaryExpr>(op, l, r); } return l; }
std::shared_ptr<ASTNode> Parser::comparison() { auto l = term(); while (match(YTokenType::LT) || match(YTokenType::GT) || match(YTokenType::LTE) || match(YTokenType::GTE)) { std::string op = tokens[current - 1].value; auto r = term(); l = std::make_shared<BinaryExpr>(op, l, r); } return l; }
std::shared_ptr<ASTNode> Parser::term() { auto l = factor(); while (match(YTokenType::PLUS) || match(YTokenType::MINUS)) { std::string op = tokens[current - 1].value; auto r = factor(); l = std::make_shared<BinaryExpr>(op, l, r); } return l; }
std::shared_ptr<ASTNode> Parser::factor() { auto l = unary(); while (match(YTokenType::STAR) || match(YTokenType::SLASH) || match(YTokenType::PERCENT)) { std::string op = tokens[current - 1].value; auto r = unary(); l = std::make_shared<BinaryExpr>(op, l, r); } return l; }
std::shared_ptr<ASTNode> Parser::unary() {
    if (match(YTokenType::KW_NOT)) { auto operand = unary(); return std::make_shared<NotExpr>(operand); }
    if (match(YTokenType::MINUS)) {
        auto operand = unary();
        return std::make_shared<BinaryExpr>("-", std::make_shared<NumberExpr>(0.0), operand);
    }
    auto e = primary();
    while (match(YTokenType::L_BRACKET)) { auto idx = expression(); match(YTokenType::R_BRACKET); e = std::make_shared<ArrayAccessExpr>(e, idx); }
    return e;
}

std::shared_ptr<ASTNode> Parser::primary() {
    std::shared_ptr<ASTNode> expr = nullptr;
    
    if (match(YTokenType::KW_NEW)) {
        std::string cn = consume(YTokenType::IDENTIFIER, "Имя класса после 'новый'").value;
        consume(YTokenType::L_PAREN, "Ожидается '('");
        std::vector<std::shared_ptr<ASTNode>> args;
        if (!check(YTokenType::R_PAREN)) {
            do {
                args.push_back(expression());
            } while (match(YTokenType::COMMA));
        }
        consume(YTokenType::R_PAREN, "Ожидается ')'");
        expr = std::make_shared<NewExpr>(cn, args);
    }
    else if (match(YTokenType::KW_THIS)) {
        expr = std::make_shared<ThisExpr>();
    }
    else if (match(YTokenType::KW_VVOD)) {
        // Поддерживаем и "ввод", и "ввод()" — съедаем необязательные скобки
        if (match(YTokenType::L_PAREN)) {
            match(YTokenType::R_PAREN);
        }
        expr = std::make_shared<CallExpr>("ввод", std::vector<std::shared_ptr<ASTNode>>{});
    }
    else if (match(YTokenType::KW_TRUE)) {
        expr = std::make_shared<BoolLiteral>(true);
    }
    else if (match(YTokenType::KW_FALSE)) {
        expr = std::make_shared<BoolLiteral>(false);
    }
    else if (match(YTokenType::NUMBER)) {
        expr = std::make_shared<NumberExpr>(std::stod(tokens[current - 1].value));
    }
    else if (match(YTokenType::STRING)) {
        expr = std::make_shared<StringExpr>(tokens[current - 1].value);
    }
    else if (match(YTokenType::IDENTIFIER)) {
        std::string name = tokens[current - 1].value;
        if (check(YTokenType::L_PAREN)) {
            advance();
            if (name == "длина") { auto a = expression(); match(YTokenType::R_PAREN); expr = std::make_shared<LengthExpr>(a); }
            else {
                std::vector<std::shared_ptr<ASTNode>> args;
                if (!check(YTokenType::R_PAREN)) { do { args.push_back(expression()); } while (match(YTokenType::COMMA)); }
                match(YTokenType::R_PAREN); expr = std::make_shared<CallExpr>(name, args);
            }
        } else {
            expr = std::make_shared<IdentifierExpr>(name);
        }
    }
    else if (match(YTokenType::L_BRACKET)) {
        std::vector<std::shared_ptr<ASTNode>> elems;
        if (!check(YTokenType::R_BRACKET)) { do { elems.push_back(expression()); } while (match(YTokenType::COMMA)); }
        match(YTokenType::R_BRACKET); expr = std::make_shared<ArrayExpr>(elems);
    }
    else if (match(YTokenType::L_PAREN)) {
        expr = expression(); match(YTokenType::R_PAREN);
    }
    else {
        error(peek(), "Ожидается выражение"); advance();
        return std::make_shared<NumberExpr>(0.0);
    }
    
    // Обработка обращений через точку: obj.поле, obj.метод()
    while (match(YTokenType::DOT)) {
        std::string member = consume(YTokenType::IDENTIFIER, "Имя поля/метода после '.'").value;
        if (check(YTokenType::L_PAREN)) {
            advance();
            std::vector<std::shared_ptr<ASTNode>> args;
            if (!check(YTokenType::R_PAREN)) {
                do {
                    args.push_back(expression());
                } while (match(YTokenType::COMMA));
            }
            consume(YTokenType::R_PAREN, "Ожидается ')'");
            expr = std::make_shared<MethodCall>(expr, member, args);
        } else {
            expr = std::make_shared<MemberAccess>(expr, member);
        }
    }
    return expr;
}