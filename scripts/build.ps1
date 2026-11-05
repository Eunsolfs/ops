#requires -Version 7.0
<#
.SYNOPSIS
    用原生 VS2022 工程构建 OP（默认不写死本机 MSBuild 路径）。

.DESCRIPTION
    优先用 vswhere 找 MSBuild；找不到时回退常见安装位置。
    默认关闭 Twinkle 同步，避免在 CI/其它仓库里产生副作用。
    本机 tlog 文件偶发被杀软/索引器占用时，加 -NoTrackFileAccess 可绕过（代价是增量构建失效）。

.PARAMETER Configuration
    Debug 或 Release（默认 Release）。

.PARAMETER Platform
    x64 或 Win32（默认 x64）。

.PARAMETER Target
    all / native = 构建 op.sln；test = 只构建 tests\op_test.vcxproj（需要先构建 native）。

.PARAMETER WarningsAsErrors
    把编译告警升级为错误（/p:OpWarningsAsErrors=true）。

.EXAMPLE
    pwsh scripts/build.ps1
    pwsh scripts/build.ps1 -Platform Win32 -Configuration Debug
    pwsh scripts/build.ps1 -Target test -WarningsAsErrors
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [ValidateSet("all", "native", "test")]
    [string]$Target = "all",
    [string]$MSBuild = "",
    [switch]$Rebuild,
    [switch]$WarningsAsErrors,
    [switch]$SyncTwinkle,
    [switch]$NoTrackFileAccess
)

$ErrorActionPreference = "Stop"
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

function Resolve-MSBuildPath {
    if ($MSBuild) {
        if (-not (Test-Path -LiteralPath $MSBuild)) {
            throw "指定的 MSBuild 不存在：$MSBuild"
        }
        return (Resolve-Path -LiteralPath $MSBuild).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) {
            return $found
        }
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe"),
        "Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "找不到 MSBuild.exe。请安装 VS2022（含 C++ 工作负载），或用 -MSBuild <path> 指定。"
}

$msbuildPath = Resolve-MSBuildPath
Write-Host "[OP] MSBuild: $msbuildPath"
Write-Host "[OP] $Configuration | $Platform | $Target"

if ($Target -eq "test") {
    $project = Join-Path $repo "tests\op_test.vcxproj"
} else {
    $project = Join-Path $repo "op.sln"
}

$arguments = @(
    $project,
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:OpSyncTwinkle=$($SyncTwinkle.IsPresent.ToString().ToLower())",
    "/m",
    "/nodeReuse:false",
    "/v:minimal"
)
if ($Rebuild) { $arguments += "/t:Rebuild" } else { $arguments += "/t:Build" }
if ($WarningsAsErrors) { $arguments += "/p:OpWarningsAsErrors=true" }
if ($NoTrackFileAccess) { $arguments += "/p:TrackFileAccess=false" }

& $msbuildPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "构建失败（exit=$LASTEXITCODE）：$Configuration $Platform $Target"
}

Write-Host "[OP] 构建成功：$Configuration $Platform $Target" -ForegroundColor Green
