#pragma once
#include "lexer.h"
#include <memory>
#include <vector>
#include <string>
#include <iostream>

struct ASTNode { virtual ~ASTNode() = default; virtual void print(int indent = 0) const = 0; };
struct NumberExpr : ASTNode { double value; NumberExpr(double v) : value(v) {} void print(int i) const override {} };
struct StringExpr : ASTNode { std::string value; StringExpr(const std::string& v) : value(v) {} void print(int i) const override {} };
struct BoolLiteral : ASTNode { bool value; BoolLiteral(bool v) : value(v) {} void print(int i) const override {} };
struct IdentifierExpr : ASTNode { std::string name; IdentifierExpr(const std::string& n) : name(n) {} void print(int i) const override {} };
struct BinaryExpr : ASTNode { std::string op; std::shared_ptr<ASTNode> left, right; BinaryExpr(const std::string& o, std::shared_ptr<ASTNode> l, std::shared_ptr<ASTNode> r) : op(o), left(l), right(r) {} void print(int i) const override {} };
struct LogicalExpr : ASTNode { std::string op; std::shared_ptr<ASTNode> left, right; LogicalExpr(const std::string& o, std::shared_ptr<ASTNode> l, std::shared_ptr<ASTNode> r) : op(o), left(l), right(r) {} void print(int i) const override {} };
struct NotExpr : ASTNode { std::shared_ptr<ASTNode> operand; NotExpr(std::shared_ptr<ASTNode> o) : operand(o) {} void print(int i) const override {} };
struct CallExpr : ASTNode { std::string callee; std::vector<std::shared_ptr<ASTNode>> arguments; CallExpr(const std::string& c, const std::vector<std::shared_ptr<ASTNode>>& a) : callee(c), arguments(a) {} void print(int i) const override {} };
struct ArrayExpr : ASTNode { std::vector<std::shared_ptr<ASTNode>> elements; ArrayExpr(const std::vector<std::shared_ptr<ASTNode>>& e) : elements(e) {} void print(int i) const override {} };
struct ArrayAccessExpr : ASTNode { std::shared_ptr<ASTNode> array, index; ArrayAccessExpr(std::shared_ptr<ASTNode> a, std::shared_ptr<ASTNode> i) : array(a), index(i) {} void print(int i) const override {} };
struct ArrayAssignStmt : ASTNode {
    std::string arrayName;
    std::vector<std::shared_ptr<ASTNode>> indices;
    std::shared_ptr<ASTNode> value;
    ArrayAssignStmt(const std::string& name, const std::vector<std::shared_ptr<ASTNode>>& idx, std::shared_ptr<ASTNode> v)
        : arrayName(name), indices(idx), value(v) {}
    void print(int i) const override {}
};
struct LengthExpr : ASTNode { std::shared_ptr<ASTNode> value; LengthExpr(std::shared_ptr<ASTNode> v) : value(v) {} void print(int i) const override {} };
struct VarDeclaration : ASTNode {
    std::string type, name;
    std::shared_ptr<ASTNode> value;
    bool isArray;
    VarDeclaration(const std::string& t, const std::string& n, std::shared_ptr<ASTNode> v, bool arr)
        : type(t), name(n), value(v), isArray(arr) {}
    void print(int i) const override {}
};
struct Assignment : ASTNode { std::string name; std::shared_ptr<ASTNode> value; Assignment(const std::string& n, std::shared_ptr<ASTNode> v) : name(n), value(v) {} void print(int i) const override {} };
struct PrintStmt : ASTNode { std::shared_ptr<ASTNode> value; PrintStmt(std::shared_ptr<ASTNode> v) : value(v) {} void print(int i) const override {} };
struct PrintNoNewlineStmt : ASTNode { std::shared_ptr<ASTNode> value; PrintNoNewlineStmt(std::shared_ptr<ASTNode> v) : value(v) {} void print(int i) const override {} };
struct IfStmt : ASTNode { std::shared_ptr<ASTNode> condition; std::vector<std::shared_ptr<ASTNode>> thenBranch, elseBranch; IfStmt(std::shared_ptr<ASTNode> c) : condition(c) {} void print(int i) const override {} };
struct ForStmt : ASTNode { std::shared_ptr<ASTNode> init, condition, increment; std::vector<std::shared_ptr<ASTNode>> body; ForStmt(std::shared_ptr<ASTNode> i, std::shared_ptr<ASTNode> c, std::shared_ptr<ASTNode> inc) : init(i), condition(c), increment(inc) {} void print(int i) const override {} };
struct WhileStmt : ASTNode { std::shared_ptr<ASTNode> condition; std::vector<std::shared_ptr<ASTNode>> body; WhileStmt(std::shared_ptr<ASTNode> c) : condition(c) {} void print(int i) const override {} };
struct Param { std::string type, name; };
struct FunctionDecl : ASTNode { std::string name; std::vector<Param> params; std::vector<std::shared_ptr<ASTNode>> body; FunctionDecl(const std::string& n) : name(n) {} void print(int i) const override {} };
struct ReturnStmt : ASTNode { std::shared_ptr<ASTNode> value; ReturnStmt(std::shared_ptr<ASTNode> v) : value(v) {} void print(int i) const override {} };

