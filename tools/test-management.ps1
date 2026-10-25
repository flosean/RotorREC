$ErrorActionPreference = 'Stop'
$rrRoot = Split-Path -Parent $PSScriptRoot
$rrVswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$rrVs = & $rrVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$rrVs) { throw 'MSVC C build tools are required.' }
$rrVars = Join-Path $rrVs 'VC/Auxiliary/Build/vcvars64.bat'
$rrOutput = Join-Path $rrRoot 'build-host-tests'
New-Item -ItemType Directory -Path $rrOutput -Force | Out-Null
Push-Location $rrOutput
try {
    $rrCompile = 'call "{0}" >nul && cl /nologo /std:c11 /experimental:c11atomics /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}/main" /I"{1}/tests" "{1}/tests/management_test.c" "{1}/main/management/protocol.c" "{1}/main/management/settings.c" /Fe:management_test.exe /link bcrypt.lib && management_test.exe' -f $rrVars, $rrRoot
    & cmd.exe /d /s /c $rrCompile
    if ($LASTEXITCODE -ne 0) { throw 'Management native tests failed.' }
    $rrResetCompile = 'call "{0}" >nul && cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}/main" /I"{1}/tests" "{1}/tests/reset_test.c" "{1}/main/management/settings.c" /Fe:reset_test.exe && reset_test.exe' -f $rrVars, $rrRoot
    & cmd.exe /d /s /c $rrResetCompile
    if ($LASTEXITCODE -ne 0) { throw 'Reset native tests failed.' }
    $rrWifiCompile = 'call "{0}" >nul && cl /nologo /std:c11 /experimental:c11atomics /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}/main" /I"{1}/tests" "{1}/tests/ground_wifi_test.c" "{1}/main/management/protocol.c" /Fe:ground_wifi_test.exe && ground_wifi_test.exe' -f $rrVars, $rrRoot
    & cmd.exe /d /s /c $rrWifiCompile
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi server native tests failed.' }
    $rrButtonCompile = 'call "{0}" >nul && cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"{1}/main" /I"{1}/tests" "{1}/tests/boot_button_test.c" /Fe:boot_button_test.exe && boot_button_test.exe' -f $rrVars, $rrRoot
    & cmd.exe /d /s /c $rrButtonCompile
    if ($LASTEXITCODE -ne 0) { throw 'BOOT button native tests failed.' }
} finally { Pop-Location }
& python "$rrRoot/tests/test_manager.py"
if ($LASTEXITCODE -ne 0) { throw 'Manager Python tests failed.' }
