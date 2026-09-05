# Myanglish Store TSF clean test (v1.0.1)

This branch tests a Store/MSIX-specific TSF registration path without calling
`regsvr32` from the package and without writing the COM CLSID registry key from
application code.

## Intended flow

1. MSIX installs `MyanglishIME.dll` as an in-process COM server using
   `windows.comServer` with `desktop7:CompatMode="classic"` and user scope.
2. Normal launch of `MyanglishSettings.exe` registers the TSF profile with
   `ITfInputProcessorProfileMgr::RegisterProfile`.
3. TSF categories are registered.
4. `InstallLayoutOrTip` enables the profile for the current user.
5. `Win + Space` should show Myanglish and the IME DLL should be loaded by TSF.

No US substitute keyboard layout is registered in this Store path.

## Build

```powershell
cd C:\Projects\myanglish

git fetch origin
git switch store-tsf-clean-v1.0.1
git pull

Remove-Item .\build-store-clean-v1.0.1 -Recurse -Force -ErrorAction SilentlyContinue
cmake -S . -B .\build-store-clean-v1.0.1 -A x64
cmake --build .\build-store-clean-v1.0.1 --config Release

Test-Path .\build-store-clean-v1.0.1\Release\MyanglishSettings.exe
Test-Path .\build-store-clean-v1.0.1\Release\MyanglishIME.dll
```

Both `Test-Path` commands must return `True`.

## Prepare the existing Store staging folder

The commands below reuse the existing `store-msix-stage1\Assets` folder.

```powershell
Copy-Item .\packaging\store\AppxManifest.xml .\store-msix-stage1\AppxManifest.xml -Force
Copy-Item .\build-store-clean-v1.0.1\Release\MyanglishSettings.exe .\store-msix-stage1\MyanglishSettings.exe -Force
Copy-Item .\build-store-clean-v1.0.1\Release\MyanglishIME.dll .\store-msix-stage1\MyanglishIME.dll -Force

Remove-Item .\store-msix-stage1\data -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item .\data .\store-msix-stage1\data -Recurse -Force
```

## Pack and sign for local testing

```powershell
$makeappx = Get-ChildItem `
  "C:\Program Files (x86)\Windows Kits\10\bin" `
  -Recurse -Filter makeappx.exe |
  Where-Object { $_.FullName -match '\\x64\\makeappx\.exe$' } |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName

$msix = "C:\Projects\myanglish\dist\Myanglish-v1.0.1-Store-ClassicTSF-x64.msix"
Remove-Item $msix -Force -ErrorAction SilentlyContinue

& $makeappx pack `
  /d "C:\Projects\myanglish\store-msix-stage1" `
  /p $msix /o

$signtool = Get-ChildItem `
  "C:\Program Files (x86)\Windows Kits\10\bin" `
  -Recurse -Filter signtool.exe |
  Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName

& $signtool sign `
  /fd SHA256 `
  /sha1 89B8C8F50B62758367945951A87E66B502B33873 `
  /s My `
  $msix
```

For local testing of the custom `classicAppCompat` capability, use a Windows
Developer Mode test machine.

## Clean test

First remove the old standalone R1.14 registration correctly through its
`DllUnregisterServer` so that it cannot make the Store package look successful.

```powershell
$stableDll = "$env:LOCALAPPDATA\MyanglishIME\R1.14-ShiftCapsLock-Test\MyanglishIME.dll"
Start-Process regsvr32.exe -Verb RunAs -Wait -ArgumentList "/u `"$stableDll`""

Stop-Process -Name ctfmon -Force -ErrorAction SilentlyContinue
Start-Process "$env:windir\System32\ctfmon.exe"
```

Confirm Myanglish is gone from `Win + Space` before installing the test MSIX.
Then:

```powershell
Get-AppxPackage MAUNGKAUNG.Myanglish | Remove-AppxPackage
Add-AppxPackage -Path $msix

explorer.exe "shell:AppsFolder\MAUNGKAUNG.Myanglish_3exj47kn9m28y!Myanglish"

Stop-Process -Name ctfmon -Force -ErrorAction SilentlyContinue
Start-Process "$env:windir\System32\ctfmon.exe"
```

Test:

- `Win + Space` -> Myanglish
- Notepad: `mingalar` + Space -> Myanmar output
- CapsLock language switching
- Shift+CapsLock capital-lock behavior
- candidate cycling and Enter commit

If bootstrap fails, inspect:

```powershell
Get-Content "$env:LOCALAPPDATA\MyanglishIME\store-tsf-bootstrap.log" -Tail 50
```

## Restore the stable R1.14 runtime after the experiment

```powershell
$stableDll = "$env:LOCALAPPDATA\MyanglishIME\R1.14-ShiftCapsLock-Test\MyanglishIME.dll"
Start-Process regsvr32.exe -Verb RunAs -Wait -ArgumentList "`"$stableDll`""

Stop-Process -Name ctfmon -Force -ErrorAction SilentlyContinue
Start-Process "$env:windir\System32\ctfmon.exe"
```
