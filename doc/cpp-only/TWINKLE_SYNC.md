# Twinkle 侧同步说明

> 关联分支：Twinkle 仓库 `slim/ops-cpp-only`（临时分支；用户要求全部处理完后移除）
> 关联分支：ops 仓库 `slim/cpp-only-vs`

## 1. 为什么要改 Twinkle

ops 精简后删除了 28 个 C API（点阵字库 / 本地字库 / 内存字库）。Twinkle 的
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

本次只构建了 x64。`modules\Twinkle.Background\lib` 里旧的 `op_c_api_x86.dll` /
`op_c_api_x86.lib` / `op_x86.dll` 未替换；如果 Twinkle 需要 x86：

1. 给 ops 的 VS 工程补 Win32 配置；
2. 准备 x86 的 BlackBone / OpenCV / vcpkg 依赖；
3. 重新构建 x86 的 DLL/LIB 后再同步。

## 5. 已验证

- `OPController.h/.cpp` 删除后，`Twinkle.Background.vcxproj` Release|x64 编译通过
  （0 警告 / 0 错误）。
- `ColorSelfTest.vcxproj` Release|x64 使用新 `op_c_api_x64.lib` 链接通过
  （0 警告 / 0 错误），PostBuild 复制的新 DLL 与 ops 产物哈希一致。
- ops 新 `op_c_api.h` 已无被删的 28 个声明；新 `op_c_api_x64.lib` 已无对应导出。

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
