$dst = Join-Path $env:LOCALAPPDATA "YAMO"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$userPath = (($userPath -split ";") | Where-Object { $_ -ne $dst }) -join ";"
[Environment]::SetEnvironmentVariable("Path", $userPath, "User")
Remove-Item -Path "HKCU:\Software\Classes\.yamo" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\YAMO.Script" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "YAMO uninstalled."
