param(
    [ValidateSet('c3', 'c6')][string]$Board = 'c3',
    [string[]]$IdfArguments = @('build')
)
$ErrorActionPreference = 'Stop'
if ($IdfArguments -contains 'set-target' -or $IdfArguments -contains 'fullclean') {
    throw 'Use build, flash, monitor or size; this wrapper owns the board selection.'
}
$rrRoot = Split-Path -Parent $PSScriptRoot
$rrBuild = Join-Path $rrRoot "build-passthrough-$Board"
$rrTarget = if ($Board -eq 'c3') { 'esp32c3' } else { 'esp32c6' }
$rrDefaults = if ($Board -eq 'c3') { 'config/sdkconfig.c3-zero.defaults' } else { 'sdkconfig.defaults' }
Push-Location $rrRoot
try {
    & "$PSScriptRoot/idf.ps1" -IdfArguments (@('-B', $rrBuild, "-DIDF_TARGET=$rrTarget",
        "-DSDKCONFIG=$rrBuild/sdkconfig", "-DSDKCONFIG_DEFAULTS=$rrDefaults") + $IdfArguments)
    exit $LASTEXITCODE
} finally { Pop-Location }
