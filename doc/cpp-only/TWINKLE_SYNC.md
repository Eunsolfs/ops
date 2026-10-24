# Twinkle 侧同步说明

> 关联分支：Twinkle 仓库 `slim/ops-cpp-only`（临时分支；用户要求全部处理完后移除）
> 关联分支：ops 仓库 `slim/cpp-only-vs`

## 1. 为什么要改 Twinkle

ops 精简后删除了 27 个 C API（点阵字库 / 本地字库 / 内存字库）。Twinkle 的
`modules/Twinkle.Background` 里原来有 27 个 `OPController` 封装直接调用这些 `Op*`
函数。如果只换 `op_c_api_x64.lib` 而不改 Twinkle，链接会报 `LNK2019`。

## 2. 已完成的 Twinkle 修改

文件：

- `modules/Twinkle.Background/include/OPController.h`
- `modules/Twinkle.Background/src/OPController.cpp`

删除的 27 个成员（声明 + 实现）：

```text
SetDict / GetDict / SetMemDict / UseDict / AddDict / SaveDict / ClearDict
GetDictCount / GetNowDict
SetBinaryPreprocess / GetBinaryPreprocess
FetchWord / FetchWordEx / ExtractWordRects / ExtractWordRectsEx
FetchWords / FetchWordsEx / FetchWordsByRects
GetBinaryPreview / GetWordPreview / CheckWordDict / NormalizeWordDict
RenameWordDict / GetWordsNoDict
GetWordResultCount / GetWordResultPos / GetWordResultStr
```

保留：

```text
Ocr / OcrEx / FindStr / FindStrEx / OcrAuto / OcrFromFile / OcrAutoFromFile / FindLine
```

这些保留项在新 ops 中仍然存在，走 HTTP OCR（`FindLine` 不依赖 OCR 服务）。

`Twinkle.Ocr` 模块是 HTTP OCR 的 IPC 客户端，不引用上述被删能力，无需修改。

## 3. 换库步骤（发布/联调时）

新 ops 产物：

```text
external/ops/include/op_c_api.h
external/ops/lib/x64/op_c_api_x64.lib
external/ops/bin/x64/op_c_api_x64.dll
external/ops/bin/x64/op_x64.dll
```

同步到 Twinkle 使用的位置：

> Release 构建 `op_c_api_x64` 的 PostBuild 已自动复制 DLL / 导入库 / `op_c_api.h`
> （`/p:OpSyncTwinkle=false` 可关闭）。下面的手动命令仅在关闭自动同步或独立拷贝时使用。

```powershell
# 头文件（Twinkle 所有工程通过 external\OP\op-include 引用）
Copy-Item external\ops\include\op_c_api.h external\OP\op-include\op_c_api.h -Force

# 导入库 + DLL（Twinkle.Background 的工程引用目录）
Copy-Item external\ops\lib\x64\op_c_api_x64.lib modules\Twinkle.Background\lib\ -Force
Copy-Item external\ops\bin\x64\op_c_api_x64.dll  modules\Twinkle.Background\lib\ -Force
Copy-Item external\ops\bin\x64\op_x64.dll        modules\Twinkle.Background\lib\ -Force
```

然后重新构建 Twinkle（至少 `Twinkle.Background` + 任一链接 `op_c_api_x64.lib` 的 exe）。
测试工程（`ColorSelfTest` / `PipelineTest` / `Bench` 等）的 PostBuild 会从
`modules\Twinkle.Background\lib` 复制 DLL 到输出目录。

> `modules\Twinkle.Background\lib` 与 `external\OP` 在 Twinkle 的 `.gitignore` 中，
> 这些二进制/头文件不会进入 Twinkle 的 git 历史。

## 4. x86 说明

x86 已完整配置并构建（`op.sln` 的 `Release|Win32`）：

- 依赖：`pwsh -File scripts\bootstrap-deps.ps1 -Arch x86`
- 产物：`bin\x86\op_x86.dll`、`op_c_api_x86.dll`、`tools.dll`、`cpp_call.exe`、
  `op_test.exe`；`lib\x86\libop.lib`、`op_c_api_x86.lib`
- PostBuild 已把 x86 的 `op_x86.dll/.lib`、`op_c_api_x86.dll/.lib` 同步到
  `modules\Twinkle.Background\lib`

