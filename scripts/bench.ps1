#requires -Version 7.0
<#
.SYNOPSIS
    运行合成图性能基准（不依赖仓外 assets）。

.DESCRIPTION
    设置 OP_RUN_BENCH=1 后运行 op_test 的 BenchTest 集，输出 BENCH,<name>,<w>x<h>,<tpl>,... CSV 行。
    结果默认同时打印并保存到 build/bench-latest.log（build/ 已在 .gitignore 中）。

.PARAMETER Platform
    x64（默认）或 Win32。

.PARAMETER OutFile
    额外保存路径；默认 build/bench-latest.log。

.EXAMPLE
    pwsh scripts/bench.ps1
    pwsh scripts/bench.ps1 -Platform Win32 -OutFile build/bench-x86.log
#>
param(
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$arch = if ($Platform -eq "x64") { "x64" } else { "x86" }
$exe = Join-Path $repo "bin\$arch\op_test.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 $exe。先运行 pwsh scripts/build.ps1 -Target test -Platform $Platform"
}

if (-not $OutFile) {
    $OutFile = Join-Path $repo "build\bench-latest.log"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null

$env:OP_RUN_BENCH = "1"
Write-Host "[OP] bench $arch -> $OutFile"
$output = & $exe --gtest_filter="BenchTest.*" --gtest_brief=1 2>&1
$exitCode = $LASTEXITCODE
$output | Write-Host
$output | Out-File -FilePath $OutFile -Encoding utf8
Write-Host "[OP] exit=$exitCode"
exit $exitCode
