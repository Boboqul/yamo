#pragma once
#include "sqlite3.h"
#include "parser.h"
#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_set>

struct ErrorException { std::shared_ptr<class Value> value; };
class Value { public: virtual ~Value() = default; virtual std::string toString() const = 0; virtual double toNumber() const = 0; virtual bool toBool() const = 0; };
class NumberValue : public Value { public: double value; NumberValue(double v) : value(v) {} std::string toString() const override { if (value == (int)value) return std::to_string((int)value); return std::to_string(value); } double toNumber() const override { return value; } bool toBool() const override { return value != 0; } };
class StringValue : public Value { public: std::string value; StringValue(const std::string& v) : value(v) {} std::string toString() const override { return value; } double toNumber() const override { try { return std::stod(value); } catch (...) { return 0.0; } } bool toBool() const override { return !value.empty(); } };
class BoolValue : public Value { public: bool value; BoolValue(bool v) : value(v) {} std::string toString() const override { return value ? "истина" : "ложь"; } double toNumber() const override { return value ? 1.0 : 0.0; } bool toBool() const override { return value; } };
class ArrayValue : public Value { public: std::vector<std::shared_ptr<Value>> elements; ArrayValue() {} std::string toString() const override { return "[массив]"; } double toNumber() const override { return 0.0; } bool toBool() const override { return !elements.empty(); } };
class NullValue : public Value {
public:
    std::string toString() const override { return "null"; }
    double toNumber() const override { return 0.0; }
    bool toBool() const override { return false; }
};
class MapValue : public Value {
public:
    std::vector<std::string> order;
    std::unordered_map<std::string, std::shared_ptr<Value>> pairs;
    void set(const std::string& k, std::shared_ptr<Value> v) {
        if (!pairs.count(k)) order.push_back(k);
        pairs[k] = v;
    }
    std::string toString() const override;  // определён в interpreter.cpp
    double toNumber() const override { return 0.0; }
    bool toBool() const override { return !pairs.empty(); }
};
class Environment;
class ClassValue : public Value {
public:
    std::string name;
    std::shared_ptr<ClassDecl> classDecl;
    ClassValue(const std::string& n, std::shared_ptr<ClassDecl> cd) : name(n), classDecl(cd) {}
    std::string toString() const override { return "[класс " + name + "]"; }
    double toNumber() const override { return 0.0; }
    bool toBool() const override { return true; }
};
class ObjectValue : public Value {
public:
    std::string className;
    std::unordered_map<std::string, std::shared_ptr<Value>> fields;
    std::shared_ptr<ClassDecl> classDecl;
    std::vector<std::shared_ptr<ClassDecl>> chain;  // от производного к базовому
    ObjectValue(const std::string& cn, std::shared_ptr<ClassDecl> cd) : className(cn), classDecl(cd) {}
    std::string toString() const override { return "[объект " + className + "]"; }
    double toNumber() const override { return 0.0; }
    bool toBool() const override { return true; }
};
class Interpreter {
    friend int main(int argc, char* argv[]);
    std::unordered_set<std::string> importedModules;
    void importModule(const std::string& path);
    std::string currentDir;
    void executeTryCatch(std::shared_ptr<struct TryCatchStmt> tc);
    sqlite3* db = nullptr;
    bool dbOpen = false;
private:
    std::shared_ptr<Environment> environment;
    std::unordered_map<std::string, std::shared_ptr<FunctionDecl>> functions;
    std::unordered_map<std::string, std::shared_ptr<ClassDecl>> classes;
    std::shared_ptr<Value> evaluate(std::shared_ptr<ASTNode> node);
    void execute(std::shared_ptr<ASTNode> node);
    void executeBlock(const std::vector<std::shared_ptr<ASTNode>>& block);
    void executeBlockWithEnv(const std::vector<std::shared_ptr<ASTNode>>& block, std::shared_ptr<Environment> env);
    std::shared_ptr<Value> callFunction(const std::string& name, const std::vector<std::shared_ptr<ASTNode>>& arguments);
    std::shared_ptr<Value> callMethod(std::shared_ptr<ObjectValue> obj, const std::string& method, const std::vector<std::shared_ptr<ASTNode>>& arguments);
public:
    void interpret(std::shared_ptr<Program> program);
};