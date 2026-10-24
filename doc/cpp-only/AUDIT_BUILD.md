# OP 构建/工程审计（slim/cpp-only-vs @ cdb2eb3）

> **快照说明**：本报告是 HEAD `cdb2eb3` 的审计结果；Debug `/MT`、gtest 依赖、
> PostBuild 门禁与头文件同步已在工作区修复，当前状态以 [AUDIT_SUMMARY.md](AUDIT_SUMMARY.md) 为准。

> 审计对象：`op.sln`、6 个 `.vcxproj`、`vs/op.{common,deps,sources}.props`、`scripts/bootstrap-deps.ps1`、
> `README.md` / `README_EN.md` / `CLAUDE.md` / `doc/cpp-only/BUILD_VS.md`、`Twinkle.sln` 的 OP 工程（只读）。
> 审计方式：静态读工程 + 与 `git show d00f837:{CMakeLists.txt,libop/CMakeLists.txt,tests/CMakeLists.txt}` 逐条对照
> + **实际构建**（Release x64 / Release Win32 全量 Rebuild、Debug|x64 探针、增量构建、Clean 探针、MIDL 探针）。
> 审计时间：2026-10-25。本审计只新增本文件（`doc/cpp-only/AUDIT_BUILD.md`），未修改任何源码/工程、未提交 git。
> **注意**：审计进行中工作区出现了另一路（未提交）改动（见 §0.1）；凡涉及这些文件的结论都以 HEAD `cdb2eb3` 为准，
> 并标注工作区现状。工程文件（`op.sln`/`*.vcxproj`/`vs/*.props`）在审计期间没有变化。

## 0. 结论摘要

| 级别 | 数量 | 说明 |
| --- | --- | --- |
| P0 | 0 | 未发现"现在就会产出错误二进制"的问题；Release x64/Win32 均可 0 错误 0 警告复现 |
| **P1** | **5** | 会污染/破坏下游、或让"看起来支持"的配置必然失败、或全新机器无法构建 |
| P2 | 13 | 可复现性、增量正确性、文档漂移、体积/耗时优化 |

一句话结论：**Release 路径已经可用且与旧 CMake 等价；真正的坑集中在
（a）Debug 配置是"假支持"，（b）PostBuild 无配置门禁会覆盖 Twinkle 的 Release 产物，
（c）全新机器缺 gtest 导致整解决方案构建失败，（d）Twinkle 头文件靠手工同步，
（e）vcpkg include 根仍在 include 路径里（OpenCV4 头随时可能再被命中）。**

### 0.1 审计期间的工作区并发变化（必读）

审计开始时工作区是干净的（`git status` 为空）。审计过程中出现了**一批未提交改动，不是本审计产生的**
（同时出现了另一份 `doc/cpp-only/AUDIT_RUNTIME.md`，推测是并行的运行时审计/修复）：

| 文件 | 工作区现状 | 对本报告的影响 |
| --- | --- | --- |
| `scripts/bootstrap-deps.ps1` | `Install-VcpkgPackages` 已加 `gtest:$triplet` 并校验 `gtest.lib`（工作区 `:168-181`）；**HEAD `cdb2eb3` 中 `grep gtest` = 0 处** | P1-3 对 HEAD 成立；工作区已有待提交修复 |
| `README.md` / `README_EN.md` / `doc/cpp-only/BUILD_VS.md` | 只补了 gtest 相关说明（`README.md:107` 附近、`BUILD_VS.md:48` 附近） | P2-13 只修了"vcpkg 包列表"这一项；`BUILD_VS.md:22/25`、`README*.md:63`、导出数、Debug 报错类型**仍未修** |
| `libop/**`、`tests/*.cpp`、`doc/ocr.md`、`doc/cpp-only/API_CHANGES.md` | 运行时/文档修复 | 不在本次构建审计范围 |

因此：**本报告的 file:line 与结论按 HEAD `cdb2eb3` 陈述**；受并发改动影响的条目已单独标注"工作区已修/未修"。
`op.sln`、6 个 `*.vcxproj`、`vs/*.props` 在整个审计期间**没有改动**，所有引用对 HEAD 与工作区都成立。
实测构建（§1）发生在两份状态之间，Release x64/Win32 均 0 警告 0 错误 —— 说明这批并发改动没有破坏构建。

## 1. 实测验证记录

环境：VS2022 17.11（`v143`，MSVC 14.41.34120）、Windows SDK 10.0.22621.0、`VCPKG_ROOT=Y:/Tools/vcpkg`。

| # | 命令 | 结果 | 耗时 |
| --- | --- | --- | --- |
| 1 | `MSBuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m` | **0 warning / 0 error**，6 工程全部产出 | 105.2 s |
| 2 | 同上，去掉 `/t:Rebuild`（增量，第二次） | 0/0，`OpGenerateComIdl` 被正确 Skip | **1.6 s** |
| 3 | `MSBuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m` | **0 warning / 0 error**，产出 `op_x86.dll`/`op_c_api_x86.dll`/`op_test.exe` | 67.1 s |
| 4 | `MSBuild op_x64.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64`（OutDir/IntDir 重定向到 `%TEMP%`） | **失败**，35.6 s，`LNK2038` ×N + `LNK4098` | — |
| 5 | `MSBuild tools.vcxproj /p:Configuration=Release /p:Platform=x64` → `/t:Clean` | Clean 后 `op_i.c/op_i.h/op_p.c/dlldata.c` **仍在**，只有 `op.tlb` 被删 | — |
| 6 | 删除 `build\vs\x64\Release\op_x64\op.tlb` 后增量构建 | MIDL 重新生成，`OP.RES` 被重编 → `ResourceCompileAdditionalInputs` **生效** | — |
| 7 | `bin\x64\op_test.exe --gtest_list_tests` | 13 suites / **132 cases**；x64 与 x86 均能加载同名 DLL | — |
| 8 | `bin\x64\op_test.exe --gtest_filter="UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*:OpTest.*"` | `[PASSED] 10 tests` | 0.4 s |
| 9 | `bin\x64\cpp_call.exe` | `Ver=0.4.8.3 / OpenCV=5.0.0` | — |
| 10 | `dumpbin /dependents bin\x64\op_{x64,c_api_x64}.dll` | 只有系统 DLL（`WINHTTP/d3d11/dxgi/gdiplus/...`），无 OpenCV/BlackBone/MinHook → 文档说法成立 | — |
| 11 | `dumpbin /exports` | `op_c_api_x64.dll` = **200** 个导出；`op_x64.dll` = **215** 个（文档写 198/213，见 P2-13） | — |
| 12 | `dumpbin /directives` | OpenCV/gtest/BlackBone 的 `.lib` 均为 `RuntimeLibrary=MT_StaticRelease`、`_ITERATOR_DEBUG_LEVEL=0` | — |
| 13 | `git check-ignore` / `git ls-files` | `bin/`、`lib/`、`build/` 均未被跟踪（`build\_deps` 也不进 git） | — |
| 14 | `cl /EP /MT` vs `cl /EP /MTd` 预处理探针（验证 `_DEBUG` 与 `cv::debug_build_guard` 的触发条件） | `/MT` → **`_DEBUG` 未定义**；`/MTd` → `_DEBUG` 已定义（→ 旧 CMake 的 `/MT` Debug 不会触发 guard，新工程的 `/MTd` 才会） | — |
| 15 | `dumpbin /directives minhook.x64.lib` | 只有 `/DEFAULTLIB:LIBCMT`，**没有** `/FAILIFMISMATCH:RuntimeLibrary`（与 OpenCV/gtest/BlackBone 不同） | — |

