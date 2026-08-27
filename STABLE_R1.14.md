# Myanglish Alpha 0.10.8.3 R1.14 — Confirmed Stable

Confirmed working on Windows on 2026-08-28.

## Stable root

R1.14 is based on the confirmed-working R1.12 Stable Root Smart Raw Spacing build.

## Confirmed behavior

- Raw English smart boundary spacing works.
  - `ဒီ` + raw `code` + Space -> `ဒီ code `
  - Existing space before the raw word is preserved without creating a double space.
  - Ctrl+Space uses the same raw-English commit behavior.
- Special Myanmar stack remains supported.
  - `မင်း` + `ဂ` + Ctrl+Enter -> `မင်္ဂ`
- Normal stack is confirmed working.
  - `တက်` + `က` + Ctrl+Enter -> `တက္က`
- Existing candidate, Space, Backspace, loanword, punctuation, digit, Tab, Left/Right and other stable behavior should remain based on the R1.12 root.

## Stable package

Package filename:
`myanglish-alpha1083r114-r112-root-normal-stack-only.zip`

SHA-256:
`d73ce8825a402502e5c1ac7ce9573d9c46edba2ff5c88153806d203f2dcbe7c7`

This branch is the rollback marker for the confirmed R1.14 stable version before further development.
