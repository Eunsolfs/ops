#requires -Version 7.0
<#
.SYNOPSIS
    运行 op_test。默认只跑不依赖外部服务/真实桌面的 core 集。

.DESCRIPTION
    自动设置 OP_SKIP_OCR_TESTS / OP_SKIP_WGC_TESTS，透传 gtest 退出码。
    Scope=all 才跑完整套件（需要 OCR 服务、交互桌面与测试资产）。

.PARAMETER Platform
    x64 或 Win32（默认 x64）。

.PARAMETER Scope
    core（默认，算法/字符串/图色/OpenCV/YOLO）或 all。

.PARAMETER Filter
    自定义 --gtest_filter，设置后覆盖 Scope。

.EXAMPLE
    pwsh scripts/test.ps1
    pwsh scripts/test.ps1 -Platform Win32
    pwsh scripts/test.ps1 -Scope all
    pwsh scripts/test.ps1 -Filter "ImageColorTest.*"
#>
param(
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [ValidateSet("core", "all")]
    [string]$Scope = "core",
    [string]$Filter = "",
    [switch]$AlsoDisabled
)

$ErrorActionPreference = "Stop"
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$arch = if ($Platform -eq "x64") { "x64" } else { "x86" }
$exe = Join-Path $repo "bin\$arch\op_test.exe"

if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 $exe。先运行 pwsh scripts/build.ps1 -Target test -Platform $Platform"
}

# core 集不依赖 OCR 服务 / WGC / 真实桌面输入。
$env:OP_SKIP_OCR_TESTS = "1"
$env:OP_SKIP_WGC_TESTS = "1"

$coreFilter = "UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*:ImageColorTest.*:OpenCvTest.*:YoloTest.*"
$effectiveFilter = if ($Filter) { $Filter } elseif ($Scope -eq "all") { "*" } else { $coreFilter }

$arguments = @("--gtest_filter=$effectiveFilter", "--gtest_brief=1")
if ($AlsoDisabled) { $arguments += "--gtest_also_run_disabled_tests" }

Write-Host "[OP] $exe --gtest_filter=$effectiveFilter"
& $exe @arguments
$exitCode = $LASTEXITCODE
Write-Host "[OP] exit=$exitCode"
exit $exitCode
