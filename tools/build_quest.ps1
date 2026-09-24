param(
    [switch]$Install,
    [switch]$Launch,
    [string]$Serial,
    [string]$Rom,
    [string]$Sdk,
    [string]$Ndk,
    [string]$JavaHome,
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$buildArguments = @((Join-Path $PSScriptRoot 'build_quest.py'), '--jobs', "$Jobs")
if ($Install) { $buildArguments += '--install' }
if ($Launch) { $buildArguments += '--launch' }
foreach ($item in @(@('--serial', $Serial), @('--rom', $Rom), @('--sdk', $Sdk), @('--ndk', $Ndk), @('--java-home', $JavaHome))) {
    if ($item[1]) { $buildArguments += $item }
}
& python @buildArguments
exit $LASTEXITCODE
