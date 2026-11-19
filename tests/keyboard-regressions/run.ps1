# Compile the complete production keyboard driver against a simulated USB host.
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDir = Join-Path $repoRoot 'dogdog-controller/build/keyboard-regressions'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$clang = (Get-Command clang.exe -ErrorAction SilentlyContinue).Source
if (-not $clang) { $clang = 'C:/Program Files/LLVM/bin/clang.exe' }
if (-not (Test-Path $clang)) { throw 'Install LLVM/Clang or put clang.exe on PATH' }
$exePath = Join-Path $outputDir 'keyboard.exe'
& $clang -ffreestanding -fno-builtin -fno-stack-protector -fuse-ld=lld -nostdlib -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter `
    '-Wl,/entry:test_main,/subsystem:console,/nodefaultlib' `
    '-I' (Join-Path $PSScriptRoot 'stubs') `
    '-I' (Join-Path $repoRoot 'dogdog-controller/main/include') `
    (Join-Path $PSScriptRoot 'keyboard.c') -o $exePath
if ($LASTEXITCODE -ne 0) { throw 'Keyboard regression compilation failed' }
& $exePath
if ($LASTEXITCODE -ne 0) { throw "Keyboard regressions failed, assertion $LASTEXITCODE" }
Write-Output 'Passed: USB keyboard delivery regressions'