> 副作用声明：本次实际构建重写了 `bin/`、`lib/`、`build/vs/`，并且 PostBuild 按设计把
> `op_*.dll/.lib` 重新写入了 `modules/Twinkle.Background/lib/`（该目录被 Twinkle 的 `.gitignore` 忽略，
> 内容与构建前一致，未产生 git 变更。文件 mtime 已变为 1:43/1:47）。

## 2. 已核对无误的项（避免后来人重复劳动）

1. **配置矩阵完整**：`op.sln:25-30` 的 4 个解决方案配置对 6 个工程（`libop`/`op_x64`/`op_c_api_x64`/`tools`/`cpp_call`/`op_test`）都有 `ActiveCfg + Build.0`
   （`op.sln:32-79`）；每个 `.vcxproj` 的 `ProjectConfigurations` 也都是 4 项。**没有缺映射**。
2. **源码清单精确**：`vs/op.sources.props` 里 `ClCompile` 共 66 个 libop/kiero2 源文件，磁盘上同名集合也是 66 个，**无漏、无多**
   （`Get-ChildItem -Recurse -Filter *.cpp` 差集为空）；`op/OpContext.cpp`、`op/OpRuntime.cpp` 等 12 个 `op/` 文件、`capture/`、`hook/`、`opencv/` 全部在册。
3. **Kiero2 slot**：`vs/op.sources.props:43-57` 给 5 个后端 cpp 分别 `KIERO_IMPL_FIRST_SLOT=0..4`，`kiero_intern.cpp:104` 不设 slot —— 与旧 `libop/CMakeLists.txt` 的 `set_source_files_properties` 逐条一致。
4. **局部 C++20 集合**：`vs/op.sources.props:11-38` 的 7 个文件 = 旧 `OP_LOCAL_CXX20_SOURCES`（含 WGC）逐条一致，且都带
   `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS`。
5. **MIDL host 跟随架构且不需要 dev prompt**：实测 Win32 的 Exec 为
   `PATH=...\VC\Tools\MSVC\14.41.34120\bin\HostX86\x86;%PATH%` + `...\Windows Kits\10\bin\10.0.22621.0\x64\midl.exe`；
   x64 用 `HostX64\x64`。`INCLUDE` 由 `$(VC_IncludePath);$(WindowsSDK_IncludePath)` 显式补齐（`vs/op.common.props:127`）。
