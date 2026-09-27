#pragma once
#include "parser.h"
#include <unordered_map>
#include <vector>
#include <string>

class TypeChecker {
    std::vector<std::string> errors;
    std::unordered_map<std::string, std::string> vars;
    std::unordered_map<std::string, std::shared_ptr<ClassDecl>> classes;
    std::unordered_map<std::string, std::shared_ptr<FunctionDecl>> functions;

    void err(const std::string& msg) { errors.push_back("Ошибка типа: " + msg); }

    bool isNumber(const std::string& t) { return t == "целое" || t == "дробное" || t == "число"; }

    std::string classOfType(const std::string& t) {
        const std::string P = "класс:";
        if (t.rfind(P, 0) == 0) return t.substr(P.size());
        return "";
    }

    bool compatible(const std::string& declared, const std::string& actual) {
        if (actual == "неизвестно" || actual == "любой") return true;
        if (declared == "любой") return true;
        if (declared == actual) return true;
        if (isNumber(declared) && isNumber(actual)) return true;
        if (declared.find("[]") != std::string::npos && actual == "массив") return true;
        if (declared == "массив" && actual.find("[]") != std::string::npos) return true;
        if (declared == "массив" && actual == "массив") return true;
        if (classOfType(actual) == declared && !declared.empty()) return true;
        return false;
    }

    std::string builtinType(const std::string& name) {
        // Возвращают текст
        if (name == "ввод" || name == "http_получить" || name == "читать_файл" ||
            name == "верхний_регистр" || name == "нижний_регистр" ||
            name == "заменить" || name == "часть" || name == "повторить" ||
            name == "ввод_окно" || name == "json_собрать" || name == "в_текст" ||
        // Браузер (чтение)
            name == "браузер_текст" || name == "браузер_значение" ||
            name == "браузер_атрибут" || name == "браузер_стиль" ||
            name == "браузер_заголовок" || name == "браузер_хтмл" || 
            name == "браузер_адрес") return "текст";

        // Возвращают логическое
        if (name == "содержит" || name == "начинается_с" || name == "бд_открыть" ||
            name == "бд_закрыть" || name == "бд_запрос" || name == "заканчивается_на" ||
            name == "существует_файл" || name == "http_скачать" || name == "импорт_из_сети" ||
            name == "сообщение" || name == "сообщение_с_заголовком" ||
            name == "вопрос" || name == "содержит_ключ" ||
            name == "браузер_показать" || name == "браузер_файл" ||
        // Браузер (логические действия)
            name == "браузер_открыть" || name == "браузер_страница" ||
            name == "браузер_назад" || name == "браузер_вперёд" || name == "браузер_обновить" ||
            name == "браузер_закрыть" || name == "браузер_существует" ||
            name == "браузер_установить_текст" || name == "браузер_установить_значение" ||
            name == "браузер_установить_атрибут" || name == "браузер_установить_стиль" ||
            name == "браузер_добавить_класс" || name == "браузер_удалить_класс" ||
            name == "браузер_установить_заголовок" || name == "браузер_нажать" ||
            name == "браузер_фокус" || name == "браузер_очистить" ||
            name == "браузер_вставить_хтмл" || name == "браузер_добавить_хтмл" || 
            name == "браузер_удалить_элемент") return "логическое";

        // Возвращают массив
        if (name == "разделить" || name == "бд_выбрать" || name == "ключи" || name == "значения") return "массив";

        // Возвращают число
        if (name == "случайное" || name == "модуль" || name == "округлить" || name == "степень" ||
            name == "корень" || name == "мин" || name == "макс" || name == "найти" ||
            name == "в_число" || name == "длина" || name == "ждать" ||
            name == "запись_файл" || name == "дописать_файл" || name == "удалить_файл" ||
            name == "браузер_количество" ||
            name == "синус_рад" || name == "косинус_рад" || name == "тангенс_рад" ||
            name == "котангенс_рад" || name == "секанс_рад" || name == "косеканс_рад" ||
            name == "логарифм" || name == "экспонента" || name == "пи" || name == "е" ||
            name == "арксинус" || name == "арккосинус" || name == "арктангенс" ||
            name == "добавить") return "число";

        if (name == "json_разобрать") return "неизвестно";
        return "неизвестно";
    }

