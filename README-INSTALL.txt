Myanglish Master Rhyme v1 integration

Files:
- engine/MyanglishConverter.cpp
- engine/MyanglishConverter.h
- tests/ConverterTests.cpp
- data/myanglish_rules/rhymes_master_v1.csv

This keeps the legacy data/rules/rhymes.csv and tone_marks.csv as fallbacks.
Master scoring:
- main spelling: 300000
- variant spelling: 200000

Windows build/test from C:\Projects\myanglish:
  cmake -S . -B build-release
  cmake --build build-release --config Release --target MyanglishTests MyanglishIME
  .\build-release\Release\MyanglishTests.exe

Do not delete the old rules files.
