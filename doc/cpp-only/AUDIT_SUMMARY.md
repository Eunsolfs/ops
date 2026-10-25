# OP 精简分支审计汇总（slim/cpp-only-vs）

- 审计基线：`slim/cpp-only-vs` @ `cdb2eb3`（对照上游 `d00f837`）
- 审计日期：2026-10-25
- 代码修复提交：`188cfae`（`fix(audit): 修复削减分支的 P0/P1 隐藏问题并补充审计`）
- 后续最优优化提交：`1ccc557`（COM IID 1.1、x86 C API 别名、OCR 并发、int64 积分图、Debug 输出隔离）
- 本轮选择优化提交：`3de65a8`（FindPic/GDI 缓存、DPI 实际状态、Delay/KeyPressStr、C API 契约、测试完善）
- COM 接口清理提交：`ea7d350`（删除旧 `IOpAutomation`，改用 `IOpAutomation2`）
- 当前状态：工作区已提交；本文件与三份详细报告同批入库，后续文档更新见 `git log`。
- 详细报告：
  - [AUDIT_RUNTIME.md](AUDIT_RUNTIME.md)：运行时/线程/资源/性能，16 条
  - [AUDIT_OCR_API.md](AUDIT_OCR_API.md)：字库 OCR 删除后的 API/COM/测试一致性，15 条
  - [AUDIT_BUILD.md](AUDIT_BUILD.md)：原生 VS / x86 / 依赖脚本 / op_test，18 条
- 三份报告是“修复前 HEAD”的快照；本文件记录**当前工作区**的状态。

## 1. 结论

- Release / Debug × x64 / Win32 **四个配置均 0 警告 / 0 错误**。
- `op_test` 共 138 x64 用例 / 137 x86 用例；x64 核心集 13/13 PASS，x86 核心集 10/10 PASS（含大图积分图、DPI、
  x86 C API 别名、COM IID/注册入口测试）。
- `cpp_call.exe`：`Ver = 0.4.8.3`、`OpenCV = 5.0.0`。
- 已修复 2 个 P0、4 个 P1、5 个 P2；剩余项主要是兼容性决策、性能/测试缺口和跨仓库集成。

## 2. 已修复

| ID | 级别 | 问题 | 修复 |
| --- | --- | --- | --- |
| P0-1 | P0 | `BindWindowEx` 参数校验失败时先 `reset_bind_state(false)` 清空 `_mouse/_keyboard`，early-return 后输入 API 空指针 | [BindingSession.cpp](../libop/binding/BindingSession.cpp)：参数/OS 校验全部通过后才释放旧绑定；失败保留当前绑定或默认输入 |
| P0-2 | P0 | GDI printwindow 用“当前窗口尺寸”写共享内存，容量按绑定时尺寸分配；窗口 resize 后越界写 | [GdiCapture.cpp](../libop/capture/backends/GdiCapture.cpp)：新增 `ensure_shared_capacity`；校验 `ww*wh*4` 容量与 `src_x/src_y/w/h` 边界后再 `GetDIBits`/行拷贝 |
| P1-3 | P1 | `point_t::operator<` 不是严格弱序，却被当 `std::map` 键 → UB/OCR 框丢失 | [Types.h](../libop/base/Types.h)：改为 `(y/9 行带, x, y)` 严格弱序；OCR 改用 `std::vector` + `stable_sort`，不再用 map 去重 |
| P1-4 | P1 | `ImageSearchService::OCR` 无条件返回 1，HTTP 失败伪装成“空结果” | [ImageSearchService.cpp](../libop/image/ImageSearchService.cpp)：HTTP 失败返回 0 并记日志；成功 1 |
| P1-6 | P1 | UTF-8 → CP936 → CP_ACP 双重有损转换 | [Utils.h](../libop/base/Utils.h) 新增 header-only `utf8_to_wstring`（直接 CP_UTF8）；OCR/YOLO 改用 |
| P1-D | P1 | Debug 使用 `/MTd`，与 Release 版 OpenCV/gtest/BlackBone 的 `/MT` 冲突（LNK2038） | [op.common.props](vs/op.common.props)：Debug 也使用 `/MT`；四个配置均可链接 |
| P1-G | P1 | `bootstrap-deps.ps1` 不安装 gtest，干净机器 `op_test` 必 LNK1104 | [bootstrap-deps.ps1](../scripts/bootstrap-deps.ps1)：安装并校验 `gtest:<triplet>` |
| P1-S | P1 | PostBuild 任何配置都覆盖 Twinkle 的 DLL/LIB；头文件靠手工复制 | [op_x64.vcxproj](../op_x64.vcxproj)、[op_c_api_x64.vcxproj](../op_c_api_x64.vcxproj)：仅 `Release` 且 `OpSyncTwinkle=true` 时同步；`op_c_api` 同时同步 `op_c_api.h` |
| P2-4 | P2 | `_max_return_obj_ct` 判 `>` 导致实际返回 1801 | 三处 `Find*Ex`/`FindColorBlockEx` 改为 `>=` |
| P2-5 | P2 | `OcrFromFile*` 不检查 `Image::read` 失败 | [ImageSearchService.cpp](../libop/image/ImageSearchService.cpp)：读图失败返回 0 |
| P2-9 | P2 | `FindLine` 空二值图返回 `-1` 却输出伪造 `"0,0"` | [ImageSearchAlgorithms.cpp](../libop/image/ImageSearchAlgorithms.cpp)：空图返回 0 且输出空串；`Op::FindLine` 先清空 |
| P2-COM | P2 | COM `FindStr` 不判空 `retx/rety` | [OpAutomation.cpp](../libop/com/OpAutomation.cpp)：返回 `E_POINTER` |
| P2-D | P2 | OCR 测试默认探测 tesseract:8080，但代码默认 paddle_ncnn:8082 | [test_support.cpp](../tests/test_support.cpp) 与 [doc/ocr.md](doc/ocr.md) 对齐 8082 |
| P2-A | P2 | 文档把被删 API 写成 28 个/导出 198/213；Debug 限制过时 | 文档改为 27、200/215，并更新 Debug/PostBuild/x86 说明 |

