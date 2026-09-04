# Convenience build for a Windows developer with MinGW g++ on the path.
# The supported build is CMake (see README.md); use this only for a quick local binary.
# It never passes -march=native, which is the flag the CMake build guards against on MinGW.

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
