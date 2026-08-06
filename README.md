# Myanglish IME

Myanglish IME is a Windows project that will eventually become a system-wide Burmese input method. Stage 1 is a console prototype that converts Myanglish into Burmese Unicode using a CSV dictionary and a separate conversion engine.

## Stage 1 Included Here

- CSV dictionary loading
- Myanglish-to-Burmese sentence conversion
- Longest-phrase matching
- Candidate suggestions
- Automated tests
- UTF-8 console startup for Windows

## File Guide

- `CMakeLists.txt`: builds the conversion engine, the console prototype, and the test executable.
- `data/dictionary.csv`: sample conversion dictionary with at least 30 entries.
- `engine/UnicodeUtils.h` and `engine/UnicodeUtils.cpp`: UTF-8-safe string helpers for trimming, splitting, and joining input.
- `engine/Dictionary.h` and `engine/Dictionary.cpp`: loads the CSV file and stores lookup data.
- `engine/MyanglishConverter.h` and `engine/MyanglishConverter.cpp`: converts sentences and returns ranked candidates.
- `prototype/main.cpp`: console app that loads the dictionary, converts input, and shows candidates for single-word input.
- `tests/ConverterTests.cpp`: self-contained automated tests.
- `tests/test_data.csv`: smaller dictionary used by tests.
- `ime/`, `settings/`, `installer/`, `docs/`: reserved for later stages.

## Build on Windows 11

These commands assume you are using the Developer Command Prompt for Visual Studio, or a VS Code terminal that already has `cmake` and a C++ compiler available.

If CMake reports that it is using `NMake Makefiles` or cannot find `nmake`, delete the existing `build/` directory before reconfiguring so CMake can pick up the requested generator.

1. Configure the project with Visual Studio 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

2. Build the console prototype and tests:

```powershell
cmake --build build --config Debug
```

3. Run the console prototype:

```powershell
.\build\Debug\MyanglishPrototype.exe
```

4. Run the automated tests:

```powershell
.\build\Debug\MyanglishTests.exe
```

## Open in VS Code

```powershell
code .
```

If `code` is not recognized, open the folder from VS Code using `File > Open Folder`.

## Suggested VS Code Extensions

- CMake Tools
- C/C++
- C++ Test Explorer or your preferred test runner helper

## UTF-8 Terminal Setup on Windows

If Burmese text does not display correctly, use a modern terminal like Windows Terminal or the VS Code terminal, then run:

```powershell
chcp 65001
```

The prototype also switches the console code pages to UTF-8 at startup. If your font does not support Myanmar Unicode, switch the terminal font to a font with Myanmar coverage.

## Behavior in Stage 1

- `mingalar par` converts to `မင်္ဂလာပါ`
- `kyay zu tin par tal` converts to `ကျေးဇူးတင်ပါတယ်`
- `sa` shows ranked candidate choices
- Unknown words are preserved instead of being deleted
- Uppercase and lowercase Myanglish are treated the same

## Next Stage

After Stage 1 passes, the next step will be a Windows Text Services Framework IME built on the same conversion engine.
