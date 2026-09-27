# YAMO Language v2026.09.24 (Native OOP Release)

Cross-platform programming language with native Object-Oriented support.

## Structure
- `yamo.exe`: Windows Binary (x64)
- `yamo-linux-x64`: Linux Binary (x64) - Ready to use!
- `build_yamo.sh`: Script to compile YAMO on Linux/macOS from source.
- `библиотека/`: Contains `стандартная.ямо` (Core Library).
- `документ/`: Detailed instructions.

## How to Use

### On Windows
Just run:
```cmd
.\yamo.exe my_script.yamo
```

### On Linux
The binary is ready! Just make it executable and run:
```bash
chmod +x yamo-linux-x64
./yamo-linux-x64 my_script.yamo
```

### Building from Source (Linux/macOS)
If you need a custom build:
1. Ensure source files are in `./source` folder.
2. Run the builder:
```bash
chmod +x build_yamo.sh
./build_yamo.sh
```
This creates `yamo_unix` which works on both Linux and macOS.

## Features
- **Classes:** `класс Точка { ... }`
- **Inheritance:** `класс Круг : Фигура { ... }`
- **Constructors:** Automatic call to `инициализация`.
- **Self-hosting:** Meta-interpreter included in library.