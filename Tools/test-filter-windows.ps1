# Compatibility entry point: TEST61 uses the ordinary build, including audio tests.
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "build-windows.ps1")
