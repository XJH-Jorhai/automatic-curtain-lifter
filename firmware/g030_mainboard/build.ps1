param(
    [ValidateSet('Product', 'TrqCapture', 'Debug', 'Release', 'TuyaBridge', 'TuyaApp')][string]$Configuration = 'Product',
    [string]$CmakeExe = 'cmake',
    [string]$NinjaExe = 'ninja',
    [string]$PythonExe = 'python',
    [string]$ArmBin = '',
    [string]$TuyaSdkDir = ''
)
$ErrorActionPreference = 'Stop'
if ($ArmBin) { $env:PATH = "$ArmBin;$env:PATH" }
$CmakeExe = (Get-Command $CmakeExe -ErrorAction Stop).Source
$NinjaExe = (Get-Command $NinjaExe -ErrorAction Stop).Source
$PythonExe = (Get-Command $PythonExe -ErrorAction Stop).Source
Get-Command arm-none-eabi-gcc -ErrorAction Stop | Out-Null
$ConfigureArgs = @('--preset', $Configuration, "-DCMAKE_MAKE_PROGRAM=$NinjaExe", "-DPython3_EXECUTABLE=$PythonExe")
if ($TuyaSdkDir) { $ConfigureArgs += "-DTUYA_SDK_DIR=$((Resolve-Path -LiteralPath $TuyaSdkDir).Path)" }
Push-Location $PSScriptRoot
try {
    & $CmakeExe @ConfigureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $CmakeExe --build --preset $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally { Pop-Location }