    std::string typeOf(std::shared_ptr<ASTNode> node) {
        if (!node) return "неизвестно";
        if (auto n = std::dynamic_pointer_cast<NumberExpr>(node))
            return (n->value == (int)n->value) ? "целое" : "дробное";
        if (std::dynamic_pointer_cast<StringExpr>(node)) return "текст";
        if (std::dynamic_pointer_cast<BoolLiteral>(node)) return "логическое";
        if (auto i = std::dynamic_pointer_cast<IdentifierExpr>(node)) {
            auto it = vars.find(i->name);
            return it != vars.end() ? it->second : "неизвестно";
        }
        if (auto a = std::dynamic_pointer_cast<ArrayExpr>(node)) {
            if (a->elements.empty()) return "массив";
            std::string et = typeOf(a->elements[0]);
            if (et == "неизвестно" || et == "любой" || et == "массив") return "массив";
            return et + "[]";
        }
        if (auto ac = std::dynamic_pointer_cast<ArrayAccessExpr>(node)) {
            std::string at = typeOf(ac->array);
            if (at.size() > 2 && at.substr(at.size() - 2) == "[]") return at.substr(0, at.size() - 2);
            return "неизвестно";
        }
        if (std::dynamic_pointer_cast<LengthExpr>(node)) return "целое";
        if (auto l = std::dynamic_pointer_cast<LogicalExpr>(node)) { typeOf(l->left); typeOf(l->right); return "логическое"; }
        if (auto ne = std::dynamic_pointer_cast<NotExpr>(node)) { typeOf(ne->operand); return "логическое"; }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(node)) {
            std::string lt = typeOf(b->left), rt = typeOf(b->right);
            if (b->op == "==" || b->op == "!=" || b->op == "<" || b->op == ">" || b->op == "<=" || b->op == ">=") return "логическое";
            if (b->op == "+") {
                if (lt == "текст" || rt == "текст") return "текст";
                bool ltArr = lt.find("[]") != std::string::npos || lt == "массив";
                bool rtArr = rt.find("[]") != std::string::npos || rt == "массив";
                if (ltArr && rtArr) return lt == "массив" ? rt : lt;
            }
            return "число";
        }
        if (auto c = std::dynamic_pointer_cast<CallExpr>(node)) {
            checkCallArgs(c->callee, c->arguments);
            if (functions.count(c->callee)) return "неизвестно";
            return builtinType(c->callee);
        }
        if (auto n = std::dynamic_pointer_cast<NewExpr>(node)) return "класс:" + n->className;
        if (auto m = std::dynamic_pointer_cast<MemberAccess>(node)) {
            std::string cn = classOfType(typeOf(m->object));
            if (!cn.empty() && classes.count(cn)) {
                for (auto& f : classes[cn]->fields)
                    if (f->name == m->member) return f->type;
            }
            return "неизвестно";
        }
        if (auto m = std::dynamic_pointer_cast<MethodCall>(node)) { for (auto& a : m->args) typeOf(a); return "неизвестно"; }
        if (std::dynamic_pointer_cast<ThisExpr>(node)) {
            auto it = vars.find("этот");
            return it != vars.end() ? it->second : "неизвестно";
        }
        return "неизвестно";
    }

    void checkCallArgs(const std::string& name, const std::vector<std::shared_ptr<ASTNode>>& args) {
        if (!functions.count(name)) { for (auto& a : args) typeOf(a); return; }
        auto f = functions[name];
        if (args.size() != f->params.size()) {
            err("Функция '" + name + "' ожидает " + std::to_string(f->params.size()) +
                " аргументов, а передано " + std::to_string(args.size()));
            return;
        }
        for (size_t i = 0; i < args.size(); i++) {
            std::string at = typeOf(args[i]);
            if (!compatible(f->params[i].type, at))
                err("Аргумент " + std::to_string(i + 1) + " функции '" + name + "': ожидался '" +
                    f->params[i].type + "', получен '" + at + "'");
        }
    }

    void checkStmt(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        if (auto v = std::dynamic_pointer_cast<VarDeclaration>(node)) {
            std::string decl = v->type;
            if (v->value) {
                std::string actual = typeOf(v->value);
                if (!compatible(decl, actual))
                    err("Переменной '" + v->name + "' типа '" + decl + "' присваивается значение типа '" + actual + "'");
            }
            vars[v->name] = decl;
            return;
        }
        if (auto a = std::dynamic_pointer_cast<Assignment>(node)) {
            auto it = vars.find(a->name);
            std::string at = typeOf(a->value);
            if (it != vars.end() && !compatible(it->second, at))
                err("Переменной '" + a->name + "' типа '" + it->second + "' присваивается значение типа '" + at + "'");
            return;
        }
        if (auto ma = std::dynamic_pointer_cast<MemberAssignment>(node)) {
            auto it = vars.find(ma->object);
            std::string at = typeOf(ma->value);
            std::string cn = (it != vars.end()) ? classOfType(it->second) : "";
            if (!cn.empty() && classes.count(cn)) {
                for (auto& f : classes[cn]->fields)
                    if (f->name == ma->member && !compatible(f->type, at))
                        err("Полю '" + ma->member + "' типа '" + f->type + "' присваивается значение типа '" + at + "'");
            }
            return;
        }
        if (auto p = std::dynamic_pointer_cast<PrintStmt>(node)) { typeOf(p->value); return; }
        if (auto p = std::dynamic_pointer_cast<PrintNoNewlineStmt>(node)) { typeOf(p->value); return; }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(node)) {
            typeOf(i->condition);
            for (auto& s : i->thenBranch) checkStmt(s);
            for (auto& s : i->elseBranch) checkStmt(s);
            return;
        }
        if (auto f = std::dynamic_pointer_cast<ForStmt>(node)) {
            if (f->init) checkStmt(f->init);
            if (f->condition) typeOf(f->condition);
            if (f->increment) checkStmt(f->increment);
            for (auto& s : f->body) checkStmt(s);
            return;
        }
        if (auto w = std::dynamic_pointer_cast<WhileStmt>(node)) {
            if (w->condition) typeOf(w->condition);
            for (auto& s : w->body) checkStmt(s);
            return;
        }
        if (auto tc = std::dynamic_pointer_cast<TryCatchStmt>(node)) {
            for (auto& s : tc->tryBody) checkStmt(s);
            if (!tc->errorName.empty()) vars[tc->errorName] = "текст";
            for (auto& s : tc->catchBody) checkStmt(s);
            return;
        }
        if (auto c = std::dynamic_pointer_cast<CallExpr>(node)) { checkCallArgs(c->callee, c->arguments); return; }
        if (auto mc = std::dynamic_pointer_cast<MethodCall>(node)) { typeOf(mc->object); for (auto& a : mc->args) typeOf(a); return; }
    }

public:
    std::vector<std::string> check(std::shared_ptr<Program> program) {
        errors.clear(); vars.clear(); classes.clear(); functions.clear();
        for (auto& s : program->statements) {
            if (auto f = std::dynamic_pointer_cast<FunctionDecl>(s)) functions[f->name] = f;
            if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) classes[c->name] = c;
        }
        for (auto& s : program->statements) {
            if (auto f = std::dynamic_pointer_cast<FunctionDecl>(s)) {
                vars.clear();
                for (auto& p : f->params) vars[p.name] = p.type;
                for (auto& st : f->body) checkStmt(st);
            } else if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
                for (auto& m : c->methods) {
                    vars.clear();
                    vars["этот"] = "класс:" + c->name;
                    for (auto& p : m->params) vars[p.name] = p.type;
                    for (auto& st : m->body) checkStmt(st);
                }
            } else {
                checkStmt(s);
            }
        }
        return errors;
    }
};