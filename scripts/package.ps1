#requires -Version 7.0
<#
.SYNOPSIS
    打包 OP 的 Release 产物（DLL/LIB/头文件/许可证/PDB），生成可分发 zip。

.DESCRIPTION
    默认使用已构建好的 Release 产物（bin\<arch>\、lib\<arch>\）；加 -Build 会先调用 build.ps1。
    输出：
      dist/op-<version>-<arch>.zip
      dist/op-<version>-<arch>-symbols.zip
    版本从 include/op_version.h 的 OP_VERSION_STRING 读取，避免多处手工同步。

.EXAMPLE
    pwsh scripts/package.ps1
    pwsh scripts/package.ps1 -Arch x64 -Build
#>
param(
    [ValidateSet("x64", "Win32", "both")]
    [string]$Arch = "both",
    [string]$Version = "",
    [switch]$Build,
    [switch]$IncludeTestTools
)

$ErrorActionPreference = "Stop"
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$dist = Join-Path $repo "dist"

if (-not $Version) {
    $versionHeader = Join-Path $repo "include\op_version.h"
    $match = Select-String -Path $versionHeader -Pattern '#define\s+OP_VERSION_STRING\s+"([^"]+)"' |
        Select-Object -First 1
    if (-not $match) {
        throw "无法从 $versionHeader 解析 OP_VERSION_STRING"
    }
    $Version = $match.Matches[0].Groups[1].Value
}
Write-Host "[OP] package version: $Version"

$targets = if ($Arch -eq "both") { @("x64", "Win32") } else { @($Arch) }

foreach ($target in $targets) {
    $archDir = if ($target -eq "x64") { "x64" } else { "x86" }
    if ($Build) {
        & (Join-Path $PSScriptRoot "build.ps1") -Configuration Release -Platform $target -NoTrackFileAccess
    }

    $binDir = Join-Path $repo "bin\$archDir"
    $libDir = Join-Path $repo "lib\$archDir"
    $stage = Join-Path $dist "op-$Version-$archDir"
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Join-Path $stage "bin") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $stage "lib") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $stage "include") -Force | Out-Null

    $binFiles = @(
        "op_c_api_$archDir.dll",
        "op_$archDir.dll",
        "tools.dll"
    )
    if ($IncludeTestTools) { $binFiles += "cpp_call.exe"; $binFiles += "op_test.exe" }
    foreach ($file in $binFiles) {
        $source = Join-Path $binDir $file
        if (-not (Test-Path -LiteralPath $source)) {
            throw "找不到 $source；先运行 pwsh scripts/build.ps1 -Configuration Release -Platform $target"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $stage "bin") -Force
    }

    $libFiles = @("op_c_api_$archDir.lib", "op_$archDir.lib", "libop.lib")
    foreach ($file in $libFiles) {
        $source = Join-Path $libDir $file
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $stage "lib") -Force
        }
    }

    $headers = @("op_c_api.h", "libop.h", "op_version.h")
    foreach ($header in $headers) {
        Copy-Item -LiteralPath (Join-Path $repo "include\$header") -Destination (Join-Path $stage "include") -Force
    }

    Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination $stage -Force
    Copy-Item -LiteralPath (Join-Path $repo "THIRD_PARTY_NOTICES.md") -Destination $stage -Force
    if (Test-Path -LiteralPath (Join-Path $repo "README.md")) {
        Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination $stage -Force
    }

    # 符号文件单独打包：线上崩溃符号化用，不随运行时安装包扩散。
    $symbols = Join-Path $dist "op-$Version-$archDir-symbols"
    if (Test-Path -LiteralPath $symbols) {
        Remove-Item -LiteralPath $symbols -Recurse -Force
    }
    New-Item -ItemType Directory -Path $symbols -Force | Out-Null
    Get-ChildItem -Path $binDir -Filter "*.pdb" -File | Copy-Item -Destination $symbols -Force
    Get-ChildItem -Path $libDir -Filter "*.pdb" -File | Copy-Item -Destination $symbols -Force

    $runtimeZip = Join-Path $dist "op-$Version-$archDir.zip"
    $symbolsZip = Join-Path $dist "op-$Version-$archDir-symbols.zip"
    Remove-Item -LiteralPath $runtimeZip, $symbolsZip -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $runtimeZip -Force
    Compress-Archive -Path (Join-Path $symbols "*") -DestinationPath $symbolsZip -Force

    Write-Host "[OP] $runtimeZip" -ForegroundColor Green
    Write-Host "[OP] $symbolsZip" -ForegroundColor Green
}

Write-Host "[OP] package done" -ForegroundColor Green
