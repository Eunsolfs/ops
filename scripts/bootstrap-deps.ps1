#requires -Version 7.0
<#
.SYNOPSIS
    准备 OP 原生 VS 工程所需的第三方静态依赖（BlackBone / OpenCV / MinHook）。

.DESCRIPTION
    OP 自身只使用原生 Visual Studio 工程（op.sln）构建；本脚本只负责一次性准备第三方库：
      - vcpkg: minhook / directx-headers（x64-windows-static）
      - BlackBone: 从源码构建 BlackBone.lib（/MT）
      - OpenCV 5.0.0: 从源码构建静态库（/MT，最小模块集）

    重要（本机踩坑记录）：
    如果执行过 `vcpkg integrate install`，vcpkg 会把 x64-windows（动态 CRT）的 include/lib
    注入所有 MSBuild 工程。OpenCV 构建时会误用 vcpkg 里的 OpenCV4/protobuf 头文件，报
    `"ENGINE_NEW": 不是 "cv::dnn" 的成员` 等错误。因此本脚本在构建第三方库时显式传
    `/p:VcpkgEnabled=false` 并临时移除 VCPKG_ROOT 环境变量。

.PARAMETER VcpkgRoot
    vcpkg 根目录。默认依次取 $env:VCPKG_ROOT、build\_deps\vcpkg、Y:\Tools\vcpkg。

.PARAMETER DepsDir
    依赖缓存目录，默认 build\_deps。

.PARAMETER Force
    即使检测到已构建好的库也重新构建。

.PARAMETER SkipOpenCv
    跳过 OpenCV（若只需要编译不依赖 Cv* 的目标）。

.EXAMPLE
    pwsh -File scripts\bootstrap-deps.ps1

.EXAMPLE
    pwsh -File scripts\bootstrap-deps.ps1 -VcpkgRoot Y:\Tools\vcpkg -Force
#>
param(
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
  pwsh -File scripts\bootstrap-deps.ps1 -VcpkgRoot <vcpkg 根目录>
"@
}

# ---------------------------------------------------------------------------
# 1. 工具检查
# ---------------------------------------------------------------------------
$cmake = Require-Command "cmake"
$git = Require-Command "git"
$vcpkg = Resolve-VcpkgRoot
$vcpkgExe = Join-Path $vcpkg "vcpkg.exe"

Write-Host "RepoRoot : $RepoRoot"
Write-Host "DepsRoot : $DepsRoot"
Write-Host "vcpkg    : $vcpkg"
Write-Host "cmake    : $cmake"

New-Item -ItemType Directory -Force -Path $DepsRoot | Out-Null

# ---------------------------------------------------------------------------
# 2. vcpkg 静态包
# ---------------------------------------------------------------------------
Write-Step "vcpkg: 安装 x64-windows-static 的 minhook / directx-headers"
Invoke-Checked $vcpkgExe @(
    "install",
    "minhook:x64-windows-static",
    "directx-headers:x64-windows-static"
)

# ---------------------------------------------------------------------------
# 3. BlackBone
# ---------------------------------------------------------------------------
$blackboneRoot = Join-Path $DepsRoot "BlackBone"
$blackboneLibCandidates = @(
    (Join-Path $blackboneRoot "build\vs2022-x64\BlackBone\Release\BlackBone.lib"),
    (Join-Path $blackboneRoot "build\vs2022-x64\Release\BlackBone.lib"),
    (Join-Path $blackboneRoot "build\vs2022-x64\src\BlackBone\Release\BlackBone.lib")
)
$blackboneReady = $blackboneLibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($blackboneReady -and -not $Force) {
    Write-Step "BlackBone 已存在，跳过：$blackboneReady"
} else {
    Write-Step "BlackBone: 克隆并构建（/MT）"
    if (-not (Test-Path (Join-Path $blackboneRoot ".git"))) {
        if (Test-Path $blackboneRoot) {
            Remove-Item -Recurse -Force $blackboneRoot
        }
        Invoke-Checked $git @("clone", "https://github.com/DarthTon/Blackbone", $blackboneRoot)
    }

    $blackboneBuild = Join-Path $blackboneRoot "build\vs2022-x64"
    Invoke-Checked $cmake @(
        "-S", (Join-Path $blackboneRoot "src"),
        "-B", $blackboneBuild,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
    )
    Invoke-Checked $cmake @(
        "--build", $blackboneBuild,
        "--config", "Release",
        "--",
        "/p:VcpkgEnabled=false", "/p:VcpkgApplocalDeps=false", "/p:VcpkgManifestInstall=false"
    )

    $blackboneReady = $blackboneLibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $blackboneReady) {
        throw "BlackBone.lib 构建完成但未找到。请检查 $blackboneRoot\build\vs2022-x64。"
    }
    Write-Host "  BlackBone.lib: $blackboneReady" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 4. OpenCV 5.0.0 静态库
