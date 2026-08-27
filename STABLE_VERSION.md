# Myanglish Stable Version

**Stable:** Alpha 0.10.8.3 R1.8a

Status: confirmed working on Windows on 2026-08-27.

## Stable behavior

- Myanglish to Myanmar conversion
- Candidate popup and candidate selection
- Ctrl+Enter Myanmar stack behavior
- `မင်း` + `ဂ` + Ctrl+Enter -> `မင်္ဂ`
- Kinzi/stack state is cleared after the stack operation so `င်္` does not leak into later words
- `kygg` -> `ကြောင့်`

## Source baseline

R1.8a was built from the clean R1.5 base, with the R1.8 stack/kinzi reset fix and the R1.8a PowerShell installer-script syntax fix.

This branch is kept as the rollback point before further development.
