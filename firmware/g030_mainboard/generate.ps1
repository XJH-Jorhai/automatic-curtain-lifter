param(
    [string]$CubeMxDir = "$env:LOCALAPPDATA\Programs\STM32CubeMX"
)
$ErrorActionPreference = 'Stop'
$WorkspaceDir = (Resolve-Path "$PSScriptRoot\..\..").Path
$CubeHome = Join-Path $WorkspaceDir 'work\cubemx-home'
$ScriptPath = Join-Path $WorkspaceDir 'work\mainboard_generate.txt'
$LogPath = Join-Path $WorkspaceDir 'work\mainboard_generate.log'
$Java = Join-Path $CubeMxDir 'jre\bin\java.exe'
if (!(Test-Path -LiteralPath $Java)) { throw "CubeMX Java not found: $Java" }
@(
    "config load $PSScriptRoot\g030_mainboard.ioc"
    'project name g030_mainboard'
    "project path $PSScriptRoot"
    'project toolchain CMake'
    "project setCustomFWPath $CubeHome\STM32Cube\Repository\STM32Cube_FW_G0_V1.6.3"
    'project generate'
    'exit'
) | Set-Content -LiteralPath $ScriptPath -Encoding ascii
& $Java "-Duser.home=$CubeHome" -jar "$CubeMxDir\STM32CubeMX.exe" -q $ScriptPath *> $LogPath
$Result = $LASTEXITCODE
$Text = Get-Content -Raw -LiteralPath $LogPath
if ($Result -ne 0 -or $Text -notmatch '(?m)^OK\s*$' -or $Text -notmatch 'Bye bye' -or
    $Text -notmatch 'Generated code:.*Core[\\/]Src[\\/]main\.c' -or
    !(Test-Path "$PSScriptRoot\Core\Src\main.c")) {
    throw "CubeMX generation did not complete. Inspect $LogPath"
}
Get-Content -LiteralPath $LogPath -Tail 12
