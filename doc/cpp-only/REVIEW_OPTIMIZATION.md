# OP 精简分支：优化与拓展审查（2026-11-05）

- 基线：`slim/cpp-only-vs` @ `e960b4c`
- 方法：静态通读关键路径 + 现有二进制/测试清单核对；**本文件不含代码改动**。
- 范围：优先 Twinkle 实际使用的 C API 路径（采集 / 输入 / 找图找色 / OCR / 内存），其次构建、测试与分发。
- 与前几份审计的关系：[AUDIT_SUMMARY.md](AUDIT_SUMMARY.md) 回答“削减后有没有坏”；本文件回答“现在还能更好、更全吗”，不重复已修项。

## 处理状态（2026-11-05 第 1 批）

| 项 | 状态 | 落地方式 |
| --- | --- | --- |
| O1 | ✅ 已处理 | 删除 `FindPicTh`/`FindPicExTh` 与 `m_threadPool`（线程路径全仓无调用） |
| O2 | ✅ 已处理 | 缓存项记录 `mtime+size` 并自动失效；`EnablePicCache(0)` 跳过自动缓存读取；`GetPicSize` 未命中读盘 |
| O3 | ✅ 已处理 | 日志默认 `0`（静默）+ `atomic<int>`；回调 API 留给错误体系批次 |
| O7 | ✅ 已处理 | `SetPath` 用 `GetFullPathNameW` 归一化为绝对路径 |
| O10 | ✅ 已处理 | Release 开启 `/Zi + /DEBUG`，产出 PDB |
| O13 | ✅ 已处理 | 新增 `OpCopyScreenData`（C++/C API/x86 def/头注释） |
| O17 | ✅ 已处理 | `OpAutomation.rgs` 的 TypeLib `Version` 改为 1.1 |
| O19 | ✅ 已文档化 | 行为保持不变，`delta_color` 仅透明模板生效写入头文件与代码注释 |
| G1 | ✅ 已处理 | 新增 `scripts/build.ps1`、`test.ps1`、`verify.ps1`（vswhere 定位 MSBuild） |
| G2 | ✅ 已处理 | 新增 `OpWarningsAsErrors` 开关（默认 false，CI/verify 可打开） |
| O8 | ✅ 已处理 | 颜色串解析结果按字符串缓存（上限 64 条） |
| O9 | ✅ 已处理 | 新增 `include/op_version.h` 作为唯一来源；`op.rc`/`op_c_api.rc`/`AutomationModes.h` 引用，测试校验一致性 |
| O24 | ◐ 部分处理 | DXGI staging 纹理复用 + 重建后 100ms 重试；GPU 子区域拷贝留待后续 |
| O27 | ✅ 已处理 | 移除绑定前的无条件 UnBind（参数校验失败保留旧绑定）；`IsBind` 校验窗口是否仍存在 |
| G4 | ✅ 已处理 | `OpCheckDependencies` 增加 gtest/minhook/directx-headers 检查；op_test 打开 `OpCheckGTest` |
| O4 | ◐ 部分处理 | 新增 `OpGetLastErrorCode`（OK/INVALID_HANDLE/INVALID_ARG/NOT_BOUND/CAPTURE/IO/BACKEND/INTERNAL）；OCR 后端错误尚未细分 |
| O18 | ◐ 部分处理 | 错误码覆盖无效 handle，但 handle magic/UAF 诊断未做 |
| O6 | ✅ 已处理 | 新增 `.github/workflows/ci.yml`：x64/x86 矩阵构建 + core 测试（依赖缓存），另有 docs 检查 job |
| O15 | ✅ 已处理 | 采集失败时 `GetColor` 返回空串（不再伪造 `"000000"`），错误码置 `CAPTURE` |
| O16 | ✅ 已处理 | COM 层增加 `prepare_variant` 守卫（25 处 VARIANT 解引用）与 NULL BSTR 检查；新增空指针用例 |
| G5 | ◐ 部分处理 | 修正 CLAUDE.md 架构描述与 `.clang-tidy` 用法；README 仍指向上游 release（fork 不发布二进制） |

### 待处理项评估（2026-11-05）

| 项 | 必要性 | 结论 |
| --- | --- | --- |
| O5 找图后端切换 | 待测量 | 会改变命中语义（并列命中取点），先补 G7 基准再决策 |
| O11 `OpSleep` | 文档即可 | 已加“阻塞消息泵，UI 线程用 Delay”注释，不改行为 |
| O12 结果字符串 | 可选 | 兼容接口保留；结构化结果由 E3 覆盖 |
| O14 返回值收敛 | 不建议 | 会破坏现有调用方（Twinkle 依赖 1/0/-1 组合）；错误码已能区分失败阶段 |
| O18 handle magic | 低收益 | 只能诊断不能防 UAF；错误码已覆盖无效 handle |
| O20 逐像素 SIMD | 待测量 | 无基准证据，改动面大 |
| O21 `FindPicEx` 排序 | 可选 | 需先固化跨模板命中顺序测试 |
| O22 透明模板预筛 | 待测量 | 仅在大量透明模板时收益明显 |
| O23 OCR 传输 | 当前不必 | Twinkle 使用自有 OcrPipeService，ops OCR 非热路径；启用后再优化 |
| O25 Hook 热路径 | 必要（专项） | 影响目标进程性能/正确性，需真机 DX 矩阵与 `FrameInfo` 版本化 |
| O26 输入批量 | 必要（专项） | windows/dx 模式长文本慢；协议改动需真机回归 |
| O28 采集自愈 | 专项 | 绑定核心改动，需真机切屏/窗口重建验证 |
| O29 时序/Unicode | 专项 | dead key/emoji/轨迹时长；需输入回归矩阵 |
| O30 帧观测/光标 | 专项 | 依赖 O25 的 `FrameInfo` 版本化 |
| E1–E4 | 可选 | 能力增强，按宿主需求排期 |
| E5 OCR 池 | 不必 | 同 O23 |
| E6 hook/memory/ipc 测试 | 必要（长期） | 零覆盖是最大质量风险，建议随专项逐步补 |
| E7 分发 | 部分完成 | `package.ps1` + NOTICES 已就绪；vcpkg/NuGet 可选 |
| G3 测试资产 | 必要（需素材） | 需要可再分发的图片与许可说明 |
| G7 bench/soak | 必要（下一步） | 是 O5/O20/O21/O22 决策的前提 |

> 本轮已执行：O6 / O15 / O16 / G5（部分）。正文保留原始分析；专项项建议单独开轮次并配真机矩阵。


---

## 0. 结论

### 0.1 Top 10（按“对 Twinkle 的实际影响 / 落地成本”排序）

| # | 项 | 为什么排在这里 | 成本 |
| --- | --- | --- | --- |
| 1 | O13 截图回传裸指针 → Twinkle 落盘 BMP 读回 | 每次截图多 2 次磁盘 I/O + 编解码，高频截图直接可感 | S |
| 2 | O3 日志默认弹 MessageBox、开关进程全局 | 一次内部错误就可能卡死宿主消息循环 | S |
| 3 | O2 模板缓存不看 mtime、`EnablePicCache(0)` 不生效 | 覆盖同名模板后**匹配结果错误**，不只是内存问题 | M |
| 4 | O19 `delta_color` 对非透明模板静默失效 | 参数看着生效实则被丢，属于“错得没有报错” | S |
| 5 | O1 每句柄预建 16 线程池，线程路径已废弃 | 空转资源，多控制器场景按句柄数线性放大 | S |
| 6 | O14 + O4 返回值域不一致、`GetLastError` 是 Win32 残留 | 上层无法区分“没找到/采集失败/后端失败” | M |
| 7 | O24 + O25 DXGI/Hook 采集热路径 | 每次截图一次全屏 GPU 拷贝 / 每帧重开共享内存 | S–M |
| 8 | O26 输入消息走 `SMTO_BLOCK 2000ms`，逐点/逐字符 IPC | 轨迹移动与长文本输入可被放大到秒级 | S–M |
| 9 | O16 + O17 COM 空指针/VARIANT 与注册脚本版本 1.0 | late-binding 客户端可触发崩溃；早期绑定可能解析失败 | S |
| 10 | O5 + O21 + O22 找图后端与扫描策略 | 大 ROI/多模板/透明模板的性能天花板 | M–L |