6. **op.rc ← op.tlb 依赖有效**：删除 `op.tlb` 后增量构建重编了 `op.res` 并重新链接（见 §1 #6）。
7. **增量正确、速度快**：无改动时全解决方案 1.6 s，MIDL 被 `Inputs/Outputs` 正确跳过。
8. **独立 clone 不产生副作用**：`PostBuild` 用 `if exist "$(OpTwinkleLibDir)"` 包裹；我用
   `/p:OpTwinkleLibDir=%TEMP%\no-twinkle\` 实测构建成功且未复制。
9. **迁移性**：`vs/op.deps.props:10` 用 `$(MSBuildThisFileDirectory)..\` 算 `OpRepoRoot`，**不依赖 `$(SolutionDir)`**，
   因此"单工程 msbuild"和"从 sln 构建"结果一致（这正是它比旧 build.py 好的地方）。
10. **多架构产物命名/导入库**：x64→`op_x64`/`lib\x64`，Win32→`op_x86`/`lib\x86`；`ImportLibrary` 均落在 `lib\<arch>`，
    与旧 CMake `ARCHIVE DESTINATION lib/${arch}` 等价。
11. **链接清单/顺序**：`/LIBPATH` 为 OpenCV(vc17 staticlib) → vcpkg static lib；依赖顺序 `libop.lib → OpenCV → BlackBone → MinHook → windowsapp/runtimeobject/dwmapi → 默认库`，
    实测无 `LNK2001/LNK2019/LNK2005`（重复编译的 `HookExport.cpp` 在 `op_c_api` 里没有触发重复定义，因为 `libop.lib` 的该成员没被拉入）。
12. **`/MT`、`/EHa`、`/utf-8`、`/FS`、`/MP`、`/Gy`、`/GF`、`/Oi`、`/W3`、无 `/WX`**：与旧 CMake flags 一致（`vs/op.common.props:35-48`）。
13. **`op_test` 运行期 DLL 就位**：`OutDir` 与 DLL 同为 `bin\<arch>`，实测 `--gtest_list_tests` 能加载 `op_x64.dll`/`op_c_api_x64.dll`。
14. **`op_i.c` 来源可用**：`op_test` 复用 `build\vs\<Platform>\<Cfg>\op_x64\op_i.c`，实测在 x64/x86 都能编过（但见 P2-3 的耦合风险）。

---

## 3. P1（5 条）

### P1-1 PostBuild 没有配置门禁：任何配置（含 Debug）的成功构建都会覆盖 Twinkle 的 Release 产物

- **位置**：`op_x64.vcxproj:88-93`、`op_c_api_x64.vcxproj:88-93`、目标目录 `vs/op.deps.props:29-31`
- **证据**：
  - `<PostBuildEvent><Command>if exist "$(OpTwinkleLibDir)" (copy /Y "$(TargetPath)" ... &amp;&amp; copy /Y "$(OpOutDirLib)$(TargetName).lib" ...)</Command>`
    **完全没有 `Condition`** —— 只要目录存在、只要链接成功，就写 `..\..\modules\Twinkle.Background\lib\`。
  - 本次 Release x64 / Release Win32 两次 Rebuild 后，`modules/Twinkle.Background/lib/op_x64.dll`(1:43:5x)、
    `op_x86.dll`(1:47:51) 等 8 个文件的 mtime 全部被改写 → 证明"构建 OP = 写父仓库目录"。
  - Debug 的 `TargetName` 与 Release 相同（都是 `op_x64`/`op_x86`），因此**一旦 Debug 将来能链接（补了 Debug 版 OpenCV 就会），
    就会把 /MTd 的 Debug DLL 覆盖到 Twinkle 的 Release 目录**，且无任何提示。
- **影响**：Twinkle 主仓库被静默注入非 Release 二进制；这种污染只在运行期表现为"莫名其妙的 CRT/断言语义"，极难定位。
  另外每次 `op.sln` 构建都会改父仓库文件树，`git status` 干净是因为 `.gitignore` 忽略，不是因为没有写入。
- **建议**：
  1. `Condition="'$(Configuration)' == 'Release'"`（最低成本）；
  2. 增加开关 `<OpSyncTwinkle Condition="'$(OpSyncTwinkle)' == ''">true</OpSyncTwinkle>`，需要时 `/p:OpSyncTwinkle=false` 关掉；
  3. 只在 `'$(TargetName)' != '' and Exists('$(OpTwinkleLibDir)')` 且目录内确有 Twinkle 工程文件（如 `Twinkle.Background.vcxproj` 上两级）时才拷贝，
     避免"别的仓库恰好有 `..\..\modules\Twinkle.Background\lib`"被误写。
- **置信度**：高（行为已实测）。

### P1-2 Debug|x64 / Debug|Win32 是"假配置"：6 个工程 × 2 架构都声明了 Debug，但必然链接失败

- **位置**：`libop.vcxproj:12-19`、`op_x64.vcxproj:18-25`、`op_c_api_x64.vcxproj:18-25`、`tools.vcxproj:17-24`、
  `cpp_call.vcxproj:17-24`、`tests/op_test.vcxproj:32-39`、`op.sln:26-27`；根因 `vs/op.common.props:68-80`（Debug 用 `/MTd`）
  与 `vs/op.deps.props:105`、`tests/op_test.vcxproj:132-133`（依赖全是 Release/MT 静态库）。
- **证据**（实测，§1 #4）：
  ```
  error LNK2038: 检测到"_ITERATOR_DEBUG_LEVEL"的不匹配项: 值"0"不匹配值"2"(BindingSession.obj 中)
  error LNK2038: 检测到"RuntimeLibrary"的不匹配项: 值"MT_StaticRelease"不匹配值"MTd_StaticDebug"(BindingSession.obj 中)
  warning LNK4098: 默认库"LIBCMT"与其他库的使用冲突
  ```
  `dumpbin /directives` 证实 `opencv_*500.lib`、`gtest.lib`、`BlackBone.lib` 全是 `MT_StaticRelease` + `_ITERATOR_DEBUG_LEVEL=0`，
  它们带 `/FAILIFMISMATCH`，所以**硬报 LNK2038**；MinHook（vcpkg static）只有 `/DEFAULTLIB:LIBCMT`、没有 FAILIFMISMATCH，
  于是只会给出 `LNK4098` 警告并**把 Release CRT 混进 /MTd 的 DLL**（比报错更危险，可能"静默编过"）。
  → 要真正支持 Debug，必须同时有 OpenCV / gtest / BlackBone 的 Debug 版 **且** 处理 MinHook 的 CRT 混用；只补 OpenCV 不够（文档只提了 OpenCV）。
- **影响**：VS 里 `Debug|x64` 看起来可选，选中后 30-60 s 才在链接期炸，错误是 LNK2038 而不是可读的"Debug 不支持"。
  `op.sln` 全 Debug 构建会在 `op_x64`/`op_c_api`/`tools`/`cpp_call`/`op_test` 连环失败。
- **建议**（三选一，详见 §6 末尾的分析）：
  - **最小修复 / 恢复历史行为**：Debug 的 `RuntimeLibrary` 改成 `MultiThreaded`（与旧 CMake 完全一致）。
    实测 `/MT` 下 `_DEBUG` 不定义 → `cv::debug_build_guard` 不触发、CRT 指令与现有 Release 依赖匹配，Debug|x64/Win32 立即可链接；
    代价是 `assert`/迭代器调试关闭（旧 CMake 本来就是这样）。
  - **彻底不支持**：从 6 个工程与 `op.sln` 里删掉 Debug 配置（只留 Release），或在 `vs/op.common.props` 加一个
    `Target Name="OpRejectDebug" BeforeTargets="ClCompile" Condition="'$(Configuration)'=='Debug'"` + `<Error Text="Debug 不支持…"/>`，
    让失败发生在第 1 秒、错误信息可读。
  - **真 Debug**：补全 Debug 依赖（OpenCV `?<CONFIG:Debug>` 版 + `opencv_*500d.lib`、`gtestd.lib`、BlackBone Debug、MinHook 的 CRT 混用），
    并把 `op.deps.props` 的库名/路径按 `$(Configuration)` 分支 + 处理 `cv::debug_build_guard`。
- **附带**：`doc/cpp-only/BUILD_VS.md:245-248` 和 `CLAUDE.md:102-103` 说 Debug 会报 `LNK2019`（`cv::debug_build_guard`），
  **实际首发错误是 LNK2038**；`cv::debug_build_guard` 只会在这关过了之后才出现。
- **置信度**：高。

### P1-3 `bootstrap-deps.ps1` 不安装 gtest，但 `op.sln` 里的 `op_test` 链接它 → 全新机器整解决方案构建必失败

- **位置**：`scripts/bootstrap-deps.ps1:166-179`（只 `install minhook:$triplet directx-headers:$triplet`）、
  `tests/op_test.vcxproj:132-133`（`$(OpVcpkgStaticDir)\lib\gtest.lib`）、`op.sln:18`（op_test 在解决方案里、4 个配置都 `Build.0`）。
- **证据**：本机 `Y:\Tools\vcpkg\installed\{x64,x86}-windows-static\lib\gtest.lib` 存在，但**不是这个脚本装的**
  （脚本里没有 `gtest`）；`vs/op.deps.props:113-126` 的依赖预检也**不检查 gtest.lib**。
  所以在一台只跑过 `bootstrap-deps.ps1` 的机器上，`op.sln` 的 `op_test` 会在链接期报
  `LNK1104: 无法打开文件 "…\gtest.lib"`，而前面 5 个工程都是 0 error —— 看起来像"测试工程坏了"。
- **影响**：`op.sln /t:Rebuild`（文档里的标准命令）在干净机器上不可用；CI/新同事第一次克隆必然踩。
- **建议**：
  1. `Install-VcpkgPackages` 加 `"gtest:$triplet"`（`gtest` 在 vcpkg 里提供 `gtest.lib` 与 `gtest/gtest.h`，与 `/MT` 一致）；
  2. `OpCheckDependencies` 增加 gtest 存在性检查，给出「请先运行 bootstrap-deps 或 vcpkg install gtest」的可读错误；
  3. 文档 `README.md:96-97`、`README_EN.md:96`、`BUILD_VS.md:31`、`CLAUDE.md:12` 的 vcpkg 包列表补 `gtest`。
- **工作区现状（审计结束）**：`scripts/bootstrap-deps.ps1` 已被并行改动加上了 `gtest:$triplet` 并校验 `gtest.lib`，
  `README*`/`BUILD_VS.md` 的 gtest 说明也补了；**但 HEAD `cdb2eb3` 仍缺**，合并前注意这条修复要一起进。
  另外 `CLAUDE.md:12` 的包列表仍是 "BlackBone / OpenCV / MinHook"，未补 gtest。
- **置信度**：高。

### P1-4 Twinkle 集成只自动同步 DLL/LIB，头文件靠手工 `Copy-Item`（DPI 新 API 就是靠人工同步才一致）

- **位置**：自动侧 `op_x64.vcxproj:88-93`（只 copy `$(TargetPath)` + `.lib`）；手工侧 `doc/cpp-only/TWINKLE_SYNC.md:55-63`；
  消费侧 `build/Twinkle.sdk.props:24,33,37`（头文件来自 `external\OP\op-include`，库/DLL 来自 `modules\Twinkle.Background\lib`）。
- **证据**：
  - `external\OP\op-include\op_c_api.h` 与 `external\ops\include\op_c_api.h` 当前 SHA 相同（`E86CBA59…`，含 5 处 `DpiAware`），
    mtime 为 `0:54:51`，而 ops 侧 `f8644fe` 的 DPI 改动就是在那前后 —— 说明是**人工复制**才对齐的，没有任何机制保证。
  - 头文件与导入库来自**两个不同目录、两条不同同步机制**（一个手工、一个 PostBuild），漂移时编译器不会报错。
- **影响**：下一次改 `include/op_c_api.h`（例如再加一个开关）时，Twinkle 侧会拿旧头文件配新 lib：
  轻则新 API 不可见，重则结构体/枚举布局不一致（`op_handle`、`ocr_rec_t` 等）导致运行期内存错误。
- **建议**：在 PostBuild 里加一条
  `copy /Y "$(OpRepoRoot)include\op_c_api.h" "$(OpTwinkleLibDir)..\..\external\OP\op-include\"`（连同 `libop.h`），
  或单独给一个 `scripts\sync-twinkle.ps1` 并在 `BUILD_VS.md` 里把"手工复制"改成"构建自动完成 + hash 校验"。
