param(
    [string]$MinGWRoot = $(if ($env:MINGW_ROOT) { $env:MINGW_ROOT } else { "D:\mingw64" })
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $ProjectRoot "build\windows\bin"
$Gcc = Join-Path $MinGWRoot "bin\gcc.exe"
$Gxx = Join-Path $MinGWRoot "bin\g++.exe"
$Ar = Join-Path $MinGWRoot "bin\ar.exe"
$Make = Join-Path $MinGWRoot "bin\mingw32-make.exe"
$Windres = Join-Path $MinGWRoot "bin\windres.exe"

foreach ($Tool in @($Gcc, $Gxx, $Ar, $Make, $Windres)) {
    if (-not (Test-Path $Tool)) {
        throw "Missing MinGW64 tool: $Tool"
    }
}

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
Push-Location $ProjectRoot
try {
    Write-Host "Building QuickJS debugger library"
    $env:MINGW_CC = $Gcc
    $env:MINGW_AR = $Ar
    python .\third_party\quickjs\build_quickjs.py
    if ($LASTEXITCODE -ne 0) { throw "QuickJS build failed" }

    Write-Host "Building Scintilla"
    & $Make -C .\third_party\scintilla\win32 "CXX=$Gxx" "WINDRES=$Windres"
    if ($LASTEXITCODE -ne 0) { throw "Scintilla build failed" }

    Write-Host "Building Lexilla"
    & $Make -C .\third_party\lexilla\src "CXX=$Gxx" "WINDRES=$Windres" `
        "LDFLAGS=-mwindows -Wl,--kill-at -shared -static"
    if ($LASTEXITCODE -ne 0) { throw "Lexilla build failed" }

    Write-Host "Building quickjs-studio.exe"
    $StudioArgs = @(
        "-std=c11", "-O0", "-g3", "-Wall", "-Wextra", "-Werror",
        "-D_WIN32_WINNT=0x0601",
        "-Isrc", "-Ithird_party/scintilla/include",
        "-Ithird_party/lexilla/include", "-Ithird_party/cjson",
        "src/main.c", "src/studio_ui.c", "src/studio_navigation.c",
        "src/studio_workspace.c", "src/studio_debug.c", "src/editor.c",
        "src/project.c", "src/symbol_search.c", "src/debug_session.c",
        "src/input_dialog.c", "src/project_settings.c",
        "third_party/cjson/cJSON.c", "-mwindows", "-lcomctl32", "-lcomdlg32",
        "-lshell32", "-lole32", "-luxtheme", "-lgdi32", "-luser32",
        "-o", "build/windows/bin/quickjs-studio.exe"
    )
    & $Gcc @StudioArgs
    if ($LASTEXITCODE -ne 0) { throw "QuickJS Studio build failed" }

    Write-Host "Building qjs-host.exe"
    $HostArgs = @(
        "-std=c11", "-O0", "-g3", "-Wall", "-Wextra", "-Werror",
        "-D_WIN32_WINNT=0x0601", "-Iagent", "-isystem", "third_party/quickjs",
        "examples/host.c", "agent/qjs_debug_agent.c",
        "third_party/quickjs/libquickjs_win64.a", "-static", "-lm",
        "-lwinpthread", "-o", "build/windows/bin/qjs-host.exe"
    )
    & $Gcc @HostArgs
    if ($LASTEXITCODE -ne 0) { throw "Example host build failed" }

    Copy-Item .\third_party\scintilla\bin\Scintilla.dll $BinDir -Force
    Copy-Item .\third_party\lexilla\bin\lexilla.dll $BinDir -Force
    Write-Host "PASS: $BinDir\quickjs-studio.exe"
}
finally {
    Pop-Location
}