注意：Twinkle 主解决方案的 Win32 配置本身不完整（`build\Twinkle.sdk.props` 硬编码
`op_c_api_x64.lib`，且很多工程 Win32→x64）。本次 x86 只在 `op.sln` 验证；在
`Twinkle.sln` 里 OP 工程的 Win32 目前仍映射到 x64。若要让 Twinkle 真正 x86，需要：

1. 把 `Twinkle.sdk.props` 的 `AdditionalDependencies` 按 `$(Platform)` 切到
   `op_c_api_x86.lib`；
2. 把所有 Twinkle 工程的 Win32 映射从 x64 改成 Win32；
3. 重新验证整条链路。

## 5. 已验证

- `OPController.h/.cpp` 删除后，`Twinkle.Background.vcxproj` Release|x64 编译通过
  （0 警告 / 0 错误）。
- `ColorSelfTest.vcxproj` Release|x64 使用新 `op_c_api_x64.lib` 链接通过
  （0 警告 / 0 错误），PostBuild 复制的新 DLL 与 ops 产物哈希一致。
- ops 新 `op_c_api.h` 已无被删的 27 个声明；新 `op_c_api_x64.lib` 已无对应导出。

## 6. 历史文档

- `docs/23_OP_C_API更新.md` 的 OCR 列表已更新为 HTTP OCR / FindStr / FindLine。
- `docs/_research/p1_colorfix_report.md` 中提到的 `OpGetDictCount` /
  `OpGetWordResultCount` 已加“历史记录”说明。

## 7. DPI 感知（2026-10-25 补充）

旧版 `OpCreate` 会无条件调用 `SetProcessDPIAware()`，把整个 Twinkle 进程改成
系统 DPI 感知：OP 帧变成物理像素（150% 下 `1704x1014`），而 MuMu/ADB native 仍是
`1136x640`，混用 provider 时坐标差 1.5 倍。

现在 ops 默认 **不修改**宿主 DPI，因此 Twinkle **不需要改代码**：加载新版
`op_c_api_x64.dll` 后，OP 不再把进程改成 DPI 感知，OP 帧与 MuMu/ADB native 一样
是逻辑坐标（150% 下约 `1136x676`，而不是 `1704x1014`）。

如果以后需要 OP 使用物理像素（例如想要更清晰的截图），必须在 **进程启动时、第一次
`OpCreate` 之前**显式调用：

```cpp
OpSetProcessDpiAware(1);   // 必须早于任何 OpCreate
```

不要把 `OpSetProcessDpiAware(0)` 写进 `OPController` 构造函数：那会覆盖宿主在启动时
的 opt-in。默认值已经是 0，宿主不调用即可。

注意：DPI 开关只解决“加载顺序决定坐标系”；native `1136x640` 与 GDI/OP
`1136x676` 的尺寸差仍需在 `CompositeController` 做坐标归一。

## 8. 加入 Twinkle.sln（2026-10-25）

`Twinkle.sln` 新增解决方案文件夹 **OP**，包含：

- `libop`
- `op_x64`
- `op_c_api_x64`
- `tools`
- `cpp_call`
- Solution Items：`external\ops\op.sln`、`external\ops\doc\cpp-only\BUILD_VS.md`（只读入口）

配置映射：5 个 OP 工程补齐 Debug/Release × Win32/x64；**Win32 目前映射到 x64**
（Twinkle 主解决方案的 Win32 本身不完整，见第 4 节）。solution 级依赖已加到
`Twinkle.Background`、`Twinkle.CLI`、`PipelineTest`、`ColorSelfTest`、
`AutomationRegistryTest`、`DecoupleTest`、`EmulatorProbe` → `op_c_api_x64`。

命令行 target 命名注意（MSBuild 17.11）：

```powershell
# 正确：解决方案文件夹限定 + 点转下划线
MSBuild Twinkle.sln /t:"OP\op_c_api_x64" /p:Configuration=Release /p:Platform=x64 /m
MSBuild Twinkle.sln /t:Twinkle_CLI /p:Configuration=Release /p:Platform=x64 /m
```

`/t:op_c_api_x64`（没有 OP\ 前缀）和 `/t:Twinkle.CLI`（target 名不能带点）都会失败。
`Bench` / `Soak` / `WindowProbe` 不在 `Twinkle.sln` 中，本次没有为了加依赖而新增它们。

构建 `OP\op_c_api_x64` 后，其 PostBuild 会把 DLL/LIB 同步到
`modules\Twinkle.Background\lib`（已实测 hash 一致）。
