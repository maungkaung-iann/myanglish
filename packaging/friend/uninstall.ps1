$ErrorActionPreference = "Stop"

try {
    $installRoot = Join-Path $env:LOCALAPPDATA "MyanglishIME\R1.16"
    $installedDll = Join-Path $installRoot "MyanglishIME.dll"
    $regsvr32 = Join-Path $env:WINDIR "System32\regsvr32.exe"

    if (Test-Path -LiteralPath $installedDll) {
        $unregister = Start-Process $regsvr32 -ArgumentList @("/u", "/s", "`"$installedDll`"") -Wait -PassThru
        if ($unregister.ExitCode -ne 0) {
            throw "Windows unregistration failed with code $($unregister.ExitCode)."
        }
    }

    if (Test-Path -LiteralPath $installRoot) {
        Remove-Item -LiteralPath $installRoot -Recurse -Force
    }

    Write-Host "Myanglish IME R1.16 uninstalled successfully." -ForegroundColor Green
    exit 0
} catch {
    Write-Error $_
    exit 1
}
