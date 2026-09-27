#!/bin/bash
# YAMO Build Script for Linux and macOS (Fixed Version)
# Решает проблему ошибок типов при компиляции sqlite3.c через g++

set -e

echo "=========================================="
echo "   YAMO v2026.09.24 Builder (Unix)"
echo "=========================================="

# Проверка инструментов
if ! command -v g++ &> /dev/null; then
    echo "❌ Error: g++ not found. Install build tools."
    exit 1
fi
if ! command -v cc &> /dev/null; then
    echo "❌ Error: cc (gcc/clang) not found."
    exit 1
fi

# Пути к исходникам
SRC_DIR="./source"
if [ ! -d "$SRC_DIR" ]; then
    SRC_DIR="../source"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "⚠️ Warning: Source directory not found."
    echo "Please ensure .cpp/.h files are in ./source or ../source"
    exit 1
fi

cd "$SRC_DIR"
echo "📂 Working in: $(pwd)"

# 1. КОМПИЛЯЦИЯ SQLITE (ЧЕРЕЗ C-COMPILER!)
# Это критически важно. Не используйте g++ для sqlite3.c
echo "🔨 Compiling SQLite3 (using cc)..."
cc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c sqlite3.c -o sqlite3.o

# 2. КОМПИЛЯЦИЯ ЯМО (ЧЕРЕЗ C++ COMPILER)
echo "🔨 Compiling YAMO Core (using g++)..."
g++ -O2 -std=c++17 \
    main.cpp lexer.cpp parser.cpp interpreter.cpp all_stubs.cpp \
    sqlite3.o \
    -o yamo_unix \
    -lpthread -lm

# 3. ПЕРЕМЕЩЕНИЕ БИНАРНИКА В КОРЕНЬ РЕЛИЗА
mv yamo_unix ../../yamo_unix
cd ../..

echo ""
echo "✅ Success! Binary created: ./yamo_unix"
echo "Run it with: chmod +x yamo_unix && ./yamo_unix your_program.yamo"