### 0.2 其余（按主题分布）

- **API 语义**：O7 路径规范化、O15 `GetColor` 假黑色、O18 句柄校验、O23 OCR 错误压平。
- **输入/绑定**：O27 绑定状态机、O28 采集自愈、O29 时序与 Unicode、O30 帧可观测性。
- **工程化**：O6 CI、O9 版本单源、O10 PDB、O11 `OpSleep`、O12 结果串解析，以及第 3 节的 G 系列。

---

## 1. 优化项（现有效能 / 健壮性）

### O1. 句柄级预创建线程池，而线程版找图已被绕开【P1，S】

**现状**

- `ImageSearchAlgorithms` 的构造函数直接构造线程池：`m_threadPool(default_thread_count())`，`default_thread_count()` = `std::thread::hardware_concurrency()`。
  [ImageSearchAlgorithms.cpp:311](../libop/image/ImageSearchAlgorithms.cpp)、[:118](../libop/image/ImageSearchAlgorithms.cpp)
- `ImageSearchService` 继承它，`OpContext` 又持有 `ImageSearchService image_proc`，所以**每次 `OpCreate` 都会拉起 N 个空闲线程**。
  [ImageSearchAlgorithms.h:214](../libop/image/ImageSearchAlgorithms.h)、[OpContext.h](../libop/op/OpContext.h)
- 线程版路径 `FindPicTh` / `FindPicExTh` 在仓库里**没有任何调用方**，测试也没有覆盖；服务层还明确注释绕开它：

  ```cpp
  // 不用 FindPicTh：该构建下线程池路径在 MuMu 子窗口/服务进程里恒返回 -1，
  // 非线程版 FindPic 同参数可命中（OP 官方测试工具也是这样）。
  ```

  [ImageSearchService.cpp:198](../libop/image/ImageSearchService.cpp)

**为什么值得改**

本机 `hardware_concurrency()==16`。Twinkle 是每个对象一个 `OPController`（Twinkle `modules/Twinkle.Background/src/OPController.cpp:40`），
多开时线程数按 `16 × 句柄数` 增长，全部空闲等待任务；每个线程还有默认 1MB 保留栈。

**建议（二选一）**

1. **推荐**：删除 `FindPicTh` / `FindPicExTh` / `m_threadPool`（约 200 行死代码），需要并行时改成 `std::async` 或全局共享池，按需创建。
2. 若想保留线程路径：改为懒加载（首次 `Th` 调用才建池），并用真机回归确认“恒返回 -1”的根因

   > 顺带记录：`FindPicTh` 的分块正确性依赖“扫描顺序 = 分块维度”。`dir` 为 5/6（x 优先）时按 y 分块，块内首个命中未必是全局首个，跨块比较可能选错点；
   > 若恢复该路径，需要按 `dir` 决定分块维度，或改为“先收集候选再统一比较”。

**验证**：`cpp_call.exe` 启动前后统计进程线程数（或用 Process Explorer 看单句柄）。

---

### O2. 模板缓存：无失效、无上限【P1，M】

**现状**

- `g_pic_cache` / `g_pic_match_cache` 是进程级 `std::map`，只有 `OpFreePic` 手动移除，没有容量/字节预算。
  [ImageSearchService.cpp:21](../libop/image/ImageSearchService.cpp)、[:54](../libop/image/ImageSearchService.cpp)
- **缓存命中不看文件时间**：`find_cached_pic(key)` 只按名字命中，不比较 mtime/size。
  [ImageSearchService.cpp:53](../libop/image/ImageSearchService.cpp)
- **`EnablePicCache(0)` 只关写入、不关读取**：`find_cached_pic` 在开关判断之前无条件执行。
  [ImageSearchService.cpp:438](../libop/image/ImageSearchService.cpp)
- `GetPicSize` 只查缓存、从不读盘，与 `FindPic` 的“缓存未命中就读文件”语义不对称。
  [ImageSearchService.cpp:385](../libop/image/ImageSearchService.cpp)

**影响（这是正确性问题，不只是内存）**：Twinkle 图色库允许**覆盖同名 PNG**。覆盖后同一进程内
`FindPic` 仍用旧模板，直到 `OpFreePic` 或重启；`EnablePicCache(0)` 也救不回来。
同时每个模板还持有灰度图 + 透明点列表（约原图 1.5–2 倍），动态模板名会让内存单调增长。

**建议**

1. 缓存项记录 `(mtime, size)`；命中时 `GetFileAttributesExW` 比对，不一致则重读（每次调用 1 次 stat，
   对毫秒级找图可忽略；也可按 1s 节流）。
2. `EnablePicCache(0)` 时**跳过读取**并清理自动缓存项（显式 `LoadPic` 的常驻项不受影响，文档写明）。
3. 加字节预算（默认 256MB）+ LRU；`OpGetPicCacheInfo` / `OpTrimPicCache`。
4. 修 `GetPicSize`：缓存未命中时按路径 `read_pic_file` 读尺寸（不驻留）。

**验证**：同进程内 `FindPic(a.png)` → 覆盖 a.png → 再 `FindPic(a.png)`，断言命中新图；缓存计数/字节数符合预算。

---

### O3. 日志默认弹模态框，且开关是进程全局【P1，S】

**现状**

- 默认值 `m_showErrorMsg = 1`，`setlog()` 在 `==1` 时执行 `MessageBoxA(NULL, ...)`。
  [Environment.cpp:26](../libop/base/Environment.cpp)、[Utils.cpp:135](../libop/base/Utils.cpp)
- `OpSetShowErrorMsg(handle, ...)` 写的是 `RuntimeEnvironment::m_showErrorMsg` 静态变量，不是 per-handle。
  [OpRuntime.cpp:65](../libop/op/OpRuntime.cpp)
- Twinkle 的 `OPController` 创建/绑定流程**没有**调用 `OpSetShowErrorMsg(0)`（全仓 grep 只有 DmService 在自建实例上设置）。

**影响**：`BindWindow` 失败、DXGI 抓帧失败、`SetPath` 失败等都会走 `setlog`；在宿主 UI 线程上就是一次模态阻塞。
带 `SET_ERROR_DIALOG` 的传统 COM 插件可以这样，但 Twinkle 是常驻自动化宿主，默认弹框不可接受。

**建议**

1. 默认改为 `0`（静默），或“仅 Debug 默认 1”。
2. 增加 `OpSetLogCallback(void (*)(int level, const wchar_t* msg, void* user), void* user)`，让宿主接管日志；
   文件/调试输出作为内置 sink。
3. 保持 `OpSetShowErrorMsg` 兼容，但改为“本进程内所有句柄共享”的显式文档化语义；更彻底的做法是放进 `OpContext`（per-handle），
   但这会改变现有调用方语义，建议放到下一个 typelib 版本。

**验证**：`cpp_call.exe` 里调用一次会失败的 `OpSetPath`，确认不再阻塞。

---

### O4. 错误语义缺失：`OpGetLastError` 返回 Win32 残留值【P1，M】

