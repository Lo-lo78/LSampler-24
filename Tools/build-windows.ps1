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
$commandLine = '"{0}" -arch=x64 && cmake -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DLSAMPLER_BUILD_FILTER_TESTS=ON -DLSAMPLER_BUILD_TESTS=OFF && cmake --build "{2}" --target LSampler24_VST3 LSampler24FilterTests && ctest --test-dir "{2}" --output-on-failure --no-tests=error -R "^filter-parity$"' -f $developerCommand, $projectRoot, $buildDirectory

& cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "LSampler-24 TEST62 build or audio comparison failed with exit code $LASTEXITCODE." }
Write-Host "TEST62 audio comparison passed. Build completed: $buildDirectory\LSampler24_artefacts\Release\VST3\LSampler-24.vst3"
