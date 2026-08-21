# Alpha 0.10.8.3R1.4a — All Loanwords

Development branch based on the confirmed R1.3a stable line.

## New behavior

- Loan words: first Space previews Myanmar candidate #1.
- Second Space restores and commits the original English spelling, then inserts one ASCII space.
- Normal Myanglish words keep the existing R1.3a candidate-cycle behavior.

## Loanword coverage

`imported_lexicon_alpha1083_pack2.csv` rows tagged `source=loanword` are treated as real candidate entries.

Coverage verification for this build:

- Unique tagged loanword rows: 279
- Loaded/generated candidate rows: 279
- Missing: 0

## Added mappings

- `Blouk` → `ဘယ်လောက်` / `ဘလောက်`
- `khonn` → `ခွန်း`
- `Sayer` → `ဆရာ`
- `ayr` → `အရာ`
- `hnr` → `ဏှာ`
- `kyg` → `ကြောင့်`
- `htann` → `ဌာန်း`

## Myanmar digits

The IME continues to emit official Unicode Myanmar digits U+1040–U+1049: `၀၁၂၃၄၅၆၇၈၉`.

## Protected stable behavior

R1.3a behavior is intentionally preserved for Backspace, Left/Right auto-commit, unknown-word + Space, personal dictionary Add/manage, candidate indicator behavior, symbols, and existing mappings.

## Build status

The preceding R1.4 build completed with 148 tests passed and 0 failed before registration. R1.4a adds complete loanword candidate loading on top of that line.

## Package checksum

R1.4a package SHA-256:

`40180379f83e658de2f18908bf00a204b85a6dc35f80524bad487d86686a3d67`

Branch: `alpha1083r14a-all-loanwords`
