# Run once as Administrator during the first Myanglish setup.
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $MyInvocation.MyCommand.Path
$root=Join-Path $env:ProgramData 'Myanglish'
New-Item -ItemType Directory -Force -Path $root | Out-Null
Copy-Item (Join-Path $source 'MyanglishUpdater.ps1') (Join-Path $root 'MyanglishUpdater.ps1') -Force

$taskName='Myanglish Auto Update'
$ps="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$script=Join-Path $root 'MyanglishUpdater.ps1'
$action=New-ScheduledTaskAction -Execute $ps -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$script`""
$trigger1=New-ScheduledTaskTrigger -AtStartup
$trigger2=New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(5) -RepetitionInterval (New-TimeSpan -Minutes 30)
$settings=New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
$principal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger @($trigger1,$trigger2) -Settings $settings -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host 'Myanglish background auto-update installed.' -ForegroundColor Green
