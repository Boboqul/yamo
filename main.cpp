#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Использование: yamo.exe программа.ямо" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    
    // 1. Чтение файла
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Ошибка: Не могу открыть файл '" << filename << "'" << std::endl;
        return 1;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source_code = buffer.str();
    file.close();

    try {
        // 2. Лексика
        Lexer lexer(source_code);
        auto tokens = lexer.tokenize(); 
        
        // 3. Парсинг через ПУБЛИЧНЫЙ метод parseProgram()
        Parser parser(tokens);
        auto program = parser.parseProgram();
        
        if (!program) {
            std::cerr << "Ошибка: Парсер вернул пустую программу." << std::endl;
            return 1;
        }

        // 4. Исполнение
        Interpreter interp;
        interp.interpret(program);

    } catch (const std::exception& e) {
        std::cerr << "Критическая ошибка: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Неизвестная ошибка выполнения." << std::endl;
        return 1;
    }

    return 0;
}