# MyanglishIME Alpha 0.1

This directory contains the first minimal TSF text service for the Myanglish converter.

Goals for this milestone:

- register a TSF text service DLL
- capture ASCII typing in TSF-aware apps such as Notepad
- keep an internal UTF-8 Latin composition buffer
- convert with the existing Myanglish engine on Space
- commit the highest-ranked Burmese candidate
- support Backspace, Enter, Escape, and Shift+Space mode toggle

## Development data location

The IME first looks for data beside the DLL:

- `MyanglishIME.dll`
- `data/dictionary.csv`
- `data/rules/*.csv`

If that is not present, it falls back to:

- `%ProgramData%/MyanglishIME/data`

For the current source-tree build, the engine still supports the existing compile-time source directory fallback used by the console app and tests.

## Registration

The DLL exports `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, and `DllUnregisterServer`.

Registration is per-user friendly and reversible. It writes the COM class registration under `HKCU\Software\Classes` and registers the TSF profile and keyboard category through the TSF profile manager.

## Debug logging

Optional debug logging can be enabled at build time with `MYANGLISHIME_ENABLE_DEBUG_LOG`.