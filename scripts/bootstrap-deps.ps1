#requires -Version 7.0
<#
.SYNOPSIS
    准备 OP 原生 VS 工程所需的第三方静态依赖（BlackBone / OpenCV / MinHook），支持 x64 与 x86（Win32）。

.DESCRIPTION
    OP 自身只使用原生 Visual Studio 工程（op.sln）构建；本脚本只负责一次性准备第三方库：
      - vcpkg: minhook / directx-headers（x64-windows-static、x86-windows-static）
      - BlackBone: 从源码构建 BlackBone.lib（/MT），按架构输出到 build\vs2022-x64 或 build\vs2022-Win32
      - OpenCV 5.0.0: 从源码构建静态库（/MT，最小模块集），install 到 install\vs2022-x64 或 install\vs2022-Win32

    重要（本机踩坑记录）：
    如果执行过 `vcpkg integrate install`，vcpkg 会把 x64-windows（动态 CRT）的 include/lib
    注入所有 MSBuild 工程。OpenCV 构建时会误用 vcpkg 里的 OpenCV4/protobuf 头文件，报
    `"ENGINE_NEW": 不是 "cv::dnn" 的成员` 等错误。因此本脚本在构建第三方库时显式传
    `/p:VcpkgEnabled=false` 并临时移除 VCPKG_ROOT 环境变量。x86 侧 vcpkg 的
    `x86-windows-static` 里同样装着 opencv_world4 / protobuf，风险相同，故对两种架构一并屏蔽。

.PARAMETER Arch
    目标架构，默认 x64：
      - x64  : 只准备 x64 依赖（原有行为）
      - x86  : 只准备 x86（Win32）依赖
      - both : 依次准备 x64 与 x86

.PARAMETER VcpkgRoot
    vcpkg 根目录。默认依次取 $env:VCPKG_ROOT、build\_deps\vcpkg、Y:\Tools\vcpkg。

.PARAMETER DepsDir
    依赖缓存目录，默认 build\_deps。

.PARAMETER Force
    即使检测到已构建好的库也重新构建（增量重建，不删除构建目录）。

.PARAMETER SkipOpenCv
    跳过 OpenCV（若只需要编译不依赖 Cv* 的目标）。

.EXAMPLE
    pwsh -File scripts\bootstrap-deps.ps1

.EXAMPLE
    pwsh -File scripts\bootstrap-deps.ps1 -Arch x86

.EXAMPLE
    pwsh -File scripts\bootstrap-deps.ps1 -Arch both -VcpkgRoot Y:\Tools\vcpkg -Force
#>
param(
    [ValidateSet("x64", "x86", "both")]
    [string]$Arch = "x64",
    [string]$VcpkgRoot = "",
    [string]$DepsDir = "build\_deps",
    [switch]$Force,
    [switch]$SkipOpenCv
)

$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$DepsRoot = if ([System.IO.Path]::IsPathRooted($DepsDir)) { $DepsDir } else { Join-Path $RepoRoot $DepsDir }
$OpenCvVersion = "5.0.0"
$OpenCvLibSuffix = "500"

# 构建第三方库时传给 MSBuild 的参数：屏蔽全局 `vcpkg integrate install` 的注入。
$NoVcpkgMsBuildArgs = @(
    "/p:VcpkgEnabled=false",
    "/p:VcpkgApplocalDeps=false",
    "/p:VcpkgManifestInstall=false"
)

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Require-Command([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        throw "找不到命令 '$Name'。请先安装并加入 PATH。"
    }
    return $cmd.Source
}

function Invoke-Checked([string]$FilePath, [string[]]$Arguments, [string]$WorkDir = $RepoRoot) {
    Write-Host "  $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    Push-Location $WorkDir
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "命令失败（exit $LASTEXITCODE）：$FilePath $($Arguments -join ' ')"
        }
    } finally {
        Pop-Location
    }
}

# 在屏蔽 VCPKG_ROOT 的环境下执行脚本块，结束后恢复原值。
function Invoke-WithoutVcpkg([scriptblock]$Action) {
    $savedVcpkgRoot = $env:VCPKG_ROOT
    Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
    try {
        & $Action
    } finally {
        if ($savedVcpkgRoot) { $env:VCPKG_ROOT = $savedVcpkgRoot }
    }
}

