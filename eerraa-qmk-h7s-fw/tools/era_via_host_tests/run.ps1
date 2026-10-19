$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
python (Join-Path $Here "run.py")
if ($LASTEXITCODE -ne 0) {
    throw "host tests failed"
}