- **置信度**：高（当前一致，风险为流程性）。

### P1-5 `$(OpVcpkgStaticDir)\include` 整根进入 include 路径，而它里面就有 OpenCV4 头（历史故障仅靠顺序规避）

- **位置**：`vs/op.common.props:34`（`…$(OpOpenCvIncludeDir);$(OpVcpkgStaticDir)\include;…`）。
- **证据**：
  - 实测 `Test-Path Y:\Tools\vcpkg\installed\x64-windows-static\include\opencv2\core.hpp` → **True**；
    `x86-windows-static` 同样为 **True**（还能看到 `boost/asio/cpr/curl/leptonica/protobuf` 等一大堆无关头）。
  - CL 命令行里 `/I"…\opencv\install\vs2022-x64\include"` 后面紧跟 `/I"Y:/Tools/vcpkg\installed\x64-windows-static\include"`，
    也就是说**只要 OpenCV install 里缺某个头（模块裁剪、install 不完整、大小写差异），编译就会静默命中 vcpkg 的 OpenCV4 头**，
    这正是 `BUILD_VS.md:56-84` 记录的 `"ENGINE_NEW" is not a member of "cv::dnn"` / `cv::DistanceTypes 重定义` 的成因。
- **影响**：隐蔽的"头文件混用"回归；一旦触发，报错点离原因很远（在 `features`/`calib` 模块里）。
  同时 `boost/cpr/protobuf` 等头进入搜索路径，可能让 `#include <xxx>` 意外命中，也让编译变慢。