# ---------------------------------------------------------------------------
if ($SkipOpenCv) {
    Write-Step "按参数跳过 OpenCV"
} else {
    $opencvRoot = Join-Path $DepsRoot "opencv"
    $opencvSource = Join-Path $opencvRoot "opencv-$OpenCvVersion"
    $opencvBuild = Join-Path $opencvRoot "build\vs2022-x64"
    $opencvInstall = Join-Path $opencvRoot "install\vs2022-x64"
    $opencvCoreLib = Join-Path $opencvInstall "x64\vc17\staticlib\opencv_core$OpenCvLibSuffix.lib"

    if ((Test-Path $opencvCoreLib) -and -not $Force) {
        Write-Step "OpenCV $OpenCvVersion 已存在，跳过：$opencvInstall"
    } else {
        Write-Step "OpenCV ${OpenCvVersion}: 下载/构建静态库（耗时较长，约 10-30 分钟）"

        if (-not (Test-Path (Join-Path $opencvSource "CMakeLists.txt"))) {
            New-Item -ItemType Directory -Force -Path $opencvRoot | Out-Null
            $zipPath = Join-Path $opencvRoot "opencv-$OpenCvVersion.zip"
            $zipUrl = "https://github.com/opencv/opencv/archive/refs/tags/$OpenCvVersion.zip"
            Write-Host "  下载 $zipUrl"
            Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath
            Write-Host "  解压到 $opencvRoot"
            Expand-Archive -Path $zipPath -DestinationPath $opencvRoot -Force
            Remove-Item -Force $zipPath
        }

        if (-not (Test-Path (Join-Path $opencvSource "CMakeLists.txt"))) {
            throw "OpenCV 源码目录不存在：$opencvSource"
        }

        # 关键：构建 OpenCV 时必须屏蔽全局 vcpkg 集成，避免误用 vcpkg 的 OpenCV4/protobuf 头。
        $savedVcpkgRoot = $env:VCPKG_ROOT
        Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
        try {
            Invoke-Checked $cmake @(
                "-S", $opencvSource,
                "-B", $opencvBuild,
                "-G", "Visual Studio 17 2022",
                "-A", "x64",
                "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
                "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>",
                "-DCMAKE_INSTALL_PREFIX=$opencvInstall",
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
            Invoke-Checked $cmake @(
                "--build", $opencvBuild,
                "--config", "Release",
                "--target", "install",
                "--",
                "/p:VcpkgEnabled=false", "/p:VcpkgApplocalDeps=false", "/p:VcpkgManifestInstall=false"
            )
        } finally {
            if ($savedVcpkgRoot) { $env:VCPKG_ROOT = $savedVcpkgRoot }
        }

        if (-not (Test-Path $opencvCoreLib)) {
            throw "OpenCV 构建完成但未找到 $opencvCoreLib"
        }
        Write-Host "  OpenCV install: $opencvInstall" -ForegroundColor Green
    }
}

Write-Step "依赖准备完成"
Write-Host "  BlackBone: $(($blackboneLibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1))"
if (-not $SkipOpenCv) {
    Write-Host "  OpenCV   : $(Join-Path $DepsRoot "opencv\install\vs2022-x64")"
}
Write-Host ""
Write-Host "下一步：用 VS2022 打开 op.sln，或执行：" -ForegroundColor Yellow
Write-Host "  & 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m"