**现状**

```cpp
void op::Op::GetLastError(long *ret) {
    internal::set_result(ret, ::GetLastError());
}
```

[OpRuntime.cpp:61](../libop/op/OpRuntime.cpp)

而库内失败路径几乎不调用 `::SetLastError`（全仓只有 [InputHook.cpp:282](../libop/hook/InputHook.cpp) 一处）。
所以这个 API 返回的是“最近一次无关 Win32 调用”的错误码：可能非 0（看起来有错）、也可能 0（明明失败）。
C API / COM / Twinkle 都无法据此判断失败原因。

C API 的异常边界也和它脱节：`call_int` / `call_string` 用 `catch (...)` 统一吞掉，异常既不分类也不记录：

```cpp
try { ... } catch (...) { return 0; }          // call_int，[op_c_api.cpp:41](../libop/c_api/op_c_api.cpp)
try { ... } catch (...) { return L""; }        // call_string，[op_c_api.cpp:77](../libop/c_api/op_c_api.cpp)
```

**建议（新增，不改旧语义）**

1. `OpContext` 增加 `int last_error` + 分类枚举：
   `OP_ERR_OK / PARAM / NOT_BOUND / NOT_FOUND / IO / TIMEOUT / BACKEND / OCR_BACKEND / INTERNAL`。
2. 每个失败返回点统一 `set_last_error(ctx, code)`；成功清零。
3. 新 API：`OpGetLastErrorEx(handle, int* category, const wchar_t** message)` 或 `OpGetLastErrorString(handle)`；
   `OpGetLastError` 保留“Win32 错误码”语义以兼容。
4. COM 侧同步：失败时 `Err.Number = 0x8004xxxx`，`Err.Description` 取错误串。

**验证**：新增用例覆盖 `未绑定 / 文件不存在 / 参数越界 / 后端不可用` 四类，断言分类码稳定。

---

### O5. 自定义找图 vs OpenCV 匹配：两套栈能力不对等【P1，M–L】

**现状**

| 能力 | `FindPic`（自定义，Twinkle 主用） | `TemplateMatcher`（OpenCV） |
| --- | --- | --- |
| 逐点扫描 + 灰度积分图 | ✅ | — |
| 条带切分（strip，降低大 ROI 延迟） | ❌ | ✅ [TemplateMatcher.h:253](../libop/opencv/TemplateMatcher.h) |
| 多尺度（pyramid/scales） | ❌ | ✅ [TemplateMatcher.h:256](../libop/opencv/TemplateMatcher.h) |
| 透明/掩码匹配 | 透明角点近似 | ✅ `LoadMaskedTemplate` |
| 特征/边缘/形状匹配 | ❌ | ✅ |
| 相似度返回值 | ❌（只返回坐标） | ✅ `MatchResult.score` |

`FindPic` 是外层模板循环、内层整图扫描（[ImageSearchAlgorithms.cpp:748](../libop/image/ImageSearchAlgorithms.cpp)），
单模板复杂度 O(W·H)，多模板线性叠加；ROI 大、模板多时是主要耗时点。
OpenCV 的 `matchTemplate(TM_CCOEFF_NORMED)` 有 DFT/SIMD/线程优化，且已有 strip 分块实现，可直接复用。

**建议（先量后改）**

1. 先补基准：把 `OpenCvBenchmark.DISABLED_MatchingHotPaths` 打开（或新增 `FindPic` bench），用仓库里的真实素材，记录
   `2560×1440`、`1920×1080` 下 1/4/8 模板的 P50/P95。
2. 若基准显示 `FindPic` 明显落后：在 `ImageSearchService::FindPic` 里按 `ROI 面积 × 模板数` 选择后端——
   大面积走 `opcv` 的 `matchTemplate` + strip，小面积保留现有逐点算法（避免 OpenCV 初始化开销）。
3. 统一相似度语义：`FindPic` 的 `sim` 目前映射为 `0.5 + sim/2`（[ImageSearchService.cpp:197](../libop/image/ImageSearchService.cpp)），
   与 `OpCvMatchTemplate` 的 `threshold` 不一致；至少在文档里写清两者口径，避免调用方混用。

**风险**：切换后端会改变“并列命中取哪个点”的结果，需要黄金用例固化（现有 `FindPicHonorsDirection` 等可复用）。

---

### O6. CI 缺失，质量门禁全靠人工【P1，M】

**现状**：`.github/workflows/`、`ci/` 均为空目录（上游 `ci.yml` 随 CMake 链一并删除，未补原生 VS 版本）。
当前“四配置 0 警告 / x64 139 用例”只在本地执行过。

**建议**：GitHub Actions `windows-2025` 单 job 矩阵：

```yaml
strategy: { matrix: { arch: [x64, x86] } }
steps:
  - pwsh scripts/bootstrap-deps.ps1 -Arch ${{ matrix.arch }}
  - msbuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=${{ matrix.arch }} /m
  - msbuild op.sln /t:Rebuild /p:Configuration=Debug   /p:Platform=${{ matrix.arch }} /m
  - ./bin/${{ matrix.arch }}/op_test.exe --gtest_filter=-OcrFixture.*:IntegrationTest.*
```

- OCR 用例依赖外部服务：CI 用 stub（见 E5）或先排除，另设 `OCR protocol` job。
- 窗口/输入类用例在 GitHub 托管 runner 的交互式桌面下可跑；`IntegrationTest.BindUnbindFurKillerIfPresent` 之类要按环境 skip。
- 产物上传 PDB（见 O10）。

**成本**：依赖 bootstrap 在 CI 上首次要编译 OpenCV，建议加 `actions/cache`（把 `build/_deps` 与 vcpkg 安装目录缓存起来）。

---

### O7. `SetPath` 相对路径未规范化，会叠加【P2，S】

**现状**：第二条分支校验用的是解析后的 `fpath`，保存的却是入参 `path`：

```cpp
if (::PathFileExistsW(fpath.data())) {
    m_context->curr_path = path;   // ← 原始串，可能是相对路径
```

[OpRuntime.cpp:38](../libop/op/OpRuntime.cpp)

后果：连续两次相对 `SetPath("res")`、`SetPath("sub")` 会得到 `res\sub` 相对当前目录，`GetPath` 也返回相对串；
换工作目录后同一路径失效。

**建议**：保存前统一 `GetFullPathNameW` + 去除尾部分隔符；`GetPath` 返回绝对路径（与 `GetBasePath` 语义对齐）。
顺带把 `SetPath` 的 `replacew` 逻辑抽成 `normalize_path()` 供其它路径入口复用。

---

### O8. 颜色串每次调用都重新解析【P2，S–M】

**现状**：`FindColor`/`FindMultiColor`/`GetColorNum` 每次都走 `str2colordfs()`（split + 逐项 `str2color`），
而轮询场景里同一颜色串会被调用成千上万次。[ImageSearchService.cpp:299](../libop/image/ImageSearchService.cpp)

**建议**：`ImageSearchService` 内加一个小型解析缓存（key=颜色串，value=`vector<color_df_t>`，上限如 64 条，LRU），
或新增 `OpPrecompileColor(color, &id)` + `OpFindColorById(id, ...)` 显式句柄式 API（更彻底，但要扩 API）。

**收益**：单次调用可省 1–5µs 与若干次堆分配；对高频轮询（10–50Hz）是可测的。

---

### O9. 版本号四处手工同步，公共头无 ABI 版本【P2，S】

