#requires -Version 7.0
<#
.SYNOPSIS
    OP 本地门禁：四配置构建 + core 测试。

.DESCRIPTION
    依次构建 Release/Debug × x64/Win32，然后跑 x64/x86 的 core 测试集。
    失败立即停止并返回非 0，便于 CI 直接使用。

.PARAMETER WarningsAsErrors
    构建时把告警升级为错误（默认关闭；CI 建议打开）。

.PARAMETER SkipBuild
    跳过构建，只跑测试。

.PARAMETER SkipTests
    只构建，不跑测试。

.EXAMPLE
    pwsh scripts/verify.ps1
    pwsh scripts/verify.ps1 -WarningsAsErrors
#>
param(
    [switch]$WarningsAsErrors,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$testScript = Join-Path $PSScriptRoot "test.ps1"

$configs = @(
    @{ Configuration = "Release"; Platform = "x64" },
    @{ Configuration = "Release"; Platform = "Win32" },
    @{ Configuration = "Debug"; Platform = "x64" },
    @{ Configuration = "Debug"; Platform = "Win32" }
)

if (-not $SkipBuild) {
    foreach ($entry in $configs) {
        Write-Host ""
        Write-Host "[verify] build $($entry.Configuration) $($entry.Platform)" -ForegroundColor Cyan
        $buildArgs = @{
            Configuration      = $entry.Configuration
            Platform           = $entry.Platform
            Rebuild            = $true
            NoTrackFileAccess  = $true
        }
        if ($WarningsAsErrors) { $buildArgs.WarningsAsErrors = $true }
        & $buildScript @buildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "[verify] 构建失败：$($entry.Configuration) $($entry.Platform)"
        }
    }
}

if (-not $SkipTests) {
    foreach ($platform in @("x64", "Win32")) {
        Write-Host ""
        Write-Host "[verify] test core $platform" -ForegroundColor Cyan
        & $testScript -Platform $platform -Scope core
        if ($LASTEXITCODE -ne 0) {
            throw "[verify] 测试失败：$platform"
        }
    }
}

Write-Host ""
Write-Host "[verify] PASS" -ForegroundColor Green
