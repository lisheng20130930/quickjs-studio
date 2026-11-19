param(
    [string]$MinGWRoot = $(if ($env:MINGW_ROOT) { $env:MINGW_ROOT } else { "D:\mingw64" })
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot "build-windows.ps1") -MinGWRoot $MinGWRoot

$Gcc = Join-Path $MinGWRoot "bin\gcc.exe"
Push-Location $ProjectRoot
try {
    Write-Host "Building debugger regression test"
    & $Gcc -std=c11 -O0 -g3 -Wall -Wextra `
        -isystem third_party/quickjs tests/test_debugger.c `
        third_party/quickjs/libquickjs_win64.a -static -lm -lwinpthread `
        -o build/windows/bin/test-debugger.exe
    if ($LASTEXITCODE -ne 0) { throw "Debugger test build failed" }
    & .\build\windows\bin\test-debugger.exe
    if ($LASTEXITCODE -ne 0) { throw "Debugger test failed" }
    Write-Host "Building WizardBar symbol regression test"
    & $Gcc -std=c11 -O0 -g3 -Wall -Wextra -Werror `
        -Isrc tests/test_symbol_search.c src/symbol_search.c `
        -o build/windows/bin/test-symbol-search.exe
    if ($LASTEXITCODE -ne 0) { throw "WizardBar symbol test build failed" }
    & .\build\windows\bin\test-symbol-search.exe
    if ($LASTEXITCODE -ne 0) { throw "WizardBar symbol test failed" }
    Write-Host "Building project management regression test"
    & $Gcc -std=c11 -O0 -g3 -Wall -Wextra -Werror `
        -Isrc tests/test_project.c src/project.c `
        -o build/windows/bin/test-project.exe
    if ($LASTEXITCODE -ne 0) { throw "Project management test build failed" }
    & .\build\windows\bin\test-project.exe
    if ($LASTEXITCODE -ne 0) { throw "Project management test failed" }
    Write-Host "Overall result: PASS"
}
finally {
    Pop-Location
}
