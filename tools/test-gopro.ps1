$ErrorActionPreference = 'Stop'
$gpRoot = Split-Path -Parent $PSScriptRoot
$gpVswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$gpInstall = & $gpVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$gpInstall) { throw 'MSVC C build tools are required for native tests.' }
$gpVars = Join-Path $gpInstall 'VC\Auxiliary\Build\vcvars64.bat'
$gpOutput = Join-Path $gpRoot 'build-host-tests'
New-Item -ItemType Directory -Path $gpOutput -Force | Out-Null
Push-Location $gpOutput
try {
    $gpCompile = 'call "{0}" >nul && cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}\main" "{1}\tests\gopro_test.c" "{1}\main\camera\gopro\gopro_protocol.c" "{1}\main\betaflight\bf_control.c" /Fe:gopro_test.exe && gopro_test.exe' -f $gpVars, $gpRoot
    & cmd.exe /d /s /c $gpCompile
    if ($LASTEXITCODE -ne 0) { throw "GoPro tests failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
