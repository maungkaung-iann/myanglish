MYANGLISH CONTROL CENTER V6

1. Double-click run-v5.bat
2. Keep the PowerShell window open while using the Control Center.
3. The UI opens as an Edge app window (no browser tabs/address bar).
4. Add Words writes directly to:
   %LOCALAPPDATA%\MyanglishIME\user_dictionary.csv
5. Switch away from Myanglish IME and back once after dictionary changes.

V6 adds the searchable Features table and expanded About page while preserving the existing IME itself.
The original green Myanglish logo asset is used unchanged.


AUTO UPDATE (V6)
- updater/MyanglishUpdater.ps1 checks the latest stable GitHub Release in the background.
- updater/Install-AutoUpdate.ps1 registers a highest-privilege SYSTEM Scheduled Task during the first elevated setup.
- Later updates can run without another UAC prompt.
- Installer downloads are SHA-256 verified and unverified releases are refused.
- MyanglishInstaller.exe is run with /silent.
- Git pushes alone do not update users; publish an official GitHub Release.