- C++：`MAKE_OP_VERSION(0, 4, 8, 3)`（[AutomationModes.h:105](../libop/base/AutomationModes.h)）
- 工程：`OP_VERSION="0.4.8.3"`（[op.common.props:31](../vs/op.common.props)）
- 资源：`FILEVERSION 0,4,8,3`（[op_c_api.rc:9](../libop/c_api/op_c_api.rc)、`op.rc` 同）

任一处漏改就出现“DLL 属性版本 ≠ `OpVer()`”；`include/op_c_api.h` 也没有任何版本/ABI 宏，
本 fork 删了 27 个 C API、typelib 已升 1.1，但产品版本仍是上游的 0.4.8.3。
**建议**：`vs/op.version.props` 或 `include/op_version.h` 作唯一源（RC 支持 `#include` 宏），
C++/资源/工程都从它取；公共头加 `OP_VERSION_*` + `OP_ABI_VERSION`；`op_test` 加一条“`OpVer()` == 资源版本”的用例。

---

### O10. Release 无 PDB，线上问题无法符号化【P2，S】

**现状**：Release 显式关闭调试信息。

```xml
<DebugInformationFormat>None</DebugInformationFormat>
<GenerateDebugInformation>false</GenerateDebugInformation>
```

[op.common.props:92](../vs/op.common.props)

DLL 崩溃在 Twinkle 现场只能看到模块内偏移。**建议**：Release 加 `/Zi` + 链接 `/DEBUG /OPT:REF /OPT:ICF`（不影响优化），
PDB 不进安装包、单独归档/随 release 附件上传；CI 里作为 artifact 保存。

---

### O11. `OpSleep` 阻塞消息泵，与 `OpDelay` 语义不一致【P2，S】

- `OpSleep` → `::Sleep()`（[OpRuntime.cpp:70](../libop/op/OpRuntime.cpp)），会阻塞调用线程的消息循环；
- `OpDelay` 已改为 `MsgWaitForMultipleObjectsEx`（见 `AUDIT_SUMMARY`）。

Twinkle 两者都在用（`OPController.cpp:364` 调 `OpSleep`）。**建议**：`OpSleep` 内部改走同一 `Delay` 实现（语义从“硬睡”变成“可处理消息的等待”），
或在头文件明确标注“会阻塞消息泵，UI 线程请用 OpDelay”。

---

### O12. `FindPicExS` / 结果字符串拼接的解析成本【P3，S】

批量结果用 `"x,y|name,x,y|..."` 拼接（[ImageSearchService.cpp:222](../libop/image/ImageSearchService.cpp)），
调用方要二次解析、且名字里含 `,`/`|` 时无转义。**建议**：新增结构化版本（见 E3），旧格式保持不变。

---

### O13. `OpGetScreenData` 回传裸指针，宿主被迫落盘截图【P1，S】

**现状**：`OpGetScreenData` 返回 `m_context->screenData.data()`，是 handle 内部的 `std::vector` 缓冲；
同一 handle 的下一次调用就会覆盖它，但头文件的契约块只写了 `const wchar_t*`，没写这个缓冲。
[OpImage.cpp:219](../libop/op/OpImage.cpp)、[op_c_api.h:224](../include/op_c_api.h)

调用方（Twinkle）因此不敢直接用，改成：

```cpp
// OP C API 只有 OpCapture(rect,file)（落盘），没有裸缓冲回传（OpGetScreenData 返回的是
// 进程内地址）。实现为：OpCapture → 临时 BMP → ImageFile::LoadBmp 读回 BGRA → 删文件。
```

Twinkle `modules/Twinkle.Background/src/OPController.cpp:132`

每次截图 = 一次 BMP 编码 + 一次磁盘写 + 一次读 + 一次解码 + 删除，高频截图时成本远高于采集本身，
且临时文件会被杀软扫描（这也是 Twinkle 侧“截图偶发慢”的合理怀疑点）。

**建议**：新增拷贝式 API，旧接口保留但标 DEPRECATED：

```c
// 返回 1=成功, 0=失败, -2=容量不足（此时 *needed 给出所需字节数）
OP_C_API int OP_CALL OpCopyScreenData(op_handle, int x1,int y1,int x2,int y2,
                                      int mode, void* dst, int dst_size,
                                      int* width, int* height, int* stride, int* needed);
```

- `mode` 语义与 `OpSetScreenDataMode` 一致（上下翻转/原序）；
- 顺带修 `OpGetScreenFrameInfo` 在未绑定时返回 0/0 与“真的是第 0 帧”不可区分的问题（可用 `frame_id==0 && time==0` 表示无帧，文档写明）。

**验证**：Twinkle 的 `Screenshot()` 改为直接调用新 API，对比前后 P50/P95 与临时文件数量。

---

### O14. 返回值域不统一，且采集失败静默返回【P1，S–M】

**现状**

| API | 成功 | 失败 |
| --- | --- | --- |
| `OpFindColor` | 1 | 0 |
| `OpFindPic` | 0..N-1（命中模板下标） | **-1**（未命中或采集失败，不可区分） |
| `OpFindPicEx` | 命中数 | 0 |
| `OpBindWindow` | 1 | 0 |
| `OpGetLastError` | — | Win32 残留值 |

[OpImage.cpp:121](../libop/op/OpImage.cpp)、[OpImage.cpp:63](../libop/op/OpImage.cpp)

而 `capture_region` 在采集失败时只打日志、直接 return，回调不执行，调用方拿到的就是预置值：

```cpp
if (!context->bkproc.requestCapture(...)) {
    setlog("error requestCapture");
    return;
}
```

[OpCaptureHelpers.h:17](../libop/op/OpCaptureHelpers.h)

对 `FindPic` 而言，“采集失败”与“未找到”同为 -1；对 `FindColor` 而言同为 0。
头文件也没有逐个函数写返回值含义。

**建议**：返回值语义收敛到 `0=失败 / 1=成功`，具体结果用出参；`FindPic` 的模板下标/`-1` 语义保留但
新增 `OpFindPic` 的显式出参版本。与 O4 的错误码一起做，一次改文档 + 一次回归。

---

### O15. `GetColor` 采集失败返回 `"000000"`，宿主无法与真实黑色区分【P2，S】

```cpp
void op::Op::GetColor(long x, long y, std::wstring &ret) {
    color_t cr;                    // 零初始化 → 黑色
    ... with_captured_region(...); // 失败时回调不执行，cr 保持黑
    ret = cr.towstr();             // "000000"
}
```

[OpImage.cpp:180](../libop/op/OpImage.cpp)

Twinkle 用 `color.empty()` 判失败（`OPController.cpp:832`），会把这个假黑色当成功结果。
**建议**：失败时返回空串并在 `ret` 之外带错误码（C API 侧返回 0）；或改成 `const wchar_t*` + 失败返回 `L""`，
与 `OpGetColor` 的现有调用方约定对齐（这是一次行为变更，需要 Twinkle 同步）。

---

### O16. COM 层 25 处 VARIANT 裸解引用、NULL BSTR 崩溃【P1，S】

**现状**

- `x->vt = VT_I4;` 之类的写法遍布 `OpAutomation.cpp`（`ClientToScreen`、`GetClientRect` 等 25 处），
  指针为 NULL 时直接 AV；`GetIDsOfNames`/`Invoke` 的 `catch (...)` 只把异常变成 `E_FAIL`，
  但 AV 不是 C++ 异常，会击穿到宿主。
  [OpAutomation.cpp:217](../libop/com/OpAutomation.cpp)
- `SetPath(BSTR path)` 直接把 BSTR 传给 `std::wstring fpath = path;`，NULL BSTR 立即崩。
  [OpAutomation.cpp:100](../libop/com/OpAutomation.cpp)、[OpRuntime.cpp:24](../libop/op/OpRuntime.cpp)

