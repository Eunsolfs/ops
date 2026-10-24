# 纯 C++ 调用示例（cpp_call）

演示不经过 COM、也不经过 C API DLL，直接把 `libop.lib` 静态链接进 C++ 程序，使用
`include/libop.h` 里的 `op::Op` 类。

## 构建

1. 先准备依赖（只需一次）：

   ```powershell
   pwsh -File scripts\bootstrap-deps.ps1
   ```

2. 用 VS2022 打开仓库根 `op.sln`，构建 `cpp_call`；或命令行：

   ```powershell
   & 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
       examples\cpp_call\cpp_call.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
   ```

3. 运行：

   ```powershell
   .\bin\x64\cpp_call.exe
   ```

   预期输出：

   ```text
   Ver      = 0.4.8.3
   BasePath =
   ID       = 0
   OpenCV   = 5.0.0
   ```

## 关键点

- 必须定义 `U_STATIC_IMPLEMENTATION`，否则 `include/libop.h` 会把 `OP_API` 当成
  `__declspec(dllimport)`，静态链接会失败。
- 链接 `lib/x64/libop.lib`，以及 OpenCV / BlackBone / MinHook 静态库
  （工程通过 `vs/op.common.props` 自动带入）。
- 如果宿主已经通过 `op_c_api_x64.dll` 使用 OP，也可以继续用 C API；本示例只是展示
  纯 C++ 静态链接路径。
