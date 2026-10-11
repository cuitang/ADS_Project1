$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('ads-index-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$executable = Join-Path $scratch 'index_operation_test.exe'
Push-Location $projectRoot
try {
    & gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -I includes src/index/AdsIndex.c tests/index/index_operation_test.c -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & $executable $scratch
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    Write-Output "Test outputs: $scratch"
} finally {
    Pop-Location
}
