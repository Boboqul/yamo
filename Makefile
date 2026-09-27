# ===========================================================
# Makefile для ЯМО — универсальная сборка (Windows/Linux/macOS)
# ===========================================================

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -std=c++17
CFLAGS   ?= -O2

SRCS     = main.cpp lexer.cpp parser.cpp interpreter.cpp browser.cpp

ifeq ($(OS),Windows_NT)
  TARGET      = yamo.exe
  SQLITE_O    = sqlite3_win.o
  LDLIBS      = -lwinhttp -luser32 -lgdi32 -lole32
  SQLITE_DEFS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION
  RM          = rm -f
else
  TARGET      = yamo
  SQLITE_O    = sqlite3_nix.o
  LDLIBS      = -lcurl -lpthread -ldl -lm
  SQLITE_DEFS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION
  RM          = rm -f
endif

all: $(TARGET)

$(SQLITE_O): sqlite3.c
	$(CC) -c sqlite3.c -o $(SQLITE_O) $(CFLAGS) $(SQLITE_DEFS)

$(TARGET): $(SRCS) $(SQLITE_O)
	$(CXX) $(CXXFLAGS) $(SRCS) $(SQLITE_O) -o $(TARGET) -Wl,--stack,134217728 -Wl,--stack,134217728 $(LDLIBS)

clean:
	-$(RM) $(TARGET) yamo.exe yamo sqlite3.o sqlite3_win.o sqlite3_nix.o

.PHONY: all clean