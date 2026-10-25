# DPI 感知说明（OpCreate / SetProcessDPIAware）

> 关联问题：Twinkle `docs/_research/op_composite_fix.md` 第 4.1 节 / 第 5 节风险 #1
> 关联 API：`OpSetProcessDpiAware` / `OpGetProcessDpiAware` /
> `op::Op::SetProcessDpiAware` / `op::Op::IsProcessDpiAware`

## 1. 问题

旧版 `op::internal::OpContext` 构造函数里无条件调用：

```cpp
::SetProcessDPIAware();
```

`SetProcessDPIAware` 是**进程级、一次性**的：第一次 `OpCreate` 会把整个宿主进程
从“DPI 未感知”改成“系统 DPI 感知”。在 150% DPI 下：

- OP 加载前：窗口 client = `1136x676`（DPI 虚拟化后的逻辑值）
- OP 创建后：`GetClientRect` = `1704x1014`（物理值，1.5x）
- 其他 provider（MuMu/ADB native）仍是 `1136x640`/`676`
- 同一组合里 vision + input 混用不同 provider 时，坐标可能差 1.5 倍

真正的问题不是“OP 用物理像素”本身，而是**坐标系由加载顺序决定**，且库偷偷改了
宿主进程的全局状态。

## 2. 新 API

### C API（`include/op_c_api.h`）

```c
// 进程级 DPI 感知设置；必须在第一次 OpCreate 之前调用。
// enable=1：第一次创建 Op 时调用 SetProcessDPIAware()（物理像素）
// enable=0：不修改宿主进程的 DPI 感知（默认）
// 返回 1=设置被接受；0=已经创建过 Op，设置已锁定
OP_C_API int OP_CALL OpSetProcessDpiAware(int enable);
OP_C_API int OP_CALL OpGetProcessDpiAware(void);
```

> `OpGetProcessDpiAware` 现在返回 **OS 实际状态**（0=unaware，1=aware）：
> 优先用 `GetThreadDpiAwarenessContext` + `GetAwarenessFromDpiAwarenessContext`，
> 旧系统回退 `IsProcessDPIAware`。它不再返回 OP 内部配置值。

### C++（`include/libop.h`）

```cpp
static bool op::Op::SetProcessDpiAware(bool enable);
static bool op::Op::IsProcessDpiAware();
```

用法：

```cpp
// 需要物理像素（例如独立截图工具）：
OpSetProcessDpiAware(1);   // 必须在第一次 OpCreate 之前
op_handle h = OpCreate();

// 不希望库改宿主 DPI（Twinkle 默认）：
OpSetProcessDpiAware(0);   // 可省略；默认就是 0
op_handle h = OpCreate();
```

## 3. 默认值与限制

- **默认 0（关闭）**：OP 不再调用 `SetProcessDPIAware()`，由宿主通过
  manifest / `SetProcessDpiAwarenessContext` 自行决定。
- 必须在第一次 `OpCreate`（或构造 `op::Op`）之前调用；之后返回 0，**无法撤销**。
- 进程级，不是 per-handle；不要试图在多个 `op_handle` 上分别设置。
- 如果宿主 manifest 已经声明 DPI 感知，`SetProcessDPIAware()` 可能失败/无效，
  此时以 manifest 为准。
- COM 客户端没有“创建对象前”的 API 入口；COM 路径默认同样为 0。需要物理像素的
  COM 宿主应在进程启动时自行设置 DPI 感知。
- 如果多线程同时首次创建 `op::Op`，内部用 mutex 保证“配置 → 锁定 → 调用”原子化。

## 4. Twinkle 侧建议

### 4.1 立即修复

`OPController::Initialize()` 在 `OpCreate()` 前显式调用：

```cpp
OpSetProcessDpiAware(0);  // 保持与其它 provider 一致的 DPI 坐标系
m_handle = OpCreate();
```

默认值已经是 0，但显式调用可以防止未来上游把默认值改回 1。

### 4.2 坐标归一（仍需做）

DPI 开关只解决“加载顺序决定坐标系”。同一 DPI 下不同 provider 的帧尺寸仍可能不同：

| provider | 帧尺寸 |
| --- | --- |
| MuMu native / ADB | 1136x640 |
| GDI | 1136x676（含标题栏/边框） |
| OP（DPI 关闭，client） | 1136x676 左右 |
| OP（DPI 开启） | 1704x1014（150%） |

建议在 `CompositeController` 入口维护一个**规范坐标系**（如 1136x640）：

1. 每个 provider 报告 `capture_width/capture_height` 与 `input_space`；
2. vision 帧统一缩放到规范坐标系（或把识别点从 capture 空间映射到规范空间）；
3. input 坐标从规范空间映射回目标 provider 的输入空间；
4. 混用 vision/input 时不再假设两者尺寸相同。

仅靠 DPI 开关不能替代这一步，尤其是 GDI 的 36px 边框差异和窗口缩放。

## 5. 验证（2026-10-25）

探针程序（`dpi_probe.exe`，临时）：

| 场景 | configured | actual（GetAwarenessFromDpiAwarenessContext） |
| --- | --- | --- |
| 默认（不调用） | 0 | 0 = unaware |
| `OpSetProcessDpiAware(0)` + OpCreate | 0 | 0 = unaware |
| `OpSetProcessDpiAware(1)` + OpCreate | 1 | 1 = system-aware |
| OpCreate 之后再设置 | 返回 0 | 保持原值 |

构建：`op.sln` Release|x64，0 warning / 0 error；`op_c_api_x64.dll` 已导出
`OpSetProcessDpiAware` / `OpGetProcessDpiAware`。