function Resolve-VcpkgRoot {
    $candidates = @()
    if ($VcpkgRoot) { $candidates += $VcpkgRoot }
    if ($env:VCPKG_ROOT) { $candidates += $env:VCPKG_ROOT }
    $candidates += (Join-Path $DepsRoot "vcpkg")
    $candidates += "Y:\Tools\vcpkg"
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate "vcpkg.exe"))) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw @"
找不到 vcpkg.exe。请安装 vcpkg 后重试，或显式传入：
  pwsh -File scripts\bootstrap-deps.ps1 -Arch $Arch -VcpkgRoot <vcpkg 根目录>
"@
}

# ---------------------------------------------------------------------------
# 架构映射
# ---------------------------------------------------------------------------
# CMake -A / MSBuild /p:Platform 使用的平台名
function Get-ArchPlatform([string]$TargetArch) {
    if ($TargetArch -eq "x64") { return "x64" }
    return "Win32"
}

# 依赖目录命名：x64 沿用 vs2022-x64，x86 用 vs2022-Win32（与 vs\op.deps.props 的约定一致）
function Get-ArchTag([string]$TargetArch) {
    if ($TargetArch -eq "x64") { return "vs2022-x64" }
    return "vs2022-Win32"
}

function Get-VcpkgTriplet([string]$TargetArch) {
    return "$TargetArch-windows-static"
}

# ---------------------------------------------------------------------------
# 1. 工具检查
# ---------------------------------------------------------------------------
$cmake = Require-Command "cmake"
$git = Require-Command "git"
$vcpkg = Resolve-VcpkgRoot
$vcpkgExe = Join-Path $vcpkg "vcpkg.exe"

$archs = switch ($Arch) {
    "x64" { @("x64") }
    "x86" { @("x86") }
    "both" { @("x64", "x86") }
}

Write-Host "RepoRoot : $RepoRoot"
Write-Host "DepsRoot : $DepsRoot"
Write-Host "vcpkg    : $vcpkg"
Write-Host "cmake    : $cmake"
Write-Host "Arch     : $Arch -> $($archs -join ', ')"

New-Item -ItemType Directory -Force -Path $DepsRoot | Out-Null