- **建议**：只加真正需要的子目录，例如
  `<AdditionalIncludeDirectories>…;$(OpVcpkgStaticDir)\include\minhook;$(OpVcpkgStaticDir)\include\directx;…</AdditionalIncludeDirectories>`
  （MinHook 头在 `include\minhook\`，directx-headers 在 `include\directx\`），或改用 `/external:I` + `<AdditionalOptions>`；
  若必须保留整根，至少在 `OpCheckDependencies` 里加一条断言：`Exists('$(OpVcpkgStaticDir)\include\opencv2\core.hpp')` 时报 Warning 提示风险。
- **置信度**：高（路径存在性已实测）。

---

## 4. P2（13 条）

### P2-1 MIDL 生成物没进 Clean/FileWrites：`/t:Clean` 后 `op_i.c/op_i.h` 仍残留，且"半失败"时会跳过 MIDL

- **位置**：`vs/op.common.props:107-134`（target 有 `Inputs/Outputs`，但没有 `<FileWrites>` / `OnError Delete`）。
- **证据**（实测 §1 #5）：`MSBuild tools.vcxproj /t:Clean` 后 IntDir 里
  `op_i.c`、`op_i.h`、`op_p.c`、`dlldata.c` **都还在**，只有 `op.tlb` 被删（RC 跟踪了它）。
  由于 target 的 `Outputs` 包含 5 个文件而 `Inputs` 只有 `op.idl`，只要第 5 个（`op.tlb`）不在就整体重跑，所以"全清"看起来正常；
  但 `op_i.c/op_i.h` 单独残留意味着：
  - `clean` 出来的树不是干净树（IDE 里"清理后仍能编过"的假象）；
  - 若某次 `midl` 在写完 `op_i.c/op_i.h` 后失败（写 tlb/代理失败、磁盘满、权限），文件 mtime 已晚于 `op.idl`，
    下次构建 5 个输出又都在 → **MIDL 被 Skip**，编译用的是可能不完整的 `op_i.h`，并且 `op_test` 也会从
    `$(OpIntDirRoot)op_x64\` 读到同一份。
- **建议**：在 target 里加 `<ItemGroup><FileWrites Include="$(IntDir)op_i.c;$(IntDir)op_i.h;$(IntDir)op.tlb;$(IntDir)op_p.c;$(IntDir)dlldata.c" /></ItemGroup>`；
  加 `OnError Execute` 删除输出；或先输出到 `$(IntDir)midl.tmp\` 再整体 Move。
- **置信度**：Clean 行为高（实测）；"半失败"路径为中（推理，未制造故障）。

### P2-2 MIDL 被重复执行且产物与架构无关；`op_p.c`/`dlldata.c` 生成后从不编译

- **位置**：`op_x64.vcxproj:57` 与 `tools.vcxproj:56` 都 `OpGenerateComIdl=true`；`vs/op.common.props:132` 仍传 `/proxy` `/dlldata`。
- **证据**：一次 Rebuild 的日志里有 **2 条** `[OP] MIDL … -> …\op_x64\…` 与 `…\tools\…`；
  `build\vs\x64\Release\op_x64\op_i.c` 与 `build\vs\Win32\Release\op_x64\op_i.c` **SHA 完全相同**（`E07777A8…`），
  `op.tlb` 也完全相同（`40818D89…`）——即"每架构、每工程各生成一次"是纯重复。
  全仓库对 `op_p.c`/`dlldata.c` 的引用数为 0（只有 `op_i.c/op_i.h/op.tlb` 被用到）。
- **影响**：每次 Rebuild 多 ~2 次 midl + 5 个文件 IO；`/env win32` 让产物跨架构字节一致，"分架构生成"没有任何收益；
  代理/`dlldata` 是历史 CMake 的遗留，永远不参与链接。
- **建议**：新建一个"COM 生成"工程或把 target 挪到 `build\vs\com_generated\`（像旧 CMake 的 `OP_COM_GENERATED_DIR`），
  只生成一次，`op_x64`/`tools`/`op_test` 都引用它；去掉 `/proxy`、`/dlldata`（若确实不需要 marshal 代理）。
- **置信度**：高。

### P2-3 `op_test` 隐式依赖 `op_x64` 的 IntDir 路径（跨工程硬编码）

- **位置**：`tests/op_test.vcxproj:91-93`（`OpTestComGeneratedDir=$(OpIntDirRoot)op_x64\`）、`:159-164`（编译其中的 `op_i.c`）、`:172-182`（ProjectReference 仅为保序）。
- **证据**：路径里写死了目录名 `op_x64`；`op_test` 与 `op_x64` 无编译期依赖，只靠"先构建"保证文件存在。
  审计中我曾用 `/p:IntDir=` 把 `op_x64` 重定向到 `%TEMP%`（Debug 探针，见 §1 #4）；按同一做法构 `op_test` 时它仍会去**默认** IntDir 找 `op_i.c`（`C1083`，错误信息里只有一条 obj 路径），没有排查线索。
  另外 `op.sln:19-22` 的 sln 级 `ProjectDependencies` 与 `op_test.vcxproj:170-183` 的 `ProjectReference` 是**重复的依赖声明**。
- **影响**：改工程名/改 IntDir/单工程构建/并行 CI 分片时都可能断；`op_test` 无法独立于 `op_x64` 构建（虽然这是有意设计，但耦合点应集中声明）。
- **建议**：让 `op_test` 自己跑一次 MIDL（target 已参数化，只需 `OpGenerateComIdl=true` + `$(IntDir)`），
  或使用与 P2-2 相同的共享生成目录；至少在 `OpTestComGeneratedDir` 前加 `<Error Condition="!Exists(...)" Text="…先构建 op_x64"/>`。
- **置信度**：高（耦合事实明确）。

### P2-4 PDB 落点未控制：Debug 的 linker PDB 写进 `bin\<arch>`，Release 完全没有 PDB

- **位置**：`vs/op.common.props:75-79`（Debug 只设 `GenerateDebugInformation=true`）、`:88-92`（Release `false`）；链接命令默认 `/PDB:"…\bin\x64\OP_X64.PDB"`。
- **证据**：实测 Debug 探针构建产物目录里出现 `op_x64.pdb`（就是被重定向的 OutDir）；Release 链接命令行有
  `/PDB:"…\BIN\X64\OP_X64.PDB"` 但没有 `/DEBUG`，所以不产出 PDB；`lib\<arch>` 里还混着 `op_x64.exp`/`tools.exp`。
- **影响**：① Debug 的 DLL 与 PDB 混在交付目录，容易被一起拷进发行包（泄漏符号路径）；② Release 出崩溃转储后无法符号化，
  线上问题只能靠猜；③ `bin`/`lib` 里 `.pdb/.exp` 噪音。
- **建议**：加 `<ProgramDataBaseFileName>$(IntDir)$(TargetName).pdb</ProgramDataBaseFileName>`（PDB 归 IntDir），
  Release 改 `<GenerateDebugInformation>DebugFull</GenerateDebugInformation>`（`/Zi` 不影响优化，PDB 不进发布包即可）。
- **置信度**：高。

### P2-5 `WindowsTargetPlatformVersion` 硬编码 `10.0.22621.0` 且无条件覆盖

- **位置**：`libop.vcxproj:27`、`op_x64.vcxproj:33`、`op_c_api_x64.vcxproj:33`、`tools.vcxproj:32`、
  `cpp_call.vcxproj:32`、`tests/op_test.vcxproj:47`。
- **证据**：6 个工程全部写死；本机 SDK 恰好有该版本，所以构建通过。只有 10.0.19041/10.0.26100 的机器会 `MSB8036`，
  且因为赋值无条件，`/p:WindowsTargetPlatformVersion=…` 虽然能覆盖（全局属性优先），但文档没写，用户只能改工程文件。
- **建议**：`<WindowsTargetPlatformVersion Condition="'$(WindowsTargetPlatformVersion)' == ''">10.0.22621.0</WindowsTargetPlatformVersion>`；
  或直接留空用 `$(WindowsSDKVersion)`/"latest installed"，并在 `BUILD_VS.md:30` 写清最低 SDK 与覆盖方法。
- **置信度**：高。

### P2-6 版本号在 4 处硬编码，改版本必然漂移

- **位置**：`vs/op.common.props:27`（`OP_VERSION="0.4.8.3"`）、`libop/com/op.rc:61-62`（+ 79/84 的字符串）、
  `libop/c_api/op_c_api.rc:9-10`（+ 27/31）、`libop/base/AutomationModes.h:105`（`#ifndef OP_VERSION` 兜底）。
- **证据**：本次产物 `cpp_call.exe` 输出 `0.4.8.3`、资源 `FileVersion` 也是 `0.4.8.3`（当前一致）；
  但这是"人工同步"的结果，没有任何脚本校验。
- **影响**：下一次升版若漏改一处，会出现 `OpVer()` 与 DLL 资源版本不一致，Twinkle/安装器按资源判断版本时会误判。
- **建议**：只保留 `AutomationModes.h` 里的 `MAKE_OP_VERSION(a,b,c,d)` 单一来源，`op.rc`/`op_c_api.rc` 改用 `#include "version.h"` 生成，
  或让 `op.common.props` 从 `include/op_version.h` 读宏（MSBuild 不好解析 C++ 宏，实务上更简单的是加一个
  `scripts/bump-version.ps1` 一次改 4 处 + 构建后 `dumpbin /headers` 校验）。
