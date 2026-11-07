$ErrorActionPreference = 'Stop'

if (-not (Get-Command cl -ErrorAction SilentlyContinue))
{
    throw 'Run this test in a Visual Studio Developer PowerShell with the C++ tools installed.'
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root '.pio\build\ble_proxy_tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$arguments = @(
    '/nologo', '/std:c++17', '/EHsc',
    "/I$(Join-Path $PSScriptRoot 'ble_proxy')",
    "/I$(Join-Path $root 'src')",
    (Join-Path $PSScriptRoot 'ble_proxy\proxy.Tests.cpp'),
    (Join-Path $root 'src\BleApiService.cpp'),
    (Join-Path $root 'src\BleClient.cpp'),
    (Join-Path $root 'src\PresetManager.cpp'),
    "/Fo$build\",
    "/Fe$build\proxy-tests.exe"
)

try
{
    & cl @arguments
    if ($LASTEXITCODE -ne 0)
    {
        throw "BLE proxy test compilation failed (exit $LASTEXITCODE)."
    }

    & (Join-Path $build 'proxy-tests.exe')
    if ($LASTEXITCODE -ne 0)
    {
        throw "BLE proxy regression failed (exit $LASTEXITCODE)."
    }
}
finally
{
    foreach ($name in @('proxy.Tests.obj', 'BleApiService.obj', 'BleClient.obj', 'PresetManager.obj', 'proxy-tests.exe'))
    {
        $file = Join-Path $build $name
        if (Test-Path -LiteralPath $file)
        {
            Remove-Item -LiteralPath $file
        }
    }
}
