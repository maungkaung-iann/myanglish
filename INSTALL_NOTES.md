# Myanglish IME installation notes

## Windows error 0x80070020

Confirmed on 2026-09-21 with the exact `master` build.

If `MyanglishSetup.exe` fails with `HRESULT 0x80070020`, check whether Windows Explorer has loaded the IME DLL:

```powershell
tasklist /m MyanglishIME.dll
```

If the output shows `explorer.exe`, temporarily stop Explorer, run the installer, then restart Explorer:

```powershell
Stop-Process -Name explorer -Force
Start-Sleep -Seconds 2
Start-Process "C:\Myanglish-Master-Exact\MyanglishSetup.exe" -Verb RunAs -Wait
Start-Process explorer.exe
```

The taskbar and desktop may disappear temporarily while Explorer is stopped.

### Confirmed result

The exact GitHub `master` source was built without source changes. Installation initially failed with `0x80070020` because `explorer.exe` had `MyanglishIME.dll` loaded. Stopping Explorer before installation allowed the installer to complete successfully.
