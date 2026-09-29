$ErrorActionPreference = "Stop"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Administrator permission is required. Opening the Windows UAC prompt..."
    try {
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
        $elevated = Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        exit $elevated.ExitCode
    } catch {
        Write-Error "Administrator permission was not granted."
        exit 5
    }
}

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

    Write-Host "Myanglish IME uninstalled successfully." -ForegroundColor Green
    exit 0
} catch {
    Write-Error $_
    exit 1
}
