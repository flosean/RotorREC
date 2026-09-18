param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $IdfArguments = @('build')
)

# Never run set-target against the existing C6 sdkconfig or build directory.
if ($IdfArguments -contains 'set-target' -or $IdfArguments -contains 'fullclean') {
    throw 'This wrapper owns an isolated C3 build; use build, menuconfig, flash or monitor.'
}
$c3Project = Split-Path -Parent $PSScriptRoot
$c3Build = Join-Path $c3Project 'build-c3-zero'
$c3Sdkconfig = Join-Path $c3Build 'sdkconfig'
$c3Defaults = Join-Path $c3Project 'config/sdkconfig.c3-zero.defaults'
Push-Location $c3Project
try {
    & "$PSScriptRoot/idf.ps1" -IdfArguments (@(
        '-B', $c3Build,
        '-DIDF_TARGET=esp32c3',
        "-DSDKCONFIG=$c3Sdkconfig",
        "-DSDKCONFIG_DEFAULTS=$c3Defaults"
    ) + $IdfArguments)
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
