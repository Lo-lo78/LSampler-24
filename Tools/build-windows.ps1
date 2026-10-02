$ErrorActionPreference = "Stop"

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Build Tools with Desktop development with C++ is required."
}
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw "The Visual Studio C++ x64 toolchain was not found." }

$developerCommand = Join-Path $visualStudio "Common7\Tools\VsDevCmd.bat"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot "build\make-release"
$commandLine = '"{0}" -arch=x64 && cmake -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build "{2}"' -f $developerCommand, $projectRoot, $buildDirectory

& cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "LSampler-24 build failed with exit code $LASTEXITCODE." }
Write-Host "Build completed: $buildDirectory\LSampler24_artefacts\Release\VST3\LSampler-24.vst3"
