param(
    [string]$Cmake = 'C:/Program Files/CMake/bin/cmake.exe',
    [string]$Ninja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',
    [string]$Compiler = 'C:/msys64/ucrt64/bin/gcc.exe'
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $repository 'app/Tests'
$build = Join-Path $repository 'artifacts/configuration_v2/host-tests'
$ctest = Join-Path (Split-Path $Cmake) 'ctest.exe'
$previousPath = $env:Path
New-Item -ItemType Directory -Force -Path $build | Out-Null
try
{
    $env:Path = (Split-Path $Compiler) + ';' + $env:Path
    & $Cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_C_COMPILER=$Compiler" -DCMAKE_BUILD_TYPE=Debug 2>&1 | Tee-Object -FilePath (Join-Path $build 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $Cmake --build $build 2>&1 | Tee-Object -FilePath (Join-Path $build 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Host test build failed' }
    & $ctest --test-dir $build --output-on-failure --verbose --output-junit (Join-Path $build 'ctest-results.xml') 2>&1 | Tee-Object -FilePath (Join-Path $build 'ctest.log')
    if ($LASTEXITCODE -ne 0) { throw 'Host regression failed' }
}
finally
{
    $env:Path = $previousPath
}