- **置信度**：高（漂移风险）。

### P2-7 依赖引导不可复现：BlackBone 无 commit pin、vcpkg 无 baseline、OpenCV 无校验和

- **位置**：`scripts/bootstrap-deps.ps1:210`（`git clone https://github.com/DarthTon/Blackbone` 默认分支）、
  `:169-173`（`vcpkg install minhook:$triplet` 用本机 vcpkg 的 baseline）、`:321-330`（下载 tag zip，无 sha256、无重试）。
- **证据**：脚本里没有任何 commit/tag/baseline 常量；`-Force` 也不会重新拉源码（`if (-not (Test-Path …CMakeLists.txt))` 才下载，`:321`）。
  本机 `build\_deps\BlackBone` 是某个历史时刻的默认分支快照（`.git` 在，但没记录上游 commit 到工程里）。
- **影响**：半年后换机器/换人，BlackBone 上游目录结构或 CMake target 名一变，`vs/op.deps.props:39-47` 的 3 个候选路径全部落空，
  报错是"找不到 BlackBone.lib"，而实际是"上游改了布局"；vcpkg 升级也可能再次改 `minhook.x32.lib` 名字（P2-8 已见端倪）。
- **建议**：脚本里固定 `git clone --branch <tag/commit>`（或 clone 后 `git checkout <sha>`）并把 sha 写进 `BUILD_VS.md`；
  vcpkg 用 `--x-builtin-ports-root`/`vcpkg.json` 固定 baseline；OpenCV zip 校验 `Get-FileHash`。
  同时在构建成功后把"实测 deps 版本三元组（BlackBone sha/OpenCV tag/minhook ver）"写进 `build\_deps\deps.lock`。
- **置信度**：高。

### P2-8 依赖路径/包名靠"存在性探测 + 兜底"拼接，失败时错误信息与真实原因脱节

- **位置**：`vs/op.deps.props:39-47`（BlackBone 3 候选 + 2 兜底）、`:69-74`（MinHook 4 候选 + 2 兜底）、`:88-95`（OpenCV vc18→vc17→vc16）、
  `:59`（vcpkg 兜底 `Y:\Tools\vcpkg`）、`bootstrap-deps.ps1:110`（同样写死 `Y:\Tools\vcpkg`）。
- **证据**：本机 `VCPKG_ROOT=Y:/Tools/vcpkg` 才走对；`build\_deps\vcpkg` 不存在。若换到没有 Y: 盘的机器，
  `OpVcpkgRoot` 会落到 `Y:\Tools\vcpkg`（不存在），MinHook 落空 → `op_x64` 的预检（`:122-123`）能报错，
  但 `libop.vcxproj` 是 `OpBuildsAndLinksOpenCv=false`，**只查头文件**，于是会先炸在 `C1083: 无法打开包括文件 "minhook.h"`。