struct FieldDecl : ASTNode {
    std::string type, name;
    FieldDecl(const std::string& t, const std::string& n) : type(t), name(n) {}
    void print(int i) const override {}
};
struct MethodDecl : ASTNode {
    std::string name;
    std::vector<Param> params;
    std::vector<std::shared_ptr<ASTNode>> body;
    MethodDecl(const std::string& n, const std::vector<Param>& p, const std::vector<std::shared_ptr<ASTNode>>& b) : name(n), params(p), body(b) {}
    void print(int i) const override {}
};
struct ClassDecl : ASTNode {
    std::string name;
    std::string parentName;
    std::vector<std::shared_ptr<FieldDecl>> fields;
    std::vector<std::shared_ptr<MethodDecl>> methods;
    ClassDecl(const std::string& n, const std::vector<std::shared_ptr<FieldDecl>>& f,
            const std::vector<std::shared_ptr<MethodDecl>>& m) : name(n), fields(f), methods(m) {}
    void print(int i) const override {}
};
struct NewExpr : ASTNode {
    std::string className;
    std::vector<std::shared_ptr<ASTNode>> args;
    NewExpr(const std::string& c, const std::vector<std::shared_ptr<ASTNode>>& a) : className(c), args(a) {}
    void print(int i) const override {}
};
struct MemberAccess : ASTNode {
    std::shared_ptr<ASTNode> object;
    std::string member;
    MemberAccess(std::shared_ptr<ASTNode> o, const std::string& m) : object(o), member(m) {}
    void print(int i) const override {}
};
struct MethodCall : ASTNode {
    std::shared_ptr<ASTNode> object;
    std::string method;
    std::vector<std::shared_ptr<ASTNode>> args;
    MethodCall(std::shared_ptr<ASTNode> o, const std::string& m, const std::vector<std::shared_ptr<ASTNode>>& a)
        : object(o), method(m), args(a) {}
    void print(int i) const override {}
};
struct ThisExpr : ASTNode { void print(int i) const override {} };
struct TryCatchStmt : ASTNode {
    std::vector<std::shared_ptr<ASTNode>> tryBody;
    std::string errorName;
    std::vector<std::shared_ptr<ASTNode>> catchBody;
    TryCatchStmt() : errorName("") {}
    void print(int i) const override {}
};

struct ImportStmt : ASTNode {
    std::string path;
    ImportStmt(const std::string& p) : path(p) {}
    void print(int i) const override {}
};
struct ErrorStmt : ASTNode {
    std::shared_ptr<ASTNode> value;
    ErrorStmt(std::shared_ptr<ASTNode> v) : value(v) {}
    void print(int i) const override {}
};
struct BreakStmt : ASTNode { void print(int i) const override {} };
struct ContinueStmt : ASTNode { void print(int i) const override {} };
struct MemberAssignment : ASTNode {
    std::string object;
    std::string member;
    std::shared_ptr<ASTNode> value;
    MemberAssignment(const std::string& o, const std::string& m, std::shared_ptr<ASTNode> v)
        : object(o), member(m), value(v) {}
    void print(int i) const override {}
};
struct Program : ASTNode { std::vector<std::shared_ptr<ASTNode>> statements; void print(int i) const override {} };

class Parser {
private:
    std::vector<Token> tokens; size_t current;
    Token& peek(); Token& advance(); bool isAtEnd(); bool check(YTokenType type); bool match(YTokenType type);
    Token& consume(YTokenType type, const std::string& message);
    void synchronize();
    std::shared_ptr<ASTNode> parse(); std::shared_ptr<ASTNode> statement(); std::shared_ptr<ASTNode> declaration();
    std::shared_ptr<ASTNode> varDeclaration(); std::shared_ptr<ASTNode> functionDeclaration();
    std::shared_ptr<ClassDecl> classDeclaration();
    std::shared_ptr<ASTNode> ifStatement(); std::shared_ptr<ASTNode> forStatement(); std::shared_ptr<ASTNode> whileStatement();
    std::shared_ptr<ASTNode> printStatement(); std::shared_ptr<ASTNode> printNoNewlineStatement(); std::shared_ptr<ASTNode> returnStatement();
    std::shared_ptr<ASTNode> assignment(); std::shared_ptr<ASTNode> assignmentNoSemicolon();
    std::shared_ptr<ASTNode> expressionStatement(); std::shared_ptr<ASTNode> expression();
    std::shared_ptr<ASTNode> logicalOr(); std::shared_ptr<ASTNode> logicalAnd();
    std::shared_ptr<ASTNode> equality(); std::shared_ptr<ASTNode> comparison();
    std::shared_ptr<ASTNode> term(); std::shared_ptr<ASTNode> factor();
    std::shared_ptr<ASTNode> unary(); std::shared_ptr<ASTNode> primary();
public:
    Parser(const std::vector<Token>& tokens);
    std::shared_ptr<Program> parseProgram();
    void error(const Token& token, const std::string& message);
};