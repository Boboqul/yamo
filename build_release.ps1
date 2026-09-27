$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$ver = "1.0"
$dist = "release\yamo-$ver-win64"
Remove-Item -Recurse -Force "release" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$dist\stdlib", "$dist\examples", "$dist\docs" -Force | Out-Null
if (-not (Test-Path "sqlite3.o")) {
    gcc -c sqlite3.c -o sqlite3.o -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION
}
g++ -static -O2 main.cpp lexer.cpp parser.cpp interpreter.cpp sqlite3.o -o "$dist\yamo.exe" -lwinhttp -luser32 -lgdi32
Copy-Item "stdlib.*" "$dist\stdlib\"
Copy-Item "examples\*" "$dist\examples\"
Copy-Item "docs\*" "$dist\docs\"
Copy-Item "install.ps1", "uninstall.ps1" "$dist\"
Compress-Archive -Path $dist -DestinationPath "release\yamo-$ver-win64.zip" -Force
Write-Host "DONE: release\yamo-$ver-win64.zip"
