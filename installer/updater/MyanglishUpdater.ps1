param([switch]$Force)
$ErrorActionPreference = 'Stop'
$Repo = 'maungkaung-iann/myanglish'
$Root = Join-Path $env:ProgramData 'Myanglish'
$StatePath = Join-Path $Root 'updater-state.json'
$LogPath = Join-Path $Root 'updater.log'
$ChannelPath = Join-Path $Root 'update-channel.txt'
$UpdateDir = Join-Path $Root 'updates'
New-Item -ItemType Directory -Force -Path $Root,$UpdateDir | Out-Null

function Log([string]$m) {
  Add-Content -Path $LogPath -Encoding UTF8 -Value ("{0:u} {1}" -f (Get-Date),$m)
}
function Read-State {
  if(Test-Path $StatePath){ try { return Get-Content $StatePath -Raw | ConvertFrom-Json } catch {} }
  return [pscustomobject]@{ installedTag=''; lastCheck=''; lastResult='' }
}
function Save-State($s) {
  $s | ConvertTo-Json | Set-Content -Path $StatePath -Encoding UTF8
}
try {
  $channel='stable'
  if(Test-Path $ChannelPath){
    $requested=(Get-Content $ChannelPath -Raw).Trim().ToLowerInvariant()
    if($requested -eq 'test'){$channel='test'}
  }
  $headers=@{'User-Agent'='Myanglish-Updater';'Accept'='application/vnd.github+json'}
  $release=$null
  for($attempt=1;$attempt -le 3;$attempt++){
    try{
      if($channel -eq 'test'){
        $all=@(Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=20" -Headers $headers)
        $release=$all | Where-Object { !$_.draft -and $_.prerelease -and ([string]$_.tag_name).StartsWith('test-') } | Select-Object -First 1
        if(!$release){throw 'No public test prerelease with a test-* tag was found.'}
      }else{
        $release=Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers $headers
      }
      break
    }catch{
      $response=$_.Exception.Response
      $status=if($response){[int]$response.StatusCode}else{0}
      if($status -eq 403 -or $status -eq 429){
        Log "GitHub rate limit response ($status), attempt $attempt/3."
        if($attempt -lt 3){Start-Sleep -Seconds (15*$attempt);continue}
      }
      throw
    }
  }
  if(!$release){throw 'Unable to read latest GitHub Release metadata.'}
  if($release.draft){ throw 'Draft releases are never installed.' }
  if($channel -eq 'stable' -and $release.prerelease){ throw 'Stable channel refused a prerelease.' }
  if($channel -eq 'test' -and !$release.prerelease){ throw 'Test channel requires a prerelease.' }
  $tag=[string]$release.tag_name
  if(!$tag){ throw 'Release tag is missing.' }
  $state=Read-State
  $state.lastCheck=(Get-Date).ToUniversalTime().ToString('o')
  if(!$Force -and $state.installedTag -eq $tag){$state.lastResult='up-to-date';Save-State $state;Log "Up to date [$channel]: $tag";exit 0}

  $asset=$release.assets | Where-Object {$_.name -eq 'MyanglishInstaller.exe'} | Select-Object -First 1
  if(!$asset){ throw "Release $tag has no MyanglishInstaller.exe asset." }
  $expected=''
  if($asset.PSObject.Properties.Name -contains 'digest' -and $asset.digest -match '^sha256:([0-9a-fA-F]{64})$'){$expected=$Matches[1].ToLowerInvariant()}
  if(!$expected){
    $shaAsset=$release.assets | Where-Object {$_.name -eq 'MyanglishInstaller.exe.sha256'} | Select-Object -First 1
    if($shaAsset){
      $shaFile=Join-Path $UpdateDir 'MyanglishInstaller.exe.sha256'
      Invoke-WebRequest -Uri $shaAsset.browser_download_url -Headers $headers -OutFile $shaFile
      $txt=(Get-Content $shaFile -Raw).Trim()
      if($txt -match '([0-9a-fA-F]{64})'){$expected=$Matches[1].ToLowerInvariant()}
    }
  }
  if(!$expected){throw 'No SHA-256 digest is published for this release. Update refused.'}

  $installer=Join-Path $UpdateDir ("MyanglishInstaller-{0}.exe" -f ($tag -replace '[^0-9A-Za-z._-]','_'))
  Invoke-WebRequest -Uri $asset.browser_download_url -Headers $headers -OutFile $installer
  $actual=(Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLowerInvariant()
  if($actual -ne $expected){Remove-Item $installer -Force -ErrorAction SilentlyContinue;throw 'SHA-256 verification failed. Update refused.'}

  Log "Verified [$channel] $tag SHA256=$actual"
  $p=Start-Process -FilePath $installer -ArgumentList '/silent' -Wait -PassThru
  if($p.ExitCode -ne 0){throw "Installer failed with exit code $($p.ExitCode)."}

  $state.installedTag=$tag
  $state.lastResult='installed'
  Save-State $state
  Log "Installed [$channel] $tag successfully."
  Start-Process "$env:windir\System32\ctfmon.exe" -ErrorAction SilentlyContinue
  exit 0
}catch{
  try{$state=Read-State;$state.lastCheck=(Get-Date).ToUniversalTime().ToString('o');$state.lastResult='error: '+$_.Exception.Message;Save-State $state}catch{}
  Log ("ERROR: "+$_.Exception.Message)
  exit 1
}
