# Myanglish Store v1.0.2 install-fix experiment

## Why this branch exists

Microsoft Store certification for v1.0.1 stopped at package installation with error `0x80073CF6` before functional IME testing.

The v1.0.1 manifest used:

- `desktop7:CompatMode="classic"`
- `desktop7:Scope="user"`
- `Microsoft.classicAppCompat_8wekyb3d8bbwe`

This branch removes the classic compatibility custom capability and returns the COM server declaration to normal packaged COM registration.

## Manifest changes

- Version: `1.0.2.0`
- Keep `windows.comServer` / `com4:Extension`
- Use default modern, user-scoped packaged COM behavior
- Remove `desktop7:CompatMode="classic"`
- Remove `desktop7:Scope="user"`
- Remove `Microsoft.classicAppCompat_8wekyb3d8bbwe`
- Keep `runFullTrust`

## Important limitation

This change targets the Store installation failure only. It does **not** yet prove that the complete TSF IME lifecycle works with modern packaged COM.

After package installation succeeds, test all of the following on a clean machine:

1. Launch Myanglish Settings.
2. Confirm TSF profile registration succeeds.
3. Confirm TSF category registration succeeds.
4. Confirm `InstallLayoutOrTip` succeeds.
5. Restart `ctfmon.exe` if required.
6. Confirm Myanglish appears in `Win + Space`.
7. Confirm the IME DLL activates and typing works.
8. Confirm uninstall removes/cleans the profile state appropriately.

## Do not modify system ACLs

Do not change permissions on `HKLM\SOFTWARE\Microsoft\CTF\TIP` as a product fix. The ProcMon result on the development machine is diagnostic evidence, not a Store deployment solution.