**建议**：加两个 helper 并机械替换：

```cpp
static bool ensure_out(VARIANT* v, VARTYPE vt) { if (!v) return false; VariantInit(v); v->vt = vt; return true; }
static bool ensure_out(LONG* p) { return p != nullptr; }
```

每个方法首行 `if (!x || !y || !ret) return E_POINTER;`；BSTR 统一走 `safe_text()`（C API 已有等价实现）。
再加一条 COM 空指针用例（把每个 out 参数传 NULL，断言返回 `E_POINTER` 而不是崩）。

---

### O17. 注册脚本的 TypeLib 版本仍是 1.0，与 typelib 1.1 不一致【P1，S】

- `libop/com/OpAutomation.rgs`：`Version = s '1.0'`
- `libop/com/op.idl:244`：`version(1.1)`
- `OpAutomation.h:31`：`IDispatchImpl<..., /*wMajor=*/1, /*wMinor=*/1>`

CLSID 下记录的 typelib 版本与注册表实际注册的版本不一致时，部分 early-binding 客户端
（VB6/旧 VBA/部分 .NET 互操作）会按 1.0 去取类型库而失败或取到旧描述。
**建议**：`.rgs` 改为 1.1 并加一条注册后校验（读 `HKCR\CLSID\{...}\TypeLib` 的 Version 与 typelib 对比）。
注：`regsvr32` 往返仍未纳入自动测试（见 AUDIT_SUMMARY），这条只能靠静态校验 + 手工验证。

---

### O18. C API 句柄无校验，误用即 UAF【P2，S】

`op_c_context` 只有 `op::Op` 和 `std::wstring`，没有 magic/owner 信息；`call_int` 仅判 `nullptr`，
`OpDestroy` 直接 `delete`。销毁后继续调用是 UAF，重复销毁是 double free。
[op_c_api.cpp:28](../libop/c_api/op_c_api.cpp)、[op_c_api.cpp:164](../libop/c_api/op_c_api.cpp)

**建议**（只能是“诊断”不是“隔离”）：`uint32_t magic = 'OPCT'` + 析构置 0；`Debug` 下记录创建线程，
跨线程调用时 `assert`（与头文件“handle 非线程安全”的契约一致）。`tests/` 目前没有任何 C API 用例，
补一个 `c_api_contract_test`（创建/销毁/空句柄/重复销毁）。

---

### O19. `delta_color` 对非透明模板静默失效【P1，S】

`FindPic` 的分发点只在“透明模板”分支把 `dfcolor` 传下去，非透明分支走 `real_match`，根本没有该参数：

```cpp
int match_ret = (use_ts_match ? trans_match<false>(j, i, pic, dfcolor, match->transparent_points, max_err_ct)
                              : real_match(j, i, &match->gray, match->gray_norm, sim));
```

[ImageSearchAlgorithms.cpp:766](../libop/image/ImageSearchAlgorithms.cpp)

也就是说：调用方传了 `delta_color`（Twinkle 的模板就带偏色参数），只要模板四角不同色（非透明判定），
这个参数就**完全不生效**，匹配仍按灰度相似度进行。这是“参数看起来生效、实则被丢”的静默行为差异。

**建议**：二选一并写进头文件与 `API_CHANGES.md`——
① 非透明模板也按 `dfcolor` 做逐通道容差（把 `dfcolor` 并入 `real_match` 的 SAD 上限）；
② 明确文档“`delta_color` 仅对透明模板有效”，并在 `OpFindPic` 返回 0 时给出可诊断信息。
若走 ①，需要黄金用例锁定“带偏色/不带偏色”的命中差异，避免改变现有调用方行为。

---

### O20. 逐像素热路径还在用标量 C++：FindColor / FindLine / 灰度转换【P2，M】

- `FindColor`/`FindColorEx` 每像素 `_src.at<color_t>(i,j)`（一次乘加）再逐通道比较，1080p 是 2.07M × 3 次比较；
  只有 `GetColorNum` 用了行指针。[ImageSearchAlgorithms.cpp:552](../libop/image/ImageSearchAlgorithms.cpp)、[:600](../libop/image/ImageSearchAlgorithms.cpp)
- `FindLine` 是朴素 Hough：每个前景像素 × 360 个角度，累加器 6.3MB 随机写，且每次重建 cos/sin 表。
  [ImageSearchAlgorithms.cpp:1085](../libop/image/ImageSearchAlgorithms.cpp)
- 灰度转换 `fromImage4` 每像素 3 乘 1 除；`record_sum` 先把 9.3MB 积分图 `fill(0)` 再全量覆盖（只有首行/首列需要清零）。
  [ImageSearchAlgorithms.cpp:1187](../libop/image/ImageSearchAlgorithms.cpp)

**建议**（先基准后改）：OpenCV 侧的 `cv::inRange` + `countNonZero`/`findNonZero` 已有成熟实现，且 `OpenCvHelpers.cpp`
已有同型 `Mat` 视图；把“多颜色 + sim 容差”预处理成阈值后一次 `inRange`，再按 `dir` 取首个命中。
灰度用定点 `(77R+150G+29B)>>8`，积分图只清首行首列。收益需要用 `DISABLED_MatchingHotPaths` 同类基准量化。

---

### O21. `FindPicEx` 全量收集→全排序→才截断；多模板重复全图扫描【P2，S–M】

- `FindPicEx` 把所有命中点 `push_back` 后才 `stable_sort` + `resize(1800)`。
  [ImageSearchAlgorithms.cpp:876](../libop/image/ImageSearchAlgorithms.cpp)、[:890](../libop/image/ImageSearchAlgorithms.cpp)
  因比较器是全序，**“排序后取前 1800”≡“按 dir 顺序扫描到 1800 就停”**，可直接去掉排序与全量存储。
- 多模板是“外层模板循环、内层整图扫描”：N 个模板 = N 遍全图。若模板尺寸相近，可改成“外层位置、内层模板”，
  一次扫描评估所有模板（早期退出策略不变）。

**收益**：`FindPicEx` 在高命中场景（如整屏同色）可省一次全量排序与上千个点的分配；多模板找图从 O(N·W·H) 降到 O(W·H)。
**风险**：`FindPicExS` 的返回顺序/截断语义要与现有用例对齐（`FindPicHonorsDirection` 等）。

---

### O22. 透明模板完全绕过积分图预筛，且有误判【P2，M】

`check_transparent` 的判据是“四角同色且占比 ≥50%”，任何白底对话框都会被判成透明模板；
之后 `trans_match` 逐前景点比较，没有 `region_sum` 粗筛。100×100 模板约 5000 个前景点 × 2M 位置 = 1e10 次比较，
与非透明路径的 2M 次积分图查询不在一个量级。[ImageSearchAlgorithms.cpp:133](../libop/image/ImageSearchAlgorithms.cpp)、[:781](../libop/image/ImageSearchAlgorithms.cpp)

**建议**：① 优先用真 alpha（`Image` 已拷 alpha 通道）而不是“四角同色”启发式；② 透明路径先用包围盒灰度/积分图粗筛，
再逐点精确比对；③ 或直接走 OpenCV `matchTemplate` + mask（`TemplateMatcher` 已有）。

---

### O23. OCR/YOLO 传输与错误语义【P2，M】

- **每次请求新建 WinHTTP 会话**（`WinHttpOpen` + `WinHttpConnect`），没有 keep-alive；轮询 OCR 时反复 TCP/TLS 建连。
  [HttpClient.cpp:174](../libop/network/HttpClient.cpp)
- **裸 BGRA base64**：1080p = 8.29MB → 11.06MB base64，再拼一份 JSON ≈ 单次 22MB 分配+拷贝。
  [OcrService.cpp:131](../libop/ocr/OcrService.cpp)