# ---------------------------------------------------------------------------
# 2. vcpkg 静态包
# ---------------------------------------------------------------------------
function Install-VcpkgPackages([string]$TargetArch) {
    $triplet = Get-VcpkgTriplet $TargetArch
    Write-Step "vcpkg: 安装 $triplet 的 minhook / directx-headers"
    Invoke-Checked $vcpkgExe @(
        "install",
        "minhook:$triplet",
        "directx-headers:$triplet"
    )
    $libDir = Join-Path $vcpkg "installed\$triplet\lib"
    if (-not (Test-Path $libDir)) {
        throw "vcpkg 安装后未找到 $libDir"
    }
    Write-Host "  vcpkg install dir: $(Join-Path $vcpkg "installed\$triplet")" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 3. BlackBone
# ---------------------------------------------------------------------------
function Get-BlackBoneLibCandidates([string]$TargetArch) {
    $root = Join-Path $DepsRoot "BlackBone"
    $tag = Get-ArchTag $TargetArch
    return @(
        (Join-Path $root "build\$tag\BlackBone\Release\BlackBone.lib"),
        (Join-Path $root "build\$tag\Release\BlackBone.lib"),
        (Join-Path $root "build\$tag\src\BlackBone\Release\BlackBone.lib")
    )
}

function Install-BlackBone([string]$TargetArch) {
    $blackboneRoot = Join-Path $DepsRoot "BlackBone"
    $tag = Get-ArchTag $TargetArch
    $candidates = Get-BlackBoneLibCandidates $TargetArch
    $ready = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($ready -and -not $Force) {
        Write-Step "BlackBone ($TargetArch) 已存在，跳过：$ready"
        return
    }

    Write-Step "BlackBone ($TargetArch): 克隆并构建（/MT）"
    if (-not (Test-Path (Join-Path $blackboneRoot ".git"))) {
        if (Test-Path $blackboneRoot) {
            Remove-Item -Recurse -Force $blackboneRoot
        }
        Invoke-Checked $git @("clone", "https://github.com/DarthTon/Blackbone", $blackboneRoot)
    }

    $blackboneBuild = Join-Path $blackboneRoot "build\$tag"
    $platform = Get-ArchPlatform $TargetArch
    Invoke-WithoutVcpkg {
        Invoke-Checked $cmake @(
            "-S", (Join-Path $blackboneRoot "src"),
            "-B", $blackboneBuild,
            "-G", "Visual Studio 17 2022",
            "-A", $platform,
            "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
        )
        Invoke-Checked $cmake (@(
            "--build", $blackboneBuild,
            "--config", "Release",
            "--"
        ) + $NoVcpkgMsBuildArgs)
    }

    $ready = Get-BlackBoneLibCandidates $TargetArch | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $ready) {
        throw "BlackBone.lib ($TargetArch) 构建完成但未找到。请检查 $blackboneBuild。"
    }
    Write-Host "  BlackBone.lib: $ready" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 4. OpenCV 5.0.0 静态库
# ---------------------------------------------------------------------------
function Get-OpenCvPaths([string]$TargetArch) {
    $tag = Get-ArchTag $TargetArch
    $opencvRoot = Join-Path $DepsRoot "opencv"
    return @{
        Root    = $opencvRoot
        Source  = (Join-Path $opencvRoot "opencv-$OpenCvVersion")
        Build   = (Join-Path $opencvRoot "build\$tag")
        Install = (Join-Path $opencvRoot "install\$tag")
    }
}

# 在 install 目录里定位 opencv_core<500>.lib（优先 vc17，兼容 vc16/vc18）。
function Find-OpenCvCoreLib([string]$InstallDir, [string]$TargetArch) {
    $expected = Join-Path $InstallDir "$TargetArch\vc17\staticlib\opencv_core$OpenCvLibSuffix.lib"
    if (Test-Path $expected) { return $expected }
    if (-not (Test-Path $InstallDir)) { return $null }
    $found = Get-ChildItem -Path $InstallDir -Recurse -Filter "opencv_core$OpenCvLibSuffix.lib" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

# OpenCV 的 CMake 选项：两种架构完全一致（BUILD_LIST 不含 dnn）。
$OpenCvCommonCMakeArgs = @(
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>',
    "-DBUILD_SHARED_LIBS=OFF",
    "-DBUILD_WITH_STATIC_CRT=ON",
    "-DBUILD_LIST=core,imgcodecs,flann,imgproc,features,objdetect,stereo,calib",
    "-DBUILD_opencv_world=OFF",
    "-DBUILD_TESTS=OFF",
    "-DBUILD_PERF_TESTS=OFF",
    "-DBUILD_EXAMPLES=OFF",
    "-DBUILD_DOCS=OFF",
    "-DBUILD_PACKAGE=OFF",
    "-DBUILD_JAVA=OFF",
    "-DBUILD_opencv_apps=OFF",
    "-DBUILD_opencv_gapi=OFF",
    "-DBUILD_opencv_js=OFF",
    "-DBUILD_opencv_python_bindings_generator=OFF",
    "-DBUILD_opencv_python2=OFF",
    "-DBUILD_opencv_python3=OFF",
    "-DBUILD_PNG=ON",
    "-DBUILD_JPEG=ON",
    "-DBUILD_ZLIB=ON",
    "-DWITH_JASPER=OFF",
    "-DWITH_ADE=OFF",
    "-DWITH_WEBP=OFF",
    "-DWITH_TIFF=OFF",
    "-DWITH_OPENJPEG=OFF",
    "-DWITH_OPENEXR=OFF",
    "-DBUILD_JASPER=OFF",
    "-DBUILD_TIFF=OFF",
    "-DBUILD_WEBP=OFF",
    "-DBUILD_OPENJPEG=OFF",
    "-DBUILD_OPENEXR=OFF",
    "-DOPENCV_IO_ENABLE_OPENEXR=OFF",
    "-DWITH_IPP=OFF",
    "-DWITH_ITT=OFF",
    "-DWITH_OPENCL=OFF",
    "-DWITH_TBB=OFF",
    "-DWITH_OPENMP=OFF",
    "-DWITH_FFMPEG=OFF",
    "-DWITH_GSTREAMER=OFF",
    "-DWITH_MSMF=OFF"
)

function Install-OpenCv([string]$TargetArch) {
    $paths = Get-OpenCvPaths $TargetArch
    $coreLib = Find-OpenCvCoreLib $paths.Install $TargetArch

    if ($coreLib -and -not $Force) {
        Write-Step "OpenCV $OpenCvVersion ($TargetArch) 已存在，跳过：$($paths.Install)"
        return
    }

    Write-Step "OpenCV ${OpenCvVersion} ($TargetArch): 下载/构建静态库（耗时较长，约 10-30 分钟）"

    if (-not (Test-Path (Join-Path $paths.Source "CMakeLists.txt"))) {
        New-Item -ItemType Directory -Force -Path $paths.Root | Out-Null
        $zipPath = Join-Path $paths.Root "opencv-$OpenCvVersion.zip"
        $zipUrl = "https://github.com/opencv/opencv/archive/refs/tags/$OpenCvVersion.zip"
        Write-Host "  下载 $zipUrl"
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath
        Write-Host "  解压到 $($paths.Root)"
        Expand-Archive -Path $zipPath -DestinationPath $paths.Root -Force
        Remove-Item -Force $zipPath
    }

    if (-not (Test-Path (Join-Path $paths.Source "CMakeLists.txt"))) {
        throw "OpenCV 源码目录不存在：$($paths.Source)"
    }

    $platform = Get-ArchPlatform $TargetArch

    # 关键：构建 OpenCV 时必须屏蔽全局 vcpkg 集成，避免误用 vcpkg 的 OpenCV4/protobuf 头。
    Invoke-WithoutVcpkg {
        Invoke-Checked $cmake (@(
                "-S", $paths.Source,
                "-B", $paths.Build,
                "-G", "Visual Studio 17 2022",
                "-A", $platform,
                "-DCMAKE_INSTALL_PREFIX=$($paths.Install)"
            ) + $OpenCvCommonCMakeArgs)
        Invoke-Checked $cmake (@(
                "--build", $paths.Build,
                "--config", "Release",
                "--target", "install",
                "--"
            ) + $NoVcpkgMsBuildArgs)
    }

    $coreLib = Find-OpenCvCoreLib $paths.Install $TargetArch
    if (-not $coreLib) {
        throw "OpenCV ($TargetArch) 构建完成但未找到 opencv_core$OpenCvLibSuffix.lib（$($paths.Install)）"
    }
    Write-Host "  OpenCV install: $($paths.Install)" -ForegroundColor Green
    Write-Host "  $coreLib" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 5. 按架构执行
# ---------------------------------------------------------------------------
$results = @()
$totalTimer = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($targetArch in $archs) {
    Write-Step "########## 架构：$targetArch ##########"
    $archTimer = [System.Diagnostics.Stopwatch]::StartNew()

    Install-VcpkgPackages $targetArch
    Install-BlackBone $targetArch
    # 路径统一由“确定性映射 + 存在性探测”得出，不从函数返回值里取（避免混入 CMake/MSBuild 控制台输出）。
    $blackboneLib = Get-BlackBoneLibCandidates $targetArch | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $blackboneLib) {
        throw "BlackBone.lib ($targetArch) 未找到，构建可能失败。"
    }

    $opencvInstall = $null
    $opencvCore = $null
    if ($SkipOpenCv) {
        Write-Step "按参数跳过 OpenCV ($targetArch)"
    } else {
        Install-OpenCv $targetArch
        $opencvInstall = (Get-OpenCvPaths $targetArch).Install
        $opencvCore = Find-OpenCvCoreLib $opencvInstall $targetArch
    }

    $archTimer.Stop()
    $results += [pscustomobject]@{
        Arch       = $targetArch
        Platform   = (Get-ArchPlatform $targetArch)
        Triplet    = (Get-VcpkgTriplet $targetArch)
        BlackBone  = $blackboneLib
        OpenCv     = $(if ($SkipOpenCv) { "(跳过)" } else { $opencvInstall })
        OpenCvCore = $(if ($SkipOpenCv) { "(跳过)" } else { $opencvCore })
        ElapsedSec = [math]::Round($archTimer.Elapsed.TotalSeconds, 1)
    }
}

$totalTimer.Stop()

Write-Step "依赖准备完成"
$results | Format-List | Out-String | Write-Host

Write-Host ""
Write-Host "下一步：用 VS2022 打开 op.sln，或执行：" -ForegroundColor Yellow
foreach ($r in $results) {
    Write-Host "  & 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=$($r.Platform) /m"
}
Write-Host ""
Write-Host "总耗时：$([math]::Round($totalTimer.Elapsed.TotalMinutes, 1)) 分钟" -ForegroundColor Yellow
