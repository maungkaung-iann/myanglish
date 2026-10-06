$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$port=18463
$prefix="http://127.0.0.1:$port/"
$dictDir=Join-Path $env:LOCALAPPDATA 'MyanglishIME'
$dict=Join-Path $dictDir 'user_dictionary.csv'
New-Item -ItemType Directory -Force -Path $dictDir|Out-Null
if(!(Test-Path $dict)){[IO.File]::WriteAllText($dict,"myanglish,burmese,frequency`r`n",[Text.UTF8Encoding]::new($false))}
function Read-Words{
 $out=@(); $lines=[IO.File]::ReadAllLines($dict,[Text.Encoding]::UTF8)
 for($i=1;$i -lt $lines.Count;$i++){if(!$lines[$i]){continue};$p=$lines[$i].Split(',');if($p.Count -ge 2){$f=if($p.Count-ge 3){$p[2]}else{'2000000'};$prio=if([int64]$f -ge 2500000){'1st'}elseif([int64]$f -ge 1500000){'2nd'}else{'3rd'};$out += [pscustomobject]@{raw=$p[0];burmese=$p[1];frequency=$f;priority=$prio}}};return @($out)
}
function Save-Words($words){$sb=[Text.StringBuilder]::new();[void]$sb.AppendLine('myanglish,burmese,frequency');foreach($w in $words){[void]$sb.AppendLine("$($w.raw),$($w.burmese),$($w.frequency)")};[IO.File]::WriteAllText($dict,$sb.ToString(),[Text.UTF8Encoding]::new($false))}
$listener=[Net.HttpListener]::new();$listener.Prefixes.Add($prefix);$listener.Start()
$edge=(Get-Command msedge.exe -ErrorAction SilentlyContinue).Source
if(!$edge){$edge="${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"}
if(!(Test-Path $edge)){$edge="$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"}
Start-Process $edge -ArgumentList "--app=$prefix","--start-maximized"
Write-Host 'Myanglish V6 is running. Keep this PowerShell window open.' -ForegroundColor Green
Write-Host 'Close this PowerShell window to stop the local app service.'
while($listener.IsListening){
 try{$ctx=$listener.GetContext();$req=$ctx.Request;$res=$ctx.Response;$path=$req.Url.AbsolutePath
  if($path -eq '/api/words'){$json=(Read-Words)|ConvertTo-Json -Compress;$bytes=[Text.Encoding]::UTF8.GetBytes($json);$res.ContentType='application/json; charset=utf-8'}
  elseif($path -eq '/api/add' -and $req.HttpMethod -eq 'POST'){$sr=[IO.StreamReader]::new($req.InputStream,[Text.Encoding]::UTF8);$o=($sr.ReadToEnd()|ConvertFrom-Json);$words=@(Read-Words);$freq=switch([int]$o.priority){1{'3000000'}2{'2000000'}default{'1000000'}};$existing=$words|Where-Object{$_.raw -eq $o.raw -and $_.burmese -eq $o.burmese};if($existing){$existing.frequency=$freq}else{$words += [pscustomobject]@{raw=[string]$o.raw;burmese=[string]$o.burmese;frequency=$freq;priority=''}};Save-Words $words;$bytes=[Text.Encoding]::UTF8.GetBytes('{"ok":true}');$res.ContentType='application/json'}
  elseif($path -eq '/api/delete' -and $req.HttpMethod -eq 'POST'){$sr=[IO.StreamReader]::new($req.InputStream,[Text.Encoding]::UTF8);$o=($sr.ReadToEnd()|ConvertFrom-Json);$words=@(Read-Words);$new=@();for($i=0;$i -lt $words.Count;$i++){if($i-ne[int]$o.index){$new+=$words[$i]}};Save-Words $new;$bytes=[Text.Encoding]::UTF8.GetBytes('{"ok":true}');$res.ContentType='application/json'}
  else{$rel=if($path -eq '/'){'index.html'}else{$path.TrimStart('/')};$file=Join-Path $root $rel;if(!(Test-Path $file)){$res.StatusCode=404;$bytes=[Text.Encoding]::UTF8.GetBytes('Not found')}else{$bytes=[IO.File]::ReadAllBytes($file);$ext=[IO.Path]::GetExtension($file).ToLower();$res.ContentType=switch($ext){'.html'{'text/html; charset=utf-8'}'.css'{'text/css; charset=utf-8'}'.js'{'application/javascript; charset=utf-8'}'.png'{'image/png'}default{'application/octet-stream'}}}}
  $res.ContentLength64=$bytes.Length;$res.OutputStream.Write($bytes,0,$bytes.Length);$res.Close()
 }catch{Write-Host $_.Exception.Message -ForegroundColor DarkGray}
}