- **错误被压平**：后端 `-1..-9` 在 `ImageSearchService` 统一变成 `return 0`，`FindStr` 连返回值都不看；
  对调用方而言“后端挂了”与“没找到字”等价。[ImageSearchService.cpp:260](../libop/image/ImageSearchService.cpp)
- **YOLO 全局串行**：`detect` 全程持锁（含 HTTP），并发=1，且会阻塞 `SetYoloEngine`，与 OCR 的 4 槽不一致。
  [YoloDetector.cpp:77](../libop/yolo/YoloDetector.cpp)

**建议**：session 复用（每 endpoint 一个 `WinHttp` 会话 + 连接池）；可选 `"format":"png"`（`cv::imencode`，需
`OcrPipeService` 同步支持）；错误码透出到 `last_error`；YOLO 改与 OCR 相同的 `SlotGuard` 槽位模型。

---

### O24. 采集：DXGI 每帧新建 staging + 整屏拷贝；重建后可能“无新帧”【P1，S】

- 每帧 `CreateTexture2D` + 整屏 `CopyResource`，4K 每次 33MB 冗余 GPU 拷贝 + 一次驱动分配。
  [DxgiCapture.cpp:382](../libop/capture/backends/DxgiCapture.cpp)
- 读回按 4B/px 硬拷，`w/h` 只影响 CPU memcpy——GPU 侧没做子区域拷贝（`CopySubresourceRegion` + `D3D11_BOX` 可只拷请求矩形）。
- `WAIT_TIMEOUT` 且没有上一帧时直接 `false`；显示器切换/`ACCESS_LOST` 重建后，桌面静止就持续失败，直到画面变化。
  [DxgiCapture.cpp:364](../libop/capture/backends/DxgiCapture.cpp)
- 跨屏窗口“整图 fill 黑 + 只拷相交区”，黑边被当成有效像素，`_width/_height` 仍是整窗。
  [DxgiCapture.cpp:190](../libop/capture/backends/DxgiCapture.cpp)

**建议**：staging 按 `desc` 缓存复用（WGC 已这么做：`WgcCapture.cpp:721`）；`CopySubresourceRegion` 只拷请求矩形；
无新帧时阻塞 acquire 50–100ms 或回退一次 GDI；只有存在黑边时才 fill。

---

### O25. Hook 注入采集：每帧重开共享内存、每 Present 新建 staging【P1，M】

`D3D11Capture` 在目标进程内每帧执行：`GetBuffer` + `GetDevice` + 新建 staging + 整帧 `CopyResource` + `Map`，
并且**每帧重新 `OpenFileMapping`/`OpenMutex`**；还有 `static cnt=10` 的旧节流残留、不校验 `desc.OutputWindow` 是否是绑定窗口。
[D3D11Capture.cpp:52](../libop/hook/D3D11Capture.cpp)、[:135](../libop/hook/D3D11Capture.cpp)

D3D9 每 `EndScene` 新建 SYSTEMMEM 纹理 + 全帧 `GetRenderTargetData`；`opengl.std` 钩 `glBegin`，
每次 `glBegin` 一次全帧 `glReadPixels`。[D3D9Capture.cpp:61](../libop/hook/D3D9Capture.cpp)、[OpenGLCapture.cpp:67](../libop/hook/OpenGLCapture.cpp)

任一后端失败即 `set_capture_enabled(false)` 且无恢复；帧完整性只有头部 `chk`，写头先于写像素，
写方中途被杀会产出“头合法、像素半新半旧”的帧。[FrameInfo.h:9](../libop/capture/FrameInfo.h)

**建议**：setup 时缓存 mapping/mutex 句柄；加消费节流（`FrameInfo.consume_seq` + `min_interval_ms`）；
swapchain 过滤 `desc.OutputWindow`；staging 成员化；帧头 `seq` 写回做双缓冲标记；`opengl.std` 改钩 `wglSwapBuffers`。

> 注意：第 3、6、7、10 条建议都动 `FrameInfo`/共享头布局，必须**一次性版本化**并与
> `D3D11Capture.cpp:145` / `D3D12Capture.cpp:108` 的 `static_assert(sizeof(FrameInfo)==28)` 同步，否则新旧注入 DLL 混用会静默截错图。

---

### O26. 输入：消息通道阻塞调用线程，且无批量【P1，S–M】

- 所有消息型输入走 `SendMessageTimeout(..., SMTO_BLOCK, 2000)`：阻塞调用线程 2 秒且不泵消息。
  [InputMessageUtils.h:8](../libop/input/InputMessageUtils.h)
- 轨迹移动每步一次跨进程同步 `SendMessage`（步数上限 120），`KeyPressStr` 在 windows/dx 模式逐字符 1 次 IPC（200 字 = 200 次）。
  [WinMouse.cpp:184](../libop/input/mouse/WinMouse.cpp)、[DxKeyboard.cpp:97](../libop/input/keyboard/DxKeyboard.cpp)

**建议**：状态类消息（`MOUSEMOVE`/`WHEEL`/`KEYDOWN`/`KEYUP`/`CHAR`）改 `PostMessage`；
新增批量消息（`OP_WM_TEXT`/`OP_WM_PATH`，点数组走 `WM_COPYDATA`）把 O(n×IPC) 降到 O(1)；
`SendMessageTimeout` 区分“超时”与“失败”并回传。

---

### O27. 绑定状态机：先 UnBind 再校验、`IsBind` 不校验窗口【P2，S】

- `Op::BindWindow` 先 `UnBindWindow()` 再调 `BindWindow`，把 `BindingSession` 刚修好的“校验失败保留旧绑定”又抵消了。
  [OpInput.cpp:107](../libop/op/OpInput.cpp)
- `IsBind` 只看内部标志，不校验 `IsWindow`/后端是否 lost；窗口已销毁仍返回 1。
  [BindingSession.cpp:373](../libop/binding/BindingSession.cpp)
- `LockInput` 只在 DX 模式生效，其它模式静默返回 1，调用方以为锁上了。
  [BindingSession.cpp:317](../libop/binding/BindingSession.cpp)

**建议**：拆出无副作用的 `ValidateBindParams()`，校验通过后再 `UnBind`；同参数重复绑定走 fast path 返回 1；
`IsBind` 加 `IsWindow` + 后端 lost 标志；`LockInput` 不支持时返回 0/明确错误码。

---

### O28. 采集自愈与状态上报：失败只返回 0【P2，M】

只有绑定期有 `normal.auto` 回退；运行期后端失败直接透出，GDI resize 是永久失败（注释写 “window resized? rebind”），
WGC item closed 直接 false，DXGI 只原地重建。[BindingSession.cpp:276](../libop/binding/BindingSession.cpp)、[GdiCapture.cpp:245](../libop/capture/backends/GdiCapture.cpp)

**建议**：`ICaptureBackend` 增 `CaptureStatus { ok, no_frame, access_lost, item_closed, capacity, window_gone }` 与
`lastStatus()`；`BindingSession::requestCapture` 连续失败/状态命中时按 `choose_auto_displays` 的下一候选重建
（保留 `_mouse/_keyboard`，避免输入失效）；GDI 用 WGC 的 `ensureSharedResources` 思路重建映射。

---

### O29. 输入时序与文本完整度【P2，S–M】

- `Delay` 精确到 ms，但全仓无 `timeBeginPeriod` → 默认定时器 15.6ms；轨迹的 `delay = duration/(n-1)` 整数除法 + 逐步累计误差
  （duration=200/30 步实际约 470ms）。[WinMouse.cpp:184](../libop/input/mouse/WinMouse.cpp)
