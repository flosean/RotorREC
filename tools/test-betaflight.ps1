$ErrorActionPreference = 'Stop'
$bfRoot = Split-Path -Parent $PSScriptRoot
$bfVswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$bfInstall = & $bfVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$bfInstall) { throw 'MSVC C build tools are required for native tests.' }
$bfVars = Join-Path $bfInstall 'VC\Auxiliary\Build\vcvars64.bat'
$bfOutput = Join-Path $bfRoot 'build-host-tests'
New-Item -ItemType Directory -Path $bfOutput -Force | Out-Null
Push-Location $bfOutput
try {
    $bfCompile = 'call "{0}" >nul && cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}\main" "{1}\tests\betaflight_test.c" "{1}\main\betaflight\msp_v2.c" "{1}\main\betaflight\bf_control.c" "{1}\main\betaflight\osd_messages.c" "{1}\main\management\settings.c" "{1}\main\ui\status_indicator.c" /Fe:betaflight_test.exe && betaflight_test.exe' -f $bfVars, $bfRoot
    & cmd.exe /d /s /c $bfCompile
    if ($LASTEXITCODE -ne 0) { throw "Betaflight tests failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
