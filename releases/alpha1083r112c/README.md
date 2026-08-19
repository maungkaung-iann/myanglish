# Alpha 0.10.8.3R1.2c

This is the current **confirmed stable** Myanglish Windows IME build.

## Stable lineage

`0.10.8.3R1 Stable Ranked -> R1.1 -> R1.2 -> R1.2c`

The R1 Backspace behavior is the protected baseline. Experimental R2.x state-machine work is not the stable base.

## Confirmed behavior

- Backspace: stable; no past-word resurrection regression.
- Left/Right during preview: auto-commit first, then caret navigation.
- Candidate ranking and Space cycling.
- `+ Add / manage words...` personal-dictionary UI.
- `0..9 -> ၀..၉`.
- Shift-number keys remain normal English-layout symbols.
- Non-custom symbols remain English-layout symbols.
- `\\ -> င်္`.
- `Shift+\\ (|) -> င်`.
- `mhr`: `မှာ`, `မှား`.
- `mhrr`: `မှား`, `မှာ`.
- `pyg`: `ပြောင်`, `ပြောင်း`.
- `pygg`: `ပြောင်း`, `ပြောင်`.
- `souk / sout / sauk / sough / sought / S`: reviewed candidate family `ဆောက် / စောက် / ဆောင့် / စောင့်`.

## Source package checksum

Confirmed R1.2c package SHA-256:

`305b64ca816f0cfa0ef4becdea0c3161ffe52497069210e459b5455bb0238943`

Future changes should branch from this confirmed stable line and must not change Backspace behavior unless explicitly requested and separately smoke-tested.