- **建议**：兜底路径改成"必须显式提供"（`Error` 而不是猜 `Y:\`）；MinHook 头单独 `Exists()` 检查并给出
  「请运行 `pwsh -File scripts\bootstrap-deps.ps1` 或传 `/p:OpVcpkgRoot=`」；`-DepsDir` 与 `OpDepsRoot`（props 里写死 `build\_deps`）加一致性校验。
- **置信度**：高。

### P2-9 `bootstrap-deps.ps1` 无日志文件、`-SkipOpenCv` 与 `-DepsDir` 等参数缺乏副作用说明、`-Force` 语义偏弱

- **位置**：`scripts/bootstrap-deps.ps1:46-53`（参数）、`:81-92`（`Invoke-Checked` 只往控制台写）、`:369-401`（循环与汇总）。
- **证据**：仓库里出现的 `build\_deps\bootstrap-x86*.log` 是人工 `Tee-Object` 产生的（脚本本身没有 `Start-Transcript`/`Tee`）；
  失败时只抛一个异常字符串，前面几十分钟的 cmake 输出无法回看。
  `-Force` 的注释是"增量重建，不删除构建目录"（`:31-32`），OpenCV 的 `-Force` 只会重跑 configure/build（`:339-353`），
  改了 `BUILD_LIST` 这类缓存项时仍可能沿用旧 cache（CMake 会保留未显式覆盖的 cache 值）。
- **建议**：`Start-Transcript` 或 `Invoke-Checked` 里 `| Tee-Object -Append $log`；`-Force` 增加 `-Clean`（删 build 目录但不删 src/install）；
  汇总里打印每个依赖的"版本/commit/库文件名"，方便贴 issue。
- **置信度**：高（行为可读）。

### P2-10 `OpCheckDependencies` 覆盖面不足且命名误导：只查 `opencv_core`、不查 gtest、用 `OpBuildsAndLinksOpenCv` 当作"是否链接"开关

- **位置**：`vs/op.deps.props:113-126`。
- **证据**：条件里用 `'$(OpBuildsAndLinksOpenCv)' == 'true'` 来 gate `BlackBone.lib`/`MinHook`/`opencv_core` 三项检查（属性名与用途不符）；
  **没有** gtest 检查，**也没有** `opencv_geometry/stereo/features/...` 与 `libjpeg-turbo/libpng/zlib/libclapack` 的检查。
  按脚本逻辑（`Find-OpenCvCoreLib` 只看 `opencv_core*.lib`，`:254-262`）：若 `bootstrap` 中途失败（断网/磁盘满）留下"只有 opencv_core"的 install，
  `Install-OpenCv` 会判定"已存在"并跳过（`:314-317`），`OpCheckDependencies` 也通过，最后在链接期报 `LNK1104: opencv_geometry500.lib`（未实测，属推理）。
- **建议**：把检查拆成 `OpCheckHeaders` / `OpCheckLibs` 两个开关；逐库 `Exists()`（`OpOpenCvLibs` 已经是分号列表，可 `foreach` 生成 Error）；
  加 `gtest.lib`；把 `Find-OpenCvCoreLib` 改成"检查 `OpOpenCvLibs` 全清单"。
- **置信度**：高。

### P2-11 `op_test` 每个解决方案配置都 `Build.0`，且重复链接重量级静态库

- **位置**：`op.sln:52-55,76-79`（op_test 四配置都 Build）；`tests/op_test.vcxproj:132-133`（`OpLinkHeavyDeps=true` → 连 OpenCV/BlackBone/MinHook + gtest + 10 MB exe）。
- **证据**：一次完整 Release x64 构建里 `op_test.vcxproj -> bin\x64\op_test.exe` 10,482,176 字节；`op_test` 还重编
  `WindowLayout/CursorShape/OpenCvHelpers/TemplateMatcher` 4 个 TU。
- **影响**：只想出 DLL 的人也要多花 ~20-30 s 编 132 个用例并链 10 MB exe；CI 分片也无法跳过。
- **建议**：`op_test` 的 `Release|<plat>` 只保留 `ActiveCfg`（不进 Build.0），或把 op_test 移到单独的解决方案 / `tests.sln`；
  文档里把"`MSBuild op.sln`"和"`MSBuild tests\op_test.vcxproj`"分开。
- **置信度**：高。

### P2-12 `op_x64` 与 `libop` 各编译一遍全部 libop 源码（66 TU ×2），构建时间接近翻倍

- **位置**：`op_x64.vcxproj:111`（import `op.sources.props`）与 `libop.vcxproj:81`（同）；`vs/op.sources.props:5-6` 的注释说明了原因（hook 导出）。
- **证据**：Release x64 Rebuild 105 s，其中 `libop.lib`（23.8 MB）+ `op_x64` 两套 obj 同时生成；
  `build\vs\x64\Release\{libop,op_x64}` 各 66+ 个 `.obj`。
- **影响**：本机/CI 构建时间 ×~1.8；磁盘 obj 体积翻倍。
- **建议（可行性需验证）**：`op_x64` 改为"只编译 5 个 com 源 + `libop.lib` + `/WHOLEARCHIVE:libop.lib` + `/DEF:op.def`"。
  `/WHOLEARCHIVE` 会把 `HookExport.obj` 拉进来，`/DEF` 的导出就能解析（这样只保留一次全量编译）；
  若担心 `op.def` 与 `dllexport` 冲突，可让 `HookExport` 只在 `libop` 里编一次并测试导出表是否仍是 215 项。
- **置信度**：中（方案需实测；问题事实高）。

### P2-13 文档与工程实际不一致（5 处，均已核对）

| # | 文档位置 | 文中说法 | 实际 | 严重度 |
| --- | --- | --- | --- | --- |
| a | `doc/cpp-only/BUILD_VS.md:25` | "构建配置只有 `Debug\|x64` 与 `Release\|x64`" | 6 工程 + sln 都有 `Win32`，同文 §4.2/§6 也在构建 x86 —— 自相矛盾（`269b8b1` 加 x86 时漏改） | P2 |
| b | `doc/cpp-only/BUILD_VS.md:22` | "`vs/op.deps.props` … 全部相对 `$(SolutionDir)`" | 实际用 `$(MSBuildThisFileDirectory)` / `$(OpRepoRoot)`（`vs/op.deps.props:10`），且**刻意不用** `$(SolutionDir)` 才能在单工程 msbuild 下工作 | P2 |
| c | `README.md:63`、`README_EN.md:63` | "tests/ … 当前未纳入解决方案" | `op.sln:18` 已包含 `op_test`，4 配置可构建（实测） | P2 |
| d | `BUILD_VS.md:174-180` | `op_c_api` 198 导出 / `op_x64` 213 导出 | 实测 **200 / 215**（`dumpbin /exports` 的 `number of functions`） | P2 |
| e | `BUILD_VS.md:245-248`、`CLAUDE.md:102-103` | Debug 报 `LNK2019`（`cv::debug_build_guard`） | 首发是 `LNK2038`（RuntimeLibrary/_ITERATOR_DEBUG_LEVEL）+ `LNK4098`；`debug_build_guard` 那关更靠后 | P2 |

另有 2 处非错误但会误导的表述：
- `BUILD_VS.md:22` 的表格把 `vs/op.deps.props` 说成"路径全部相对 SolutionDir"，而 `README.md:143` / `CLAUDE.md:98-101` 又说"输出目录集中在 `vs/op.deps.props`" —— 建议统一成
  「依赖路径 = `$(OpRepoRoot)build\_deps`，输出 = `bin/lib/{x64,x86}`，中间 = `build/vs/{x64,Win32}/<Cfg>`」。
- `BUILD_VS.md:103-104` 说"MIDL 是自定义 target，`op.idl` 变化或 `op.tlb` 丢失时会自动重新生成" —— 行为正确，但没提"Clean 不会删 `op_i.c/op_i.h`"（见 P2-1）。

> 工作区现状（审计结束）：只有"vcpkg 包列表含 gtest"这一项在并行改动中补了（`README.md:107`、`README_EN.md:105-108`、
> `BUILD_VS.md:48`）；**上表 a/b/c/d/e 五项在 HEAD 与工作区都仍未修**，逐条修完需要同时改 `BUILD_VS.md`、两个 README 与 `CLAUDE.md`。

---

## 5. 优化空间（可以不动正确性，只提效率）

1. **消除重复的全量编译（P2-12）**：105 s → 预计 ~60-70 s（`libop` 与 `op_x64` 的 66 TU 只编一次）。
2. **MIDL 只生成一次（P2-2）**：去掉 `tools` 的 MIDL 或共享生成目录，省 2 次 midl + 5 个文件；顺手去掉 `/proxy` `/dlldata`。
3. **`op_test` 默认不随 Release 构建（P2-11）**：整解决方案 Rebuild 少 ~25 s 和 10 MB 链路。
4. **OpenCV 体积**：当前 `op_x64.dll` = 10.2 MB、`op_c_api_x64.dll` = 10.1 MB，两者各自静态包含一份 OpenCV+BlackBone（总 ~20 MB，Twinkle 两个都发）。
   `op_c_api` 其实是 `libop` 的薄封装，可以考虑让 `op_c_api` 动态转发/或让 Twinkle 只用其一；
   若只想要单文件，可用 `BUILD_LIST` 再裁 `stereo/calib`（当前 9 个 OpenCV 模块，实际模板匹配/特征匹配只用到 core/imgproc/imgcodecs/features/calib 的一部分）。
5. **增量性**：现状已经很好（1.6 s 空转）；如果做了 P2-2 的共享生成目录，注意别引入"每次必跑"的 target，否则会破坏这个 1.6 s。
6. **依赖缓存**：`build\_deps` 全在本地且不进 git（`.gitignore:19` 的 `Build` 模式顺带忽略 `build/`），换机器/CI 需要 10-30 分钟重建；
   建议把 `BlackBone.lib` + OpenCV install 打成 artifact（或记录 `deps.lock` 后走缓存），与 P2-7 的锁定一起做。
7. **`lib\<arch>` 里的 `.exp`**：`op_x64.exp`/`op_c_api_x64.exp`/`tools.exp` 对 DLL 使用方无意义，可加到 `.gitignore` 或输出到 IntDir（`<Link><ImportLibrary>` 已指定，`.exp` 跟随 IMPLIB 目录，无法单独搬，接受即可）。
8. **`/Oy-`（`OmitFramePointers=false`，`vs/op.common.props:45`）**：Release 也关掉 FPO，会损失少量性能。
   如果是为 hook/栈回溯稳定性刻意为之，建议在 props 注释里写明；否则 Release 可改成 `true`。
9. **`SDLCheck=false` + `ConformanceMode=false` + `TreatWarningAsError=false`（`:41-43`）**：与旧 CMake 一致（旧版也没有 `/WX`、`/permissive-`），
   属于"保持等价"的取舍；若要提高质量，建议至少对 `libop/op/*.cpp`（`op::Op` 新分文件）单独开 `/permissive-` 试一次。

## 6. 与旧 CMake 的差异对照（回归风险表）

| 主题 | 旧 CMake（d00f837） | 新原生 VS | 风险 |
| --- | --- | --- | --- |
| libop 源清单 | `OP_*_SOURCES` 分组合并 | `vs/op.sources.props` 66 条 | 无（已逐条核对） |
| WGC / OpenCV 开关 | `option(enable_wgc)`、`OP_HAS_OPENCV` 自动探测 | **恒开**（`OP_ENABLE_WGC` 写死、OpenCV 源恒编） | 失去"无 OpenCV/enable_wgc=OFF"构建能力；对本 fork 是有意决策，但文档未声明 |
| C++20 局部覆盖 | `set_source_files_properties(... /std:c++20)` | 同 7 个文件 + 同宏 | 无 |
| MIDL | 自定义 `add_custom_command` → `libop/com_generated/` 一次 | 每工程 target → `$(IntDir)`，两处重复 | P2-2；且 Clean 语义不同（P2-1） |
| op_com 编译集合 | 全部 libop + COM 源 | 同（`op_x64.vcxproj:111-123`） | 无（注释里"必须全量重编否则 hook 导出丢失"与 CMake 一致） |
| 静态库消费 | `target_link_libraries(libop PRIVATE …)` 对 static 会以 `$<LINK_ONLY>` 传给消费者 | `op_c_api`/`cpp_call`/`op_test` 各自显式列 OpenCV/BlackBone/MinHook | 无（但以后新增消费方必须记得 `OpLinkHeavyDeps=true`） |
| 安装/输出 | `install(... RUNTIME bin/${arch} ARCHIVE lib/${arch})` | `OutDir`/`ImportLibrary` 直接写 | 无；额外多了 `libop.lib` 安装（改进） |
| Debug | `/MT`（`CMAKE_CXX_FLAGS_DEBUG "/MT /Zi /EHa"`） | `/MTd`（`vs/op.common.props:71`） | **回归**：见下方说明 |

> 最后一行是本次审计里最值得注意的"隐藏回归"：
> - 旧 CMake 的 Debug 是 **`/MT`**（不是 `/MTd`）。实测（`cl /EP` 探针）`/MT` 下 **`_DEBUG` 不定义**，
>   因此 `cv::debug_build_guard` 不会触发、CRT 指令与 Release 版 OpenCV/gtest/BlackBone 完全匹配 ——
>   也就是说**旧 CMake 的 Debug 配置本来是可以链接的**（`BUILD_VS.md:245-248` 把"Debug 需要 Debug 版 OpenCV"写成了历史事实，其实是新工程引入 `/MTd` 之后才出现的）。
> - 新工程把 Debug 换成 `MultiThreadedDebug`：`/MTd` 会定义 `_DEBUG`（已实测），于是同时踩两个雷：
>   `LNK2038`（MT_StaticRelease vs MTd_StaticDebug、_ITERATOR_DEBUG_LEVEL 0 vs 2）+ 若绕过则 `cv::debug_build_guard` 的 `LNK2019`。
> - **最小修复（对齐旧 CMake 语义）**：把 Debug 的 `RuntimeLibrary` 也设成 `MultiThreaded`。
>   代价：`assert`/STL 迭代器调试关闭（这是旧 CMake 一直以来的实际状态），且 `op.rc` 的 `#ifdef _DEBUG` FILEFLAGS 不会再标 debug。
>   若确实要"真 Debug"（assert + 迭代器调试），则必须补整套 Debug 依赖（OpenCV`d.lib`/gtestd/BlackBone Debug）并显式处理 `CV_IGNORE_DEBUG_BUILD_GUARD`。
> - 这个选择（"假 Debug 但能编" vs "真 Debug 但要多套依赖" vs "删掉 Debug 配置"）建议由负责人拍板，不要维持现状（声明了却编不过）。

## 7. 复核命令（可直接复制）

```powershell
cd Y:\sourcecode\cpp\Twinkle\external\ops

# 1) Release 全量（应 0/0）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m

# 2) 增量（应 1-2 s 且 MIDL skipped）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /p:Configuration=Release /p:Platform=x64 /m /v:n

# 3) Debug 现状（预期 LNK2038；把输出重定向以免污染 bin/）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op_x64.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64 `
    /p:OutDir="$env:TEMP\opdbg\out\\" /p:IntDir="$env:TEMP\opdbg\obj\\" /p:OpTwinkleLibDir="$env:TEMP\opdbg\no-twinkle\\" /m

# 4) Clean 残留（应看到 op_i.c/op_i.h 还在、op.tlb 被删）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' tools.vcxproj /t:Clean /p:Configuration=Release /p:Platform=x64

# 5) 导出表 / 依赖表
dumpbin /exports bin\x64\op_c_api_x64.dll | Select-String 'number of functions'
dumpbin /dependents bin\x64\op_x64.dll

# 6) 测试
.\bin\x64\op_test.exe --gtest_list_tests
.\bin\x64\op_test.exe --gtest_filter="UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*:OpTest.*"

# 7) 只出 DLL 且不写 Twinkle 目录
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' op.sln /p:Configuration=Release /p:Platform=x64 /m `
    /p:OpTwinkleLibDir="$env:TEMP\no-twinkle\\"
```

## 8. 建议的修复顺序

1. P1-1（PostBuild 加 `Condition`）—— 一行改动，立刻消除"构建污染 Twinkle"。
2. P1-3（bootstrap 装 gtest）—— 一行改动，恢复"全新机器一条命令可构建"。
3. P1-5（收窄 vcpkg include）—— 小改动，消除最隐蔽的头文件混用回归。
4. P1-4（头文件同步）—— 加一条 copy 或脚本。
5. P1-2（Debug 决策）—— 最快是 Debug 用 `/MT`（已实测 `/MT` 下 `_DEBUG` 不定义、不会触发 `debug_build_guard`，等价旧 CMake），
   或者干脆删掉 Debug 配置；"真 Debug" 需要整套 Debug 依赖，成本最高。
6. P2-1/P2-2（MIDL 进 FileWrites + 共享生成目录）—— 一次重构两个问题。
7. P2-13（文档 5 处）—— 纯文案，建议同批改掉，避免下一个人再被 `BUILD_VS.md:25` 误导。
