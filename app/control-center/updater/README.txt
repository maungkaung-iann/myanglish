MYANGLISH BACKGROUND AUTO UPDATE

Target UX
- First install: one UAC approval.
- Later official GitHub Releases: background download + verified silent setup.
- The scheduled updater runs as SYSTEM, so later updates do not request UAC again.
- Development git pushes do NOT trigger updates. Only the latest stable GitHub Release is considered.
- Release asset name: MyanglishInstaller.exe
- The updater requires a SHA-256 digest from GitHub's release asset metadata or a MyanglishInstaller.exe.sha256 release asset.
- If verification is unavailable or fails, installation is refused.
- Installer is executed with /silent. The existing Myanglish installer already supports /silent.
- User dictionary is outside Program Files at %LOCALAPPDATA%\MyanglishIME\user_dictionary.csv and is not touched by this updater.

Files
- MyanglishUpdater.ps1: background release checker/downloader/installer.
- Install-AutoUpdate.ps1: one-time elevated registration of the Windows Scheduled Task.

Current integration status
This branch contains the working updater layer. The next packaging step is to bundle/copy these updater files from the main Myanglish installer during first install, so users never need to run Install-AutoUpdate.ps1 manually.
