$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
New-Item -ItemType Directory -Force -Path $Build | Out-Null
$Tmp = Join-Path $Build "tmp"
New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
$env:TMP = $Tmp
$env:TEMP = $Tmp

$Out = Join-Path $Build "mateprover.exe"
g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -pedantic -pthread "-Wl,--no-insert-timestamp" -o $Out (Join-Path $Root "src\mateprover.cpp")

Write-Host $Out
