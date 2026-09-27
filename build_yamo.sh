#!/bin/bash
set -e

echo "Building YAMO for Unix (Linux/macOS)..."

# 1. Compile SQLite using C compiler (CRITICAL FIX)
echo "[1/2] Compiling SQLite3 (using cc)..."
cc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c src/sqlite3.c -o src/sqlite3.o

# 2. Compile YAMO Core using C++ compiler
echo "[2/2] Compiling YAMO Core (using g++)..."
g++ -O2 -std=c++17 \
    src/main.cpp \
    src/lexer.cpp \
    src/parser.cpp \
    src/interpreter.cpp \
    src/all_stubs.cpp \
    src/sqlite3.o \
    -o yamo_unix \
    -lpthread -lm

echo ""
echo "✅ Build successful!"
echo "Run with: ./yamo_unix your_script.yamo"