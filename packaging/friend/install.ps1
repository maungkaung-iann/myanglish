$ErrorActionPreference = "Stop"

# TSF registration writes protected Windows registry entries.
# Relaunch this installer with administrator permission when needed.
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
    $packageRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $sourceDll = Join-Path $packageRoot "MyanglishIME.dll"
    $sourceData = Join-Path $packageRoot "data"

    if (!(Test-Path -LiteralPath $sourceDll)) {
        throw "MyanglishIME.dll is missing from the package."
    }
    if (!(Test-Path -LiteralPath $sourceData)) {
        throw "The data folder is missing from the package."
    }

    $installRoot = Join-Path $env:LOCALAPPDATA "MyanglishIME\R1.16"
    $installedDll = Join-Path $installRoot "MyanglishIME.dll"
    $installedData = Join-Path $installRoot "data"
    $regsvr32 = Join-Path $env:WINDIR "System32\regsvr32.exe"

    if (Test-Path -LiteralPath $installedDll) {
        $unregister = Start-Process $regsvr32 -ArgumentList @("/u", "/s", "`"$installedDll`"") -Wait -PassThru
        if ($unregister.ExitCode -ne 0) {
            Write-Warning "The previous R1.16 registration could not be removed. Continuing with repair install."
        }
    }

    New-Item -ItemType Directory -Force $installRoot | Out-Null
    if (Test-Path -LiteralPath $installedData) {
        Remove-Item -LiteralPath $installedData -Recurse -Force
    }

    Copy-Item -LiteralPath $sourceDll -Destination $installedDll -Force
    Copy-Item -LiteralPath $sourceData -Destination $installedData -Recurse -Force

    $sourceSettings = Join-Path $packageRoot "MyanglishSettings.exe"
    if (Test-Path -LiteralPath $sourceSettings) {
        Copy-Item -LiteralPath $sourceSettings -Destination (Join-Path $installRoot "MyanglishSettings.exe") -Force
    }

    $register = Start-Process $regsvr32 -ArgumentList @("/s", "`"$installedDll`"") -Wait -PassThru
    if ($register.ExitCode -ne 0) {
        throw "Windows registration failed with code $($register.ExitCode)."
    }

    $ctfmon = Join-Path $env:WINDIR "System32\ctfmon.exe"
    if (Test-Path -LiteralPath $ctfmon) {
        Start-Process $ctfmon | Out-Null
    }

    Write-Host ""
    Write-Host "Myanglish IME stable 3a8f496 installed successfully." -ForegroundColor Green
    Write-Host "Press Win + Space and choose Myanglish IME."
    Write-Host "Install folder: $installRoot"
    exit 0
} catch {
    Write-Error $_
    exit 1
}
