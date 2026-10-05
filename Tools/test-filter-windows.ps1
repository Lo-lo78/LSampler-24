$ErrorActionPreference = "Stop"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Visual Studio C++ tools are required." }
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw "C++ x64 toolchain not found." }
$developerCommand = Join-Path $visualStudio "Common7\Tools\VsDevCmd.bat"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot "build\filter-test60"
$commandLine = '"{0}" -arch=x64 && cmake -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DLSAMPLER_ENABLE_FILTER_CACHE=ON -DLSAMPLER_BUILD_FILTER_TESTS=ON -DLSAMPLER_BUILD_TESTS=OFF && cmake --build "{2}" --target LSampler24_VST3 LSampler24FilterTests && ctest --test-dir "{2}" --output-on-failure -R "^filter-parity$"' -f $developerCommand, $projectRoot, $buildDirectory
& cmd.exe /d /s /c $commandLine
$testExitCode = $LASTEXITCODE
if ($testExitCode -ne 0) { throw "TEST60 build or audio comparison failed with exit code $testExitCode. Candidate packaging is blocked." }
Write-Host "Audio comparison passed. Reports: $buildDirectory\filter-reports"
