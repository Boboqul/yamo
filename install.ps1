$ErrorActionPreference = "Stop"
$src = Split-Path -Parent $MyInvocation.MyCommand.Path
$dst = Join-Path $env:LOCALAPPDATA "YAMO"
New-Item -ItemType Directory -Path $dst -Force | Out-Null
Copy-Item (Join-Path $src "*") $dst -Recurse -Force
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$dst*") { [Environment]::SetEnvironmentVariable("Path", "$userPath;$dst", "User") }
$exe = Join-Path $dst "yamo.exe"
New-Item -Path "HKCU:\Software\Classes\.yamo" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\.yamo" -Name "(default)" -Value "YAMO.Script"
New-Item -Path "HKCU:\Software\Classes\YAMO.Script" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\YAMO.Script" -Name "(default)" -Value "YAMO Script"
New-Item -Path "HKCU:\Software\Classes\YAMO.Script\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\YAMO.Script\shell\open\command" -Name "(default)" -Value "`"$exe`" `"%1`" pause"
Write-Host "YAMO installed to $dst"
