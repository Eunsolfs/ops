# OP 精简改造：纯 C++ + 原生 VS 工程

> 分支：`slim/cpp-only-vs`
> 仓库：`Y:\sourcecode\cpp\Twinkle\external\ops`（fork: Eunsolfs/ops）
> 起点 commit：`d00f837`（0.4.8.3）
> 开始时间：2026-10-25
> 状态：x64/x86 构建、tests/op_test、Twinkle.sln 集成均已完成并提交。

本目录记录本次针对 OP 插件的大改造：分析过程、范围决策、API 变更、构建方式、进度与验证结果。
后续接手的人先读本文件，再看：

- [API_CHANGES.md](API_CHANGES.md)：精确的删除/保留 API 清单
- [BUILD_VS.md](BUILD_VS.md)：原生 VS2022 构建、依赖准备、根因与排错
- [DPI.md](DPI.md)：OpCreate 与 SetProcessDPIAware 的进程级 DPI 开关
- [TWINKLE_SYNC.md](TWINKLE_SYNC.md)：Twinkle 侧同步与换库步骤
- [PROGRESS.md](PROGRESS.md)：按时间记录的进度与验证结果

---

## 1. 目标

把上游 OP（Windows 桌面自动化 COM 插件）改造成 Twinkle 可直接使用的 **纯 C++ / 原生 Visual Studio** 版本：

1. 删除 Python、Go、SWIG 等脚本语言绑定及打包链。
2. 删除点阵字库 OCR、本地字库、内存字库相关能力与 API。
3. 删除 CMake / build.py 构建链，改为可用 VS2022 直接打开、构建、调试的原生 `.sln/.vcxproj`。
4. 保留 Twinkle 实际使用的能力：截图、后台绑定、键鼠输入、找色找图、OpenCV、YOLO HTTP、HTTP OCR、内存读写、窗口/进程操作、COM 自动化（用户要求保留）。
5. 同步修改 Twinkle 中引用被删 API 的封装（`Twinkle.Background/OPController`）。

## 2. 用户拍板的决策（2026-10-25）

| 决策点 | 选择 |
| --- | --- |
| 被删 API 的 ABI | **彻底删除**，并同步修改 Twinkle 的 OPController |
| COM 层 | **保留**（`op_x64.dll` 仍是 COM 插件，保留 IDL/MIDL/注册） |
| VS 工程形态 | **完全用原生 VS 工程替换 CMake**（提交 `.sln/.vcxproj`） |
| OpenCV | **保留且必需**，构建时链接静态库 |

> 注意：OpenCV/BlackBone 本身仍需要用各自的上游方式构建一次（OpenCV 只提供 CMake 工程）。
> 这一步属于第三方依赖准备，不属于 OP 自身构建链；OP 自身只用 VS 工程构建。

## 3. 现状分析（改造前）

### 3.1 架构

- `include/libop.h`：对外 C++ 门面 `op::Op`（Pimpl，DLL 导出）。
- `libop/op/`：`op::Op` 分文件实现（Runtime/Window/Input/Image/OpenCv/Ocr/Yolo/Memory）。
- `libop/c_api/op_c_api.cpp`：C API，**直接持有 `op::Op`**，不经过 COM。
- `libop/com/`：COM `IOpAutomation`（IDL 约 294 行、~300 dispid），ATL 实现。
- `libop/capture/`、`binding/`、`input/`、`hook/`、`window/`、`memory/`、`image/`、`ocr/`、`yolo/`：功能模块。
- 历史构建：`build.py` → vcpkg（minhook/directx-headers/gtest）+ BlackBone + OpenCV 5.0.0 静态库 → CMake VS 工程。

### 3.2 Twinkle 的实际集成方式

- Twinkle 只包含 `external/OP/op-include/op_c_api.h`，链接 `op_c_api_x64.lib`，运行期加载 `op_c_api_x64.dll`。
- `op_c_api_x64.dll` 的导入表只有系统 DLL，**不依赖 `op_x64.dll` / `tools.dll`**（已用 dumpbin 验证）。
- `op_x64.dll` 只在两类场景需要：COM 注册调用；Hook 截图时作为注入目标进程的载荷（`HookModule.h` / `HookCapture.cpp` 固定加载 `op_x64.dll`）。
- Twinkle 使用 `OpCv*` 系列接口，因此 OpenCV 必须保留。

### 3.3 本机工具链（已核实）

| 项 | 位置 / 版本 |
| --- | --- |
| VS | `Y:\Microsoft Visual Studio\2022`（17.11.5，v143，ATL 已安装） |
| Windows SDK | `Y:\Windows Kits\10`（10.0.22621，含 midl.exe） |
| CMake | 4.4.3 |
| Python | 3.10.10 |
| vcpkg | `Y:\Tools\vcpkg`（已安装 x64/x86-windows(-static) 的 minhook、directx-headers、gtest、opencv4、tesseract） |
| 磁盘 | Y: 剩余约 34 GB |