- `MOUSE_*_DELAY` 是进程级全局变量，多 handle 互相干扰。[AutomationModes.h:85](../libop/base/AutomationModes.h)
- 文本输入只走 `WM_CHAR`（`InputChar(wchar_t)` 一次一个 UTF-16 code unit，代理对会被拆开），
  无 `WM_UNICHAR`/dead key/IME 处理。[InputHook.cpp:1364](../libop/hook/InputHook.cpp)

**建议**：RAII `timeBeginPeriod(1)` 或高精度 waitable timer；轨迹改绝对时刻表；delay 配置移到 handle；
`InputChar` 改 `char32_t`/串 + `OP_WM_UNICHAR`（目标返回 FALSE 再回退 `WM_CHAR`）；IME 先做能力探测 + 显式 `NOT_SUPPORTED`。

---

### O30. 帧可观测性：frameId 不能判定“新帧”，且不含光标【P2，S–M】

- DXGI 超时复用上一帧但仍 `fmtFrameInfo` 递增 `frameId` → `frameId` 变化不代表新画面。
  [DxgiCapture.cpp:364](../libop/capture/backends/DxgiCapture.cpp)
- 所有后端都不含光标（WGC 硬编码 `IsCursorCaptureEnabled(false)`；DXGI 的 PointerPosition/PointerShape 从未读取）。
  [WgcCapture.cpp:121](../libop/capture/backends/WgcCapture.cpp)
- GDI 的 `PrintWindow(..., 0)` 与 `GetDIBits` 返回值都没检查。[GdiCapture.cpp:277](../libop/capture/backends/GdiCapture.cpp)

**建议**：`FrameInfo` 加 `seq`/`backend`/`flags`（与 O25 的版本化一起做）；GDI 改 `PW_RENDERFULLCONTENT` 并检查返回值；
采集后可选叠加光标（`GetCursorInfo` + `DrawIconEx`，默认关）；`CursorShape` 按 `HCURSOR` 缓存。

---

## 2. 拓展项（能力）

### E1. 统计与可观测性：`OpGetStats`【P1，S–M】

现状只有 `OpGetScreenFrameInfo(hwnd, frameId, time)`，看不到采集耗时、FPS、失败次数。
建议 per-handle 统计结构：

```c
typedef struct {
  int    version;            // = 1
  int    capture_backend;    // 枚举：gdi/dxgi/wgc/hook...
  double capture_ms_avg;     // 最近 N 帧滑动平均
  double capture_ms_p95;
  unsigned long long frames;
  unsigned long long capture_failures;
  unsigned long long find_calls;
  double find_ms_avg;
  int    last_error;
  const wchar_t* last_error_text;  // handle 持有，同字符串生命周期约定
} OpStats;
OP_C_API int OP_CALL OpGetStats(op_handle, OpStats*);
```

理由：Twinkle 轮询找图出现卡顿时，目前无法区分“采集慢 / 找图慢 / OCR 慢”，只能靠外部计时。

### E2. 事件回调：绑定丢失、帧就绪【P2，M】

DXGI duplication 丢失、WGC 关闭、绑定窗口销毁，目前只能靠“下一次调用返回 0”被动发现。
建议 C API 回调注册 + COM 连接点：

```c
typedef void (OP_CALL *op_event_cb)(int event_type, intptr_t hwnd, void* user);
OP_C_API int OP_CALL OpSetEventCallback(op_handle, op_event_cb, void* user);
// OP_EVENT_BIND_LOST / FRAME_READY / WINDOW_DESTROYED
```

实现要点：回调在专用线程调用，禁止在回调里再进同一 handle（文档写明）；先做 `BIND_LOST` 一类，成本最低。

### E3. 结构化结果：相似度 / 模板名 / 全部命中【P2，M】

`FindPicEx` 只给坐标；`OpCvMatchTemplate` 返回 JSON（含 score）。建议统一：

```c
typedef struct { int x, y, width, height; double score; int template_id; } OpMatch;
OP_C_API int OP_CALL OpFindPicAll(op_handle, int x1,int y1,int x2,int y2,
                                  const wchar_t* files, const wchar_t* delta,
                                  double sim, int dir, OpMatch* out, int max_count);
```

好处：调用方不必解析字符串；相似度可用于“取最佳而非首个”。

### E4. 能力查询：`OpGetCapabilities`【P2，S】

返回结构体（`struct_size` 兼容演进）：版本、是否 64 位、已编译后端列表、OpenCV 版本、
OCR/YOLO 支持、最大返回数、支持的 `dir` 值。宿主可据此降级，不必靠试探调用。

### E5. OCR：后端池 / 重试 / 结构化输出【P2，M】

现状：`HttpOcrService` 单例、单 endpoint、并发上限 4、失败不重试（[OcrService.h](../libop/ocr/OcrService.h)）。
建议：

1. endpoint 池（主/备），失败按 `连续 N 次` 熔断到备用，恢复探测；
2. 幂等请求重试 1 次（连接失败/5xx），并区分“超时”和“业务空结果”；
3. `OcrEx` 之外的 `OpOcrDetect` 返回结构化框（`text,left,top,right,bottom,score`）；
4. 测试用本地 stub（`httptest` 或内置最小 HTTP server），把 15 个 OCR 用例从“需要外部服务”变成可 CI。

### E6. 测试基建补齐【P1，M】

当前 139 用例覆盖窗口/输入/图色/OCR/OpenCV/Utils，**零覆盖**：

| 模块 | 缺口 |
| --- | --- |
| `libop/hook/`（InputHook、DisplayHook、SharedFrame、HookProtocol） | 注入协议、32/64 位、多进程同步 |
| `libop/memory/` | 读写/类型转换/跨进程权限 |
| `libop/ipc/` | SharedMemory/ProcessMutex 生命周期 |
| `libop/com/OpAutomation.*` | 187 个方法只测了注册与名字表，业务方法零覆盖 |
| `ImageSearchAlgorithms::FindLine` | 无用例（AUDIT 也提过） |
| `FindPicTh` | 死代码，无调用无测试 |

建议优先级：memory（本进程读写即可测）→ ipc（可单进程双对象）→ hook（共享帧协议纯内存测试）→ COM（在已注册 typelib 的用例上扩 5–10 个代表方法）。
另外把 `OpenCvBenchmark` 从 `DISABLED_` 改为默认关闭、`--bench` 打开的基准程序，输出 CSV 基线。

### E7. 分发与集成【P3，M】

- 现在没有 `find_package` / vcpkg port / NuGet；Twinkle 靠手工拷贝 `lib/`+`include/`。
- 建议提供 `op.props`（已存在）+ 一个 `vcpkg-overlay/ports/op` 或最小 `.pc`/CMake config（**只做消费侧配置，不恢复 CMake 构建链**）。
- Release 附件：`op_c_api_{x64,x86}.dll/.lib` + `include/` + PDB + `LICENSE`，附 SHA256。

### E8. 可选能力（需产品决策，不建议先做）

| 方向 | 评估 |
| --- | --- |
| 截图落盘 PNG/JPEG（当前只有 BMP/内存） | 若 Twinkle 需要日志截图可加；OpenCV `imwrite` 已在链接内，成本低 |
| 键鼠录制/回放 | 属于上层业务，建议留在 Twinkle，不进 ops |
| 窗口消息钩子/进程监控 | 与绑定/采集重叠，收益不明 |
| 恢复 Python/Go/SWIG 绑定 | 用户已拍板删除，不做 |

---

## 3. 工程化与仓库治理（G 系列）

### G1. 没有一键构建/测试入口，文档写死本机路径【P0，S】

