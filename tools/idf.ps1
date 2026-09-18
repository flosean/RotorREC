param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $IdfArguments
)
$ErrorActionPreference = 'Stop'
# Optional machine-specific setup is deliberately excluded from Git.
$localSetup = Join-Path (Split-Path -Parent $PSScriptRoot) '.local/idf.ps1'
if (!$env:IDF_PATH -and (Test-Path -LiteralPath $localSetup)) {
    & $localSetup -IdfArguments $IdfArguments
    exit $LASTEXITCODE
}
if (!$env:IDF_PATH) {
    throw 'Open an ESP-IDF PowerShell terminal (or run ESP-IDF export.ps1) first.'
}
$idfScript = Join-Path $env:IDF_PATH 'tools/idf.py'
if (!(Test-Path -LiteralPath $idfScript)) { throw "ESP-IDF not found: $idfScript" }
& python $idfScript @IdfArguments
exit $LASTEXITCODE
