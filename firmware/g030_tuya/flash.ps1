param(
    [Parameter(Mandatory=$true)][string]$ProbeId,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$PythonExe = 'python',
    [switch]$NoWindow
)
$ErrorActionPreference = 'Stop'
$WorkspaceDir = (Resolve-Path "$PSScriptRoot\..\..").Path
$Elf = "$WorkspaceDir\work\g030-build\$Configuration\g030_tuya.elf"
if (!(Test-Path -LiteralPath $Elf)) { throw "Build first: $Elf" }
$PythonExe = (Get-Command $PythonExe -ErrorAction Stop).Source
$env:PYTHONIOENCODING = 'utf-8'
$MonitorArgs = @()
if ($NoWindow) { $MonitorArgs += '--no-window' }
& $PythonExe "$PSScriptRoot\..\g030_mainboard\flash_bounded.py" --elf $Elf --probe $ProbeId --words 4 @MonitorArgs
if ($LASTEXITCODE -ne 0) { throw "Flash failed: $LASTEXITCODE" }
