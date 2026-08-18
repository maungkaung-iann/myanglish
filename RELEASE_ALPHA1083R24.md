# Myanglish Alpha 0.10.8.3R2.4

Base: Alpha 0.10.8.3R1 Stable Ranked, with the R2.3 inline personal-dictionary work carried forward.

Requested R2.4 changes:

- Fix stale composition behavior around Backspace so old typed text does not reappear.
- While a converted/candidate preview is active, Left/Right/Up/Down commits the visible candidate and passes the arrow key to the host application.
- Physical backslash key: `\` -> `င်္`.
- Shift + physical backslash (`|`) -> `င်`.
- `pyg` ranking: `ပြောင်`, `ပြောင်း`.
- `pygg` ranking: `ပြောင်း`, `ပြောင်`.
- `souk`, `sout`, `sauk`, `sough`, `sought`, and uppercase `S`: `ဆောက်`, `စောက်`, `ဆောင့်`, `စောင့်`.
- Preserve R1 Myanmar digits, symbol rules, candidate ranking/cycling, stack/kinzi behavior, and personal dictionary flow.

Engine regression result for the prepared R2.4 source: 184 passed, 0 failed.

The Windows IME DLL still needs to be built and smoke-tested on Windows before this branch should replace the stable branch.