`scripts/` 只有 `bootstrap-deps.ps1`；README/README_EN/CLAUDE/BUILD_VS/examples 里共 15 处
`Y:\Microsoft Visual Studio\...` 与 `Y:\Tools\vcpkg`，四配置矩阵只存在于 `AUDIT_SUMMARY.md` 的手工命令。

**建议**：`scripts/build.ps1`（`vswhere` 定位 MSBuild，`-Configuration/-Platform/-Target`，默认 `OpSyncTwinkle=false`）、
`scripts/test.ps1`（设 `OP_SKIP_*`、透传 gtest 退出码）、`scripts/verify.ps1`（四配置 + 核心测试 + 版本检查，打印 PASS/FAIL 汇总）。
文档改成 `pwsh scripts/build.ps1 -Target all`。

### G2. “0 警告”没有机制保证【P0，S】

`vs/op.common.props:44-45` 是 `WarningLevel=Level3`、`TreatWarningAsError=false`，而 `AUDIT_SUMMARY.md` 宣称四配置 0 警告。
下一次提交即可带回告警且无人发现。

**建议**：加一个 `OpWarningsAsErrors` 开关（CI/Release 打开），或至少把 `/W4` + 关键告警（C4267/C4244/C4100）设为 error；
`scripts/verify.ps1` 解析 MSBuild 日志中的 `warning C` 计数，非 0 即失败。

### G3. 测试资产不在仓库，bench 与 10 个真实图用例永久 SKIP【P1，S】

`.gitignore:45` 忽略 `assets/`；`tests/opencv_test.cpp:61` 只在仓外找图，10 处 `GTEST_SKIP() << "...assets are not available"`，
唯一的性能用例 `DISABLED_MatchingHotPaths` 也依赖同一批图。

**建议**：建 `tests/assets/`（≤5MB，压缩图 + 来源/许可说明），`.gitignore` 精确放行；或 `scripts/fetch-test-assets.ps1 -Sha256`；
bench 改独立 `op_bench`（或 `--bench` 开关）；CI 中“资产缺失”应 FAIL 而不是 SKIP。

### G4. 依赖预检漏 gtest/directx-headers；bootstrap 未 pin【P1，S–M】

`vs/op.deps.props` 的 `OpCheckDependencies` 只校验 BlackBone/OpenCV/MinHook；
`tests/op_test.vcxproj:132` 直接链 `$(OpVcpkgStaticDir)\lib\gtest.lib`，而 `op.sln` 默认构建 op_test → 干净机器上链接期才报 `LNK1104`。
`scripts/bootstrap-deps.ps1:215` 无 pin clone BlackBone，OpenCV zip 无 SHA256。

**建议**：补 gtest/minhook/directx 头断言并接到 op_test；固定 `$BlackBoneCommit`（已有目录也校验 HEAD）；
OpenCV zip 加 `Get-FileHash` 比对；新增 `-DepsArchive <tar>` 从 release 附件恢复 `build/_deps`（新人/CI 即用）。

### G5. 文档已出现可验证漂移【P1，S】

- `CLAUDE.md:62` 把 11 行的 `libop/libop.cpp` 描述成“主实现”（实际实现在 `libop/op/*.cpp`）；`AStar.hpp` 实为 `AStar.h`；
  `HttpOcrService` 实为 `OcrService`；`3rd_party` 只有 kiero2 源码、没有 “pre-built libs”。
- `.clang-tidy` 教用已删除的 `build.py -g ninja` 生成 `compile_commands.json`。
- `PROGRESS.md` 勾选“清理临时文件/构建日志”，根目录仍有 70 个 `build_*.log`（已 ignore，未入库）。
- `README.md` 的 Release badge/下载链接指向上游 `WallBreaker2/op`，本 fork 没有 release 流程。

**建议**：一次性修订；新增 `scripts/check-docs.ps1`（仓内路径存在性、版本一致、中英 README 结构对齐）并进 CI。

### G6. 打包/发布链路为零【P1，M】

无 `package/release` 脚本、无 `CHANGELOG`、无 git tag、无 `THIRD_PARTY_NOTICES`（产物静态链接 OpenCV，Apache-2.0 分发需保留许可），
Release 无 PDB（见 O10）。

**建议**：`scripts/package.ps1 -Version -Platform` 产出 `dist/op-<ver>-<arch>.zip` + `-symbols.zip`，
内含 `bin/lib/include/LICENSE/THIRD_PARTY_NOTICES/README`；生成 `THIRD_PARTY_NOTICES.md`；
首个 release 打 `v0.5.0`（本 fork 删了 27 个 C API，不应继续叫 0.4.8.3，见 O9）。

### G7. 性能/压力/泄漏/静态分析缺位【P2，M】

有效 bench = 0（DISABLED + 缺资产）；无 soak/leak/fuzz/ASAN；`.clang-tidy` 未接线且用法失效。

**建议**：bench 转正并输出 JSON + 基线阈值（±15%）；`tests/soak_test.cpp --soak --minutes=N` 循环
Bind/Unbind + Capture + FindPic，每 1000 次采样句柄数/PrivateUsage/GDI 对象数；x64 加 ASAN 配置跑 core 集。

### G8. 测试会污染真实桌面，缺少分层【P2，S–M】

22 处全局输入用例靠 `CODEX_*` 环境变量跳过；`handle_compat_test` 抢前台焦点；WGC 靠 `OP_SKIP_WGC_TESTS`。
在开发者机器上跑全量会移动鼠标、把按键打进正在用的窗口。

**建议**：分三层并写进 `tests/README.md`：`core`（纯内存/算法/字符串）、`desktop`（窗口 + 消息输入）、
`inject`（全局输入/WGC/DX，manual 或 self-hosted）；输入类优先改私有桌面（`CreateDesktop`）或强制消息模式。

---

## 4. 建议落地顺序

| 迭代 | 内容 | 预期 |
| --- | --- | --- |
| 第 1 批（S，低风险） | O13 截图拷贝 API、O3 日志默认静默+回调、O7 路径规范化、O19 delta_color、O1 删死代码/懒加载、O16/O17 COM 硬化、G1 构建脚本、G2 警告门禁 | 宿主稳定性与资源占用立刻改善 |
| 第 2 批（M） | O2 缓存失效+预算、O4 错误分类、O14 返回值收敛、O24/O25 采集热路径、O26 输入批量、O27 绑定状态机、G3 测试资产、G4 依赖预检 | 正确性与可诊断性 |
| 第 3 批（M） | O5/O21/O22 找图基准与后端、O23 OCR 传输、E1 统计、E3 结构化结果、E6/G7 测试补齐与 bench、O6/G6 CI 与打包 | 性能与发布工程 |
| 第 4 批（L） | O28 自愈、O29 时序/Unicode、E2 事件、E5 OCR 池、G5 文档/G8 测试分层 | 长期可维护性 |
| 每批完成 | 更新 `PROGRESS.md`；四配置 0 警告 + 全量 `op_test` 回归 | — |

---

## 5. 复核方式

```powershell
cd Y:\sourcecode\cpp\Twinkle\external\ops

# 线程数：启动 cpp_call 前后对比
.\bin\x64\cpp_call.exe

# 测试清单（确认覆盖与数量）
.\bin\x64\op_test.exe --gtest_list_tests
.\bin\x64\op_test.exe --gtest_filter="UtilsTest.*:ImageColorTest.*:OpenCvTest.*"

# 四配置构建（改动后）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Debug /p:Platform=Win32 /m
```

> 说明：本文件只记录审查结论，未改动任何源码；后续每项落地时在 `PROGRESS.md` 追加一条并附验证命令与结果。