## 2.1 本轮追加优化（2026-10-25 07:00）

- **COM 早期绑定**：旧 `IOpAutomation` 接口已删除；新接口 `IOpAutomation2`，IID
  `59d588f8-b39e-4427-9826-c1fba566318d`，typelib `1.1`，`IDispatchImpl` 1,1；旧 early-binding
  客户端 QI 旧 IID 时干净失败，late binding 不受影响。
- **x86 GetProcAddress**：新增 [op_c_api_x86.def](../libop/c_api/op_c_api_x86.def)，同时导出
  `_OpXxx@N` 与 `OpXxx` 别名；x86 共 400 个导出。`UtilsTest.X86CapiUndecoratedAliases` 已验证。
- **OCR 并发**：配置锁只保护 endpoint/timeout 快照，HTTP 放锁外；最多 4 个在途请求，
  不再因单次后端卡死阻塞 `SetOcrEngine`/全进程 OCR。
- **积分图**：`_sum` 从 `Image`(int32) 改为 `ImageSum`(int64)，`region_sum`/`gray_norm`/
  `real_match`/`FindLine` 全部 64 位；5K/双 4K 不再回绕。
- **Debug 输出隔离**：Debug 产物改到 `bin/{x64|x86}/Debug/`、`lib/{x64|x86}/Debug/`，
  避免同名 `op_x64.lib`/DLL 与 Release 互相覆盖；Twinkle 仍只使用 Release。
- **COM 异常边界**：`OpAutomation::Invoke` 统一 `try/catch(...) -> E_FAIL`，覆盖 late-binding 调用。
- **FindPic 帧缓存**：采集后把 `FrameInfo.frameId` 传入 `ImageSearchService`；同一帧复用
  `_gray` + 灰度积分图，`FindColorBlock`/`FindLine` 覆写 `_sum` 后会自动重建。
- **GDI 位图复用**：`GdiCapture` 同尺寸复用 `CreateCompatibleBitmap`，不再每帧
  Delete/Create；窗口 resize 时按新尺寸重建。
- **DPI 实际状态**：`OpGetProcessDpiAware` / `IsProcessDpiAware` 改为查询 OS
  （`GetThreadDpiAwarenessContext`，旧系统回退 `IsProcessDPIAware`）。
- **Delay/KeyPressStr**：`Delay` 改 `MsgWaitForMultipleObjectsEx` 等待，不再忙等占 CPU；
  NORMAL/NORMAL2 无字符延迟时 `KeyPressStr` 合并为批量 `SendInput`。
- **C API 契约**：`op_c_api.h` 补充线程安全、`const wchar_t*` 生命周期、句柄销毁约束。
- **测试**：新增大图积分图、DPI 实际状态、x86 导出别名、COM IID、COM 注册入口/类工厂；
  OCR 用例补 `EXPECT_NO_FATAL_FAILURE`/结果格式断言。

## 3. 仍建议处理（未改代码）

| 优先级 | 问题 | 建议 |
| --- | --- | --- |
| — | `check_bind()` 未绑定时隐式绑定桌面窗口 | 用户确认不需要，保持现状 |
| — | HTTP OCR 正则解析/响应上限 | 用户确认暂时不做 |
| 低 | vcpkg 整根 include、MIDL Clean 残留、BlackBone 未 pin、OpenCV zip 无校验和、Release 无 PDB | 收窄 include；补齐 Clean/版本固定/PDB 策略 |

## 4. 测试缺口（剩余）

- 绑定失败后再次输入（P0-1 回归）。
- GDI 窗口 resize 后截图（P0-2 回归）。
- `FindLine` 空图/单像素/正常直线（目前零用例）。
- OCR 结果顺序/同坐标框/失败语义（需要可注入的 HTTP stub 或把解析抽成纯函数）。
- 真正的 `regsvr32` 注册往返未在测试里执行（需要写 HKCR/HKLM；当前测试覆盖
  `DllRegisterServer/DllUnregisterServer` 入口、类工厂与 IID 兼容）。
- 无 OCR 服务时 15 个 OCR 用例全部 SKIP，CI 仍等于零覆盖。
- 已完成：大图积分图（4096x2160）、DPI 实际状态、x86 导出别名、COM IID/入口、
  OCR `EXPECT_NO_FATAL_FAILURE`/格式断言。

## 5. 复核命令

```powershell
cd Y:\sourcecode\cpp\Twinkle\external\ops

# 四配置
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Debug /p:Platform=Win32 /m

# 核心测试（Release）
.\bin\x64\op_test.exe --gtest_filter="UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*"
.\bin\x86\op_test.exe --gtest_filter="UtilsTest.*:AlgorithmTest.*"

# Debug 在独立目录运行
.\bin\x64\Debug\op_test.exe --gtest_filter="UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*"
.\bin\x86\Debug\op_test.exe --gtest_filter="UtilsTest.*:AlgorithmTest.*"
```

> 若不想让 Release 构建写 Twinkle 的 `modules\Twinkle.Background\lib` / `external\OP\op-include`，
> 加 `/p:OpSyncTwinkle=false`。