### 3.4 基线构建复现

- 命令：`python build.py -g vs2022 -t Release -a x64 --vcpkg-root Y:\Tools\vcpkg`
- 日志：仓库根 `build-baseline.log`（未提交）
- 结果：见 [PROGRESS.md](PROGRESS.md)（BlackBone → OpenCV 5.0.0 静态库编译过程）。

## 4. 改造方案

### 4.1 删除脚本语言绑定

删除/清理：

- `bindings/python/`、`bindings/go/`
- `swig/`、`python/`、`pyproject.toml`
- `scripts/build_wheel.ps1`
- `.github/workflows/wheels.yml`
- README/CLAUDE.md/doc 中 Python/Go/SWIG/wheel 的描述

### 4.2 删除点阵字库 OCR

删除的能力：

- 点阵字库引擎（`Dictionary.h`：OP 二进制 / 大漠文本字库）
- 本地字库文件、内存字库
- 点阵 OCR 预处理、取字、预览、字库校验/规范化/改名
- Tesseract 本地模型路径（`TesseractOcr.*`，当前未编译进目标，一并删除）

**保留**的 OCR/识别能力（走 HTTP OCR 服务）：

- `SetOcrEngine`、`Ocr`、`OcrEx`、`OcrAuto`、`OcrFromFile`、`OcrAutoFromFile`
- `FindStr`、`FindStrEx`（对 HTTP OCR 结果做字符串匹配）
- `FindLine`（二值图直线检测，不依赖字库）

完整的 C++ / C / COM API 删除清单见 [API_CHANGES.md](API_CHANGES.md)。

### 4.3 原生 VS 工程

- 新解决方案 `op.sln`，至少包含：
  - `libop`（静态库，核心）
  - `op_x64`（COM DLL + Hook 导出，保留 IDL/MIDL）
  - `op_c_api_x64`（C API DLL，静态链接 libop）
  - `tools`（EasyCom，按需）
  - 测试工程（按需，剔除字库相关用例）
- 依赖安装方式：
  - vcpkg：minhook、directx-headers（静态 triplet）
  - BlackBone：上游工程构建一次，产出 `BlackBone.lib`
  - OpenCV：上游 CMake 构建一次，产出 x64 静态库
  - 通过 `vs/op.deps.props` 集中配置路径，避免改多份 vcxproj
  - 一键准备脚本：`scripts/bootstrap-deps.ps1`
- 已删除根 `CMakeLists.txt`、`libop/CMakeLists.txt`、`tools/CMakeLists.txt`、
  `tests/CMakeLists.txt`、`build.py`、`ci/triplets/`、`.github/workflows/ci.yml`。
  当前构建入口只有 `op.sln`。

### 4.4 Twinkle 同步

- `modules/Twinkle.Background/include/OPController.h`
- `modules/Twinkle.Background/src/OPController.cpp`
- 删除对被移除 `Op*` 符号的声明与实现；保留 HTTP OCR / FindStr / FindLine 封装。
- 更新 Twinkle 使用的 `op_c_api.h` 来源（改为新仓库 `external/ops/include` 或同步覆盖 `external/OP/op-include`）。

## 5. 验证清单

- [ ] `op_x64.dll` / `op_c_api_x64.dll` x64 Release 构建通过
- [ ] `op_c_api_x64.dll` 导出符号与保留 API 一致，已删符号不存在
- [ ] 冒烟测试：`OpCreate` → `OpVer` → `OpDestroy` 正常
- [ ] 截图 / 找色 / 找图 / OpenCV 至少各跑一个冒烟用例
- [ ] Twinkle.Background 能在新头文件/新 lib 下编译
- [ ] 仓库内不再有 Python/Go/SWIG/CMake/build.py 的构建引用
- [ ] 文档与 README 更新完成

## 6. 风险与注意事项

- **ABI 破坏**：删除导出符号后，旧 `op_c_api_x64.lib` 不能与新 DLL 混用；Twinkle 必须重新链接。
- **COM 接口破坏**：删除的 dispid 会让旧 COM 客户端（C#/Lua 等）调用失败；IDL 中保留 id 空洞，避免影响其他 dispid。
- **x86**：本次以 x64 为主；x86 需要单独准备 BlackBone/OpenCV/vcpkg 依赖，工程配置保留但未验证。
- **第三方依赖**：OpenCV 5.0.0 静态库首次构建耗时长（基线实测见进度日志）；建议保留缓存的 `build/_deps`。
- **禁止提交**：`build/`、`bin/`、`build-baseline.log` 等构建产物。
