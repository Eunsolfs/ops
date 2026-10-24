# OP 运行时审计报告（精简分支 slim/cpp-only-vs）

> **快照说明**：本报告是 HEAD `cdb2eb3` 的审计结果；部分 P0/P1 已在工作区修复，
> 当前状态以 [AUDIT_SUMMARY.md](AUDIT_SUMMARY.md) 为准。

- 审计对象：`Y:\sourcecode\cpp\Twinkle\external\ops`，分支 `slim/cpp-only-vs`，HEAD `cdb2eb3`
- 对比基线：`git diff d00f837..HEAD`（移除点阵字库/绑定 → 纯 C++；新增 HTTP OCR；新增 DPI 开关；新增 x86 配置）
- 审计类型：**只读**。未修改任何源码，未提交 git；仅在 `build/` 下临时编译了一个探针程序验证两条结论，验证后已删除（输出见附录 A）。
- 审计方式：逐文件读源码（`libop/{image,ocr,op,binding,capture,c_api,com,ipc,base}`）+ 对照 `doc/cpp-only/{API_CHANGES,DPI,BUILD_VS}.md` 的承诺 + 小规模实验验证。
- 结论概览：**P0 × 2，P1 × 7，P2 × 7**。最需要马上处理的是 F-01（绑定失败后鼠标/键盘空指针）与 F-02（GDI 共享帧无容量校验导致越界写）。
- 说明：本次审计只新增本文件。审计期间工作区里 `README.md`、`README_EN.md`、`doc/cpp-only/BUILD_VS.md`、`doc/ocr.md`、
  `scripts/bootstrap-deps.ps1`、`tests/test_support.cpp` 出现的改动来自并行进行的另一项任务（gtest/依赖脚本），与本报告无关。

| 编号 | 级别 | 区域 | 一句话 |
| --- | --- | --- | --- |
| F-01 | **P0** | binding/input | `BindWindowEx` 失败路径把 `_mouse/_keyboard` 置空且不恢复 → 之后任何鼠标/键盘调用解引用空指针 |
| F-02 | **P0** | capture/gdi | GDI(printwindow) 路径按“当前窗口尺寸”写共享内存、按“绑定时的尺寸”算容量，窗口变大即越界写 |
| F-03 | P1 | image/ocr | `point_t::operator<` 不是严格弱序，却被用作 `std::map` 键 → UB + OCR 框丢失/文本顺序错乱 |
| F-04 | P1 | image/ocr | `ImageSearchService::OCR` 无条件 `return 1`，OCR 后端不可用被伪装成“识别成功但没文字” |
| F-05 | P1 | ocr | OCR 全局 mutex 覆盖整个 HTTP 往返 + 进程级单例 → 所有 Op/线程的 OCR 完全串行，后端卡死时最长阻塞 ~4×timeout |
| F-06 | P1 | base/ocr | `utf8_to_ansi` 硬编码 CP936，再经 `_s2wstring`(CP_ACP) 二次转换 → 非 GBK 字符丢失、非中文系统整段乱码 |
| F-07 | P1 | image | 积分图 `record_sum` 用 `int` 累加 → ≥5K 或双 4K 全桌面 FindPic 溢出（UB/wrong result） |
| F-08 | P1 | com | COM 层零 `try/catch`（与 C API 全包裹相反）+ `FindStr` 不判空 `retx/rety` |
| F-09 | P1 | image | `FindLine` 没找到直线也输出 `"0,0"`（返回值被上层丢弃）；每次调用重建 360×对角线 `_sum`；O(像素×360) 无早退 |
| F-10 | P2 | binding | `check_bind()` 未绑定时隐式绑定桌面窗口（副作用 + `IsBind()` 语义漂移） |
| F-11 | P2 | image | 颜色字符串空/尾随分隔符语义与 `FindColor` 不一致（静默切到“背景色自动二值化”/多出一个 `000000`） |
| F-12 | P2 | ocr/network | 正则解析局限：嵌套对象、`"code"` 误匹配、空结果判定、大响应下 `std::regex` 成本 |
| F-13 | P2 | capture/image | 单次 FindPic 触发 ~5 次全帧遍历；GDI 每次截图重建 DC/位图；无 frameId 级缓存 |
| F-14 | P2 | base/input | `Delay()` 忙等 `PeekMessage` 占满一核；`KeyPressStr` 每字符调用一次 |
| F-15 | P2 | dpi | `SetProcessDPIAware()` 返回值被丢弃，`OpGetProcessDpiAware` 可能报 1 而进程实际未感知 |
| F-16 | P2 | c_api | C API 完全未文档化线程安全与 `const wchar_t*` 返回值生命周期（同一 buffer 复用） |

---

## F-01（P0）绑定失败后 `_mouse`/`_keyboard` 变成空指针，之后所有输入调用解引用崩溃

**位置**

- `libop/binding/BindingSession.cpp:157`（进入即 `reset_bind_state(false)`）
- `libop/binding/BindingSession.cpp:162-165`（非法 hwnd → `return 0`）
- `libop/binding/BindingSession.cpp:206-209 / 219-222 / 232-235`（display / mouse / keypad 模式串非法 → `return 0`）
- `libop/binding/BindingSession.cpp:346-357`（`reset_bind_state` 里 `_capture/_mouse/_keyboard` 全部 `reset()`；只有 `restore_default_input==true` 才重建 `WinMouse/WinKeyboard`，见 `:360-363`）
- 解引用点：`libop/op/OpInput.cpp:133,139,143,147,153,161,167,175,179,184,191,195,199,203,207,211,215,219,223,227,231,235,239,243,247,251,255,259,263,267,271,275,279,283,302,306,313,317,324,329,333,340,366`

**证据/复现思路**

```
1. h = OpCreate();  OpBindWindow(h, (intptr_t)真实窗口, L"normal", L"normal", L"normal", 0)  -> 1（正常）
2. OpBindWindow(h, 12345 /*已被销毁/伪造的 hwnd*/, L"normal", L"normal", L"normal", 0)
   -> Op::BindWindowEx(OpInput.cpp:109-111) 先 UnBindWindow()（恢复默认输入）
   -> bkproc.BindWindowEx -> reset_bind_state(false) 把 _mouse/_keyboard 置空
   -> IsWindow 失败，line 164 return 0   ★ 再没有任何人重建 _mouse/_keyboard
3. OpLeftClick(h) -> OpInput.cpp:191 -> m_context->bkproc._mouse->LeftClick()  // nullptr->vtable
```

同一条路径也适用于“模式串写错”（例如 `L"normal.x"`、`L"nomal"`）：`GetDesktopWindow()` 是合法窗口，
流程走到 `:206-208` 直接 `return 0`，`_mouse/_keyboard` 已经在上一步被清空。

**影响**

- COM 路径：`OpAutomation` 没有任何异常保护（见 F-08），空指针解引用直接把宿主进程带走。
- C API 路径：`call_*` 的 `catch(...)`（`op_c_api.cpp:47-51,68-74`）在 `/EHa`（`vs/op.common.props:37`）下会吞掉 AV，
  结果是**此后每一次鼠标/键盘 API 都静默返回 0，且再也无法恢复**（`_mouse` 永远为空）——比崩溃更难排查。
- 触发门槛极低：脚本里常见的“重绑到已关闭的窗口”“拼错模式串”都会命中。

**建议**

1. `reset_bind_state` 里无条件恢复默认输入后端（或把 `restore_default_input` 默认改 true，只在 `UnBindWindow` 时显式传 false 的那条路径保持一致语义）；
2. `BindWindowEx` 的 4 个 early-return 在返回前调用 `reset_bind_state(true)`；
3. `OpInput.cpp`/`Op::GetCursorPos` 等所有 `_mouse->_xxx()` 调用点加 `if (!_mouse) { set_result(ret,0); return; }`（或提供一个 `mouse()` 访问器内部保证非空）；
4. 回归测试：绑定非法 hwnd 后调用 `LeftClick/KeyPress`，断言“返回 0 而不是 AV”。

**置信度**：0.95（代码路径确定；崩溃形态取决于调用方是 COM 还是 C API）。

---

## F-02（P0）GDI printwindow 路径：共享内存无容量校验 + 不刷新尺寸，窗口变大即越界写

**位置**

- `libop/capture/backends/GdiCapture.cpp:230-289`（RDT_GDI / RDT_GDI2 走 printwindow 分支）
  - `:234-244` 用**当前** `GetWindowRect` 的 `ww/wh` 创建位图
  - `:277-280` `fmtFrameInfo(pshare, _hwnd, w, h)` 后 `GetDIBits(_hmdc, _hbmpscreen, 0L, wh, pshare + sizeof(FrameInfo), ...)`
    —— 把 `ww*wh*4` 字节写进 `_shmem`
  - `:285-289` `shared_pixels(_shmem, ww, wh)` + `gdi_row(pixels, ww, wh-1-i-y1-dy_, x1+dx_, w)`
- `libop/capture/ICaptureBackend.cpp:56-77`（共享内存大小在**绑定那一刻**按 `GetWindowRect` 定格）
- `libop/ipc/SharedMemory.h:24-43`（`OpenFileMapping` 成功就直接用，**不校验映射是否够大**；`_size` 只是查出来，没人比较）
- 对比正确实现：`libop/hook/SharedFrame.h:26-29`、`HookCapture.cpp:332-345`（Hook 路径有 `SharedFrameHasCapacity` + 显式 `x1+w>src_width` 边界检查，GDI 路径完全没有）

**证据/复现思路**

```
1. OpBindWindow(h, wnd, L"gdi", L"normal", L"normal", 0)   // 假设窗口 client 800x600
2. SetWindowSize(wnd, 1600, 1200)                          // 窗口变大，GdiCapture 没有 refreshMetrics 重载
   （grep 证实：只有 DxgiCapture.cpp:206 / WgcCapture.cpp:687 重载了 refreshMetrics）
3. OpFindColor(h, 0,0, 800,600, ...)  -> RectConvert 用旧的 _width/_height 裁剪
   -> requestCapture: ww/wh = 1600x1200，GetDIBits 写 1600*1200*4 = 7.68MB
      而 _shmem 只有 800*600*4 + header ≈ 1.92MB  -> 越界写 5.7MB
```

反向（窗口变小、`_width/_height` 仍是大的）时，`gdi_row(..., wh-1-i-y1-dy_, x1+dx_, w)` 的行/列下标会超出
新的 `ww*wh`，`std::span::subspan` 越界 → `memcpy` 越界读（release 下无检查直接读脏内存）。

**影响**

- 文件映射是 64KB 粒度，越界量普遍 ≥ 数十页 → **几乎必然 AV**；即使侥幸落在同一页，也会污染相邻映射/堆。
- `gdi`/`gdi2` 是正式支持的绑定模式（`BindingSession.cpp:180-183` 显式接受，`README.md:31` 的模式列表也列出），
  窗口 resize 在自动化场景里非常常见（游戏/模拟器/浏览器）。
- 同类隐患：`OpCaptureHelpers`/`requestCapture` 调用点没有“帧尺寸 vs 共享内存容量”的前置断言，任何后端漏检都会变成静默内存破坏。

**建议**

1. `GdiCapture` 重载 `refreshMetrics()`（或直接在 `requestCapture` 开头 `GetWindowRect` 刷新 `_width/_height` 并重算共享内存容量）；
2. 在 `ICaptureBackend` 里加统一守卫：
   ```cpp
   if (!op::hook::SharedFrameHasCapacity(*_shmem, ww, wh)) { /* 重建映射或返回 false */ }
   ```
   （`RequiredSharedFrameBytes` 已经是通用函数，GDI 路径复用它即可）
3. `gdi_row` 前置 `row>=0 && x>=0 && x+w<=src_width` 断言；
4. `SharedMemory::open_create` 在“打开到已有映射但 `size() < 请求值`”时返回 false（现在会静默接受小映射）。

**置信度**：0.8（代码路径确定；触发需要窗口在绑定后变大/变小，resize 场景很常见）。

---

## F-03（P1）`point_t::operator<` 非严格弱序，被用作 `std::map` 键 → OCR 框丢失与文本顺序错乱

**位置**

- `libop/base/Types.h:29-34`
  ```cpp
  bool operator<(const point_t &rhs) const {
      if (std::abs(y - rhs.y) < 9) return x < rhs.x;   // 9 像素内的“同一行”
      else                         return y < rhs.y;
  }
  ```
- 使用点（本分支新写的 HTTP OCR 后处理）：
  - `libop/image/ImageSearchService.cpp:485` `std::map<point_t, ocr_rec_t> ocr_res;`
  - `libop/image/ImageSearchService.cpp:490` / `:505-512` `ocr_res[it.left_top] = it;`
  - 消费点：`libop/image/ImageSearchAlgorithms.cpp:81-98` `build_ocr_text_spans()`（**按 map 迭代顺序**拼字符串）
    → `:964-989` `FindStr`、`:991-1037` `FindStrEx`

**证据（实测，见附录 A）**

```
[cmp] a<b=0 b<c=0 a<c=1  (c<b=1 b<a=1)
[cmp] cycle a<c && c<b && b<a => 1          <-- 循环，违反传递性，不是严格弱序
[map] size=3 order: (10,8) (20,0) (0,16)    <-- 插入顺序 a,b,c；迭代顺序 b,a,c
[map] equivalent keys stored: size=1        <-- (10,0) 和 (10,8) 被当成同一个 key，一个被吞
```

`(0,16) < (10,8)`、`(10,8) < (20,0)` 同时 `(20,0) < (0,16)` 成立 —— 标准要求比较器在等价类上传递，这里连
“a<b ∧ b<c ⇒ a<c”都不成立。MSVC 的红黑树不会因此立刻崩，但**顺序不可预期**，且两个“等价”键会互相覆盖。

**影响**

- `FindStr` / `FindStrEx` 的输入文本是 map 迭代序拼出来的。多行/多列 UI 下，左右两列的文本框可能被交叉拼接，
  于是 `str.find(target)` 命中一个跨行的假串 → 返回的坐标落在两列之间；或者目标串明明存在却拼不出来 → 漏检。
- 同列两个框（x 相同、y 差 < 9）会有一个被覆盖：**OCR 结果静默丢失一条**（`FindStrEx` 少一个坐标）。
- 这是标准库容器的 UB（C++17 [associative.reqmts] 要求 Compare 是严格弱序），Release 下不可依赖。

**建议**

1. 不要用 `point_t` 当 map 键；改成 `std::vector<ocr_rec_t>` + 显式 `std::stable_sort`（按 `y/9, x` 分行再按行内 x 排序），
   或直接用 `std::map<std::pair<int,int>, ...>` + 自定义安全比较；
2. 如果必须保留“≈9px 同一行”的语义，把它做成**排序键函数**（`row = y / 9`）而不是比较器，这样天然满足传递性；
3. 若要保留 `point_t::operator<`（其它地方可能也在用），必须改成严格弱序（`y` 优先、再 `x`），把“同一行”的判断搬到业务层。

**置信度**：0.9（比较器性质已用程序证伪；对 FindStr 排序的影响是确定性的，具体命中率取决于 UI 布局）。

---

## F-04（P1）`ImageSearchService::OCR` 永远返回 1，OCR 后端不可用被伪装成“成功但空”

**位置**

- `libop/image/ImageSearchService.cpp:248-263`
  ```cpp
  vocr_rec_t res;
  HttpOcrService::getInstance()->ocr(_src.pdata, _src.width, _src.height, 4, res);  // 返回值被丢弃
  ...
  return 1;                                                                        // 无条件成功
  ```
- `libop/ocr/OcrService.cpp:92-178`（`ocr()` 明确返回 `-1..-9`：空指针/尺寸非法/超 64MB/base64 失败/endpoint 非法/
  请求失败/非 200/无 code/code!=0/解析失败）
- 相关调用点同样不看返回值：`OcrEx`(`ImageSearchService.cpp:449-475`)、`FindStr`(`:477-494`)、`FindStrEx`(`:496-514`)、
  `OcrAuto`(`:516-518`)、`OcrFromFile`(`:520-530`)、`OcrAutoFromFile`(`:532-543`)

**影响**

- 宿主（Twinkle）无法区分三种状态：**没有文字** / **后端没启动** / **请求超时**。现状是都会拿到 `""` + 成功，
  于是上层“轮询 OCR 直到出现关键词”的循环会一直空转到超时，日志里也完全没有线索。
- `FindStr` 在这种情况下返回 `-1`（“找不到”），与“确实没有该字符串”不可区分；`FindStrEx` 返回 0 条同样不可区分。
- 与 YOLO 的处理方式不一致：`Op::YoloDetect`(`OpYolo.cpp:63-64`) 在 `n<0` 时保持 `{"ok":0,"code":-1,...}` 失败 JSON，
  OCR 侧丢掉了同样的信息。

**建议**

1. `ocr()` 返回值透传：`OCR` 失败返回 0（或负数），`OcrEx/FindStrEx` 也用错误码区分；
2. 若不想改 ABI/语义，至少把最后一次错误码存进 `OpContext`（例如 `last_ocr_error`），由 `GetLastError`/新 API 暴露，
   并把 `setlog` 从 `cout` 改为受 `SetShowErrorMsg` 控制；
3. 文档同步：`doc/cpp-only/API_CHANGES.md:199-200` 现在写“返回值由 0 改为 1（成功语义）”，需要补充失败语义。

**置信度**：0.95。

---

## F-05（P1）OCR 全局 mutex 覆盖整个 HTTP 往返；单例 → 所有线程/所有 Op 的 OCR 完全串行

**位置**

- `libop/ocr/OcrService.cpp:92-93` `const std::lock_guard<std::mutex> lock(m_mutex);`（函数体一直持有到 `:179`）
- `libop/ocr/OcrService.cpp:120-125`（HTTP 请求在锁内）
- `libop/ocr/OcrService.cpp:66-69`（`getInstance()` 是进程级 `static`，全进程唯一）
- `libop/ocr/OcrService.cpp:71-72`（`init/SetOcrEngine` 也用同一把锁 → 一次 OCR 卡住时 `SetOcrEngine` 也会阻塞）
- `libop/network/HttpClient.cpp:193` `WinHttpSetTimeouts(request, t, t, t, t)`，`t = OP_OCR_TIMEOUT_MS`（默认 3000，`:49-51`）

**影响**

- 语义上 `ImageSearchService` 是**每个 Op 一份**、文档也要求“每线程独立 Op”（`ImageSearchService.h:16-17`），
  但 OCR 被全局单例收口 → 多线程/多 Op 场景下 OCR 吞吐被硬串行化，与“每线程独立 Op 可以并行”的承诺矛盾。
- `WinHttpSetTimeouts` 的 4 个参数分别是 **resolve/connect/send/receive**，最坏情形一次调用 ≈ 4×timeout ≈ 12s，
  这 12s 内**全进程所有 OCR 调用**都在等这把锁（包括只想改 endpoint 的 `SetOcrEngine`）。
- 后端进程假死（端口开着但不回包）是最常见的故障形态，此时表现为“整个脚本卡住”，很难定位到 OCR。

**建议**

1. 锁只保护 `m_endpoint/m_timeout_ms` 的读取：拷一份 `endpoint/timeout` 到局部变量后立刻释放，HTTP 请求在锁外做；
2. 若确实要串行（避免打爆 OCR 服务），用一个独立的 `std::counting_semaphore`/连接池显式限制并发数，并支持超时放弃；
3. 失败后加**熔断**（例如 1s 内失败就直接返回错误，不再等满 timeout），避免上层轮询把每次调用都变成 3~12s。

**置信度**：0.95（代码事实）；对吞吐的影响取决于宿主是否多线程，但单线程下“卡死 12s”也成立。

---

## F-06（P1）OCR 文本编码：`utf8 → CP936 → CP_ACP` 双重有损转换

**位置**

- `libop/base/Utils.cpp:53-73` `utf8_to_ansi()`：`MultiByteToWideChar(CP_UTF8)` 之后**硬编码** `WideCharToMultiByte(936, ...)`
- `libop/base/Utils.cpp:17-28` `_s2wstring()`：用 `CP_ACP`（系统 ANSI 代码页）转回宽字符
- 调用链：`libop/ocr/OcrService.cpp:164-165` `ts.text = _s2wstring(utf8_to_ansi(text));`
- 同样问题在 YOLO：`libop/yolo/YoloDetector.cpp:128`

**影响**

1. **非中文系统**（ACP = 1252/1251/932…）：`utf8_to_ansi` 产出 GBK 字节，`_s2wstring` 又按 CP1252 解释 → 中文全部乱码。
   这不是“环境极端”，而是海外用户/CI 机器的默认状态。
2. **中文系统**：GBK 之外的字符（emoji、日文假名、俄文、部分符号）在 `WideCharToMultiByte(936, ...)`（默认 best-fit）
   阶段变成 `?`，**信息不可逆丢失**，之后无法再做任何匹配。
3. 现有测试（`tests/ocr_test.cpp`）都在本机中文环境跑，会掩盖第 1 类问题。

**建议**

- 直接用 `MultiByteToWideChar(CP_UTF8, ...)` 一步到 `std::wstring`，删掉中间的 ANSI 转换（`ocr_rec_t::text` 本来就是 `wstring`）；
- 若担心个别服务返回 GBK，按 JSON `Content-Type`/首个非法序列做探测，而不是硬编码 936；
- 增加“非 CP936 字符”的单元测试（直接构造 `"\xF0\x9F\x98\x80"` 之类，不依赖真实 OCR 服务）。

**置信度**：0.9。

---

## F-07（P1）灰度积分图 `int32` 累加溢出：≥5K 分辨率或双 4K 全桌面时 FindPic 结果错误

**位置**

- `libop/image/ImageSearchAlgorithms.cpp:1137-1156` `record_sum()`：`int s = 0; s += ...; _sum.at<int>(i,j) = s;`
- `libop/image/ImageSearchAlgorithms.cpp:1158-1161` `region_sum()`：四个 `int` 相减
- 调用点：`:705-706, :746-747, :819-820, :861-862`（FindPic/FindPicTh/FindPicEx*），`:927, :945`（FindColorBlock*）
- 消费点：`:1116-1135` `real_match()` 的 quick check（`abs(tnorm - region_sum(...))`）

**证据（附录 A）**

```
[sum] 3840x2160 pixels=8294400   gray_total=2115072000  INT_MAX=2147483647  overflow=0   （余量仅 1.5%）
[sum] 5120x2880 pixels=14745600  gray_total=3760128000  overflow=1
[sum] 7680x4320 pixels=33177600  gray_total=8460288000  overflow=1
[sum] int32 wrap for 7680x4320 -> -129646592
```

**影响**

- `_src` 是**本次请求区域的截图**，其尺寸上限就是窗口/桌面尺寸。5K/6K 屏、或 `BindWindow(0)` 抓整个虚拟桌面
  （两块 4K = 7680×2160）时，积分图`总和`必然溢出（有符号溢出是 UB，MSVC /O2 下按 2 的补码回绕）。
- `region_sum` 变负 → `real_match` 的 quick check 恒不成立 → **模板明明在屏幕上却匹配不到**（或反向：脏和值偶然接近阈值 → 误匹配）。
  4K（3840×2160）已只剩 1.5% 余量，任何“全白/全灰亮”的界面都可能踩线。
- `FindColorBlock` 用的是 0/1 二值图，不会溢出（最大 w*h ≈ 3.3e7），但同一份 `_sum` 容器被复用时语义容易混淆（见 F-09 的说明）。

**建议**

1. `record_sum` 的累加与存储改 `int64_t`（`_sum` 已是 4B/px 的 `Image`，改 `Image` 为 8B 或新增 `ImageSum64`）；
   内存代价 8B×(w+1)(h+1)，4K 约 66MB——若在意内存，可改为“分块积分图”（每个 block 单独 32 位），block 内和不会溢出；
2. 最低成本的止血：在 `record_sum`/`real_match` 里断言 `width*height <= INT_MAX/255`，超限时对 region 做降采样或返回明确错误；
3. 增加“5K 尺寸合成图”的 FindPic 单元测试（合成图，不依赖屏幕）。

**置信度**：0.9（溢出是算术事实；是否触发取决于屏幕/抓取区域，双 4K 桌面是现实场景）。

---

## F-08（P1）COM 层零异常保护（与 C API 相反），且 `FindStr` 不判空 `retx/rety`

**位置**

- `libop/com/OpAutomation.cpp`：整个文件 **0 处 `catch`**（`Select-String -Pattern catch` 结果为 0）。
  例：`:932-938 Ocr`、`:948-955 FindStr`、`:991-997 FindLine`、`:999-1001 SetOcrEngine` 都是直接调 `obj.*`。
- 对照：C API 每一个入口都包 `try/catch`（`libop/c_api/op_c_api.cpp:41-89`，`call_int/call_ret/call_intptr/call_string/call_json_string`）
- 会抛异常的底层：
  - `libop/image/Image.h:92-105` `CheckedImageBytes` `throw("invalid image size")` / `throw("image size overflow")`
  - `libop/image/Image.h:139-143` `throw("memory not enough")`
  - `libop/op/OpImage.cpp:323-324` `std::wregex e(s);` 在 `try` 之外构造 → 用户传入非法 `pic_name` 模式时 `std::regex_error` 直接抛出
    （`:327-334` 的 `try` 只包住了 `regex_match`）
- 空指针：`libop/com/OpAutomation.cpp:951` `retx->vt = rety->vt = VT_INT;` 无判空（同文件 `:1009-1010` 的 `YoloDetect`
  却会 `if (!ret) return E_POINTER;`，全文件判空风格不统一）

**影响**

- 异常穿过 `IDispatch::Invoke` 边界逃逸到宿主。OP 与宿主用的是 `/MT` 静态 CRT（`vs/op.common.props:71,85`），
  跨模块 unwinding 在 MSVC 上属于未定义行为，轻则进程崩溃、重则栈/CRT 状态损坏。
- 从 Lua/C# 侧看，这类错误本应是 `E_FAIL`/`DISP_E_EXCEPTION`，实际却是“进程消失”。
- `retx/rety` 为 null 时是纯 AV（`FindStr` 的三个 out 参数里只有 `ret` 是安全的，因为 `set_result` 判空，`OpResult.h:7-11`）。

**建议**

1. 在 `OpAutomation::Invoke` 或每个 `STDMETHODIMP` 外套统一宏：
   ```cpp
   #define OP_COM_TRY  try {
   #define OP_COM_CATCH } catch (...) { return E_FAIL; }
   ```
   （至少覆盖 `Ocr*`/`Find*`/`Cv*`/`Yolo*` 这些会做图像解码与正则的大方法）；
2. `FindStr`/`FindMultiColor`/`FindColor` 等写出参的方法统一 `if (!retx || !rety) return E_POINTER;`；
3. `Op::MatchPicName` 把 `std::wregex e(s)` 移进 `try`，非法模式返回空串 + `setlog`。

**置信度**：0.85（异常是否真的会抛取决于输入；`OpImage.cpp:323` 的 `std::regex_error` 是很确定的触发点）。

---

## F-09（P1）`FindLine`：空二值图返回 `-1` 却输出 `"0,0"`；每次重建 360×对角线缓冲；O(像素×360)

**位置**

- `libop/image/ImageSearchAlgorithms.cpp:1039-1078`
  - `:1041-1044` `int h = sqrt(w*w + h*h) + 2; _sum.create(360, h); _sum.fill(0);`
  - `:1052-1063` 对每个 `WORD_COLOR` 像素再循环 360 个角度（无早退、无降采样）
  - `:1064-1074` 全局扫描；`:1076-1077` `outStr = "角度,距离"; return maxval;`
- 空/无效二值图路径：`libop/image/ImageSearchAlgorithms.cpp:1178-1182` `bgr2binary` 先 `_binary.clear()`，`_src.empty()` 时直接返回
  （`ImageBin::clear()` 只把 `width/height` 置 0，**不清 `pixels`**，见 `Image.h:157-159`）
- 调用点丢弃返回值：`libop/op/OpOcr.cpp:82-85`

**影响**

1. **`outStr` 无条件赋值**（`:1076`）：即使一个 `WORD_COLOR` 像素都没有（二值图全背景，`maxval` 保持 0 或 -1），
   也照样输出 `"0,0"`（`maxRow=maxCol=0` 的初值）。`Op::FindLine` 完全丢弃返回值（`OpOcr.cpp:83-84`），
   C API/COM 只把字符串交出去 → 调用方拿到一条“角度 0、距离 0”的**假直线**，Twinkle 里就会去点一个错误位置。
   若 `_binary` 恰好为空（`width==0`，例如直接调用 `ImageSearchService::FindLine` 而 `_src` 已被 `Image::read`
   的失败路径清成 `width=0`），`h=2`、循环不执行、`maxval=-1` → **返回 -1 但仍输出 `"0,0"`**；
   公开 API 路径经过 `with_captured_region` 时 `_src` 必然非空，所以这一支是“库内部/未来调用方”的坑。
2. 更常见的路径：`FindLine` 之前 `bgr2binary` 因为 `colors` 为空（见 F-11）走 `bgr2binarybk` 的 auto 分支，
   `_binary` 语义与调用方预期不同，仍会输出一个“看起来正常”的直线。
3. 性能：`_sum` 每次 `realloc` 到 360×(对角线) 个 `int`——4K 约 6.3MB、8K 约 12.7MB，且 `fill(0)` 全量清零；
   Hough 部分对**每个匹配像素**做 360 次浮点乘加。一个半屏白色背景的帧 ≈ 2M 像素 × 360 ≈ 7 亿次运算（数百毫秒~秒级），
   而同一次调用只需要“角度/距离”两个数。`assert(d <= h)`（`:1057`）在 Release 下是空操作，等于没有保护。

**建议**

1. 空 `_binary` 时返回 0 且 `outStr.clear()`，让“无效”和“直线(0,0)”可区分；
2. `_sum` 复用成员缓冲（只是 `fill(0)`，不要每次 `create`）；角度表可以做成 `static constexpr`（现在是每次调用栈上算 720 次三角函数）；
3. 先做降采样/边缘抽样（例如按 stride 取样），或先统计“匹配像素数”为 0 时直接返回；
4. 把 `return maxval` 的语义补进文档（0 = 没有直线），并在 `Op::FindLine` 里把返回码透出（至少写进 `ret`）。

**置信度**：0.9（“0,0 + -1”确定；性能量级取决于图像密度）。

---

## F-10（P2）`check_bind()` 在未绑定时隐式绑定桌面窗口

**位置**：`libop/binding/BindingSession.cpp:503-514`，调用方 `libop/op/OpCaptureHelpers.h:35,48`

```cpp
long BindingSession::check_bind() {
    if (get_display_method().first != L"screen") return !_pic.empty();
    if (IsBind()) return true;
    // 绑定前台桌面
    return BindWindow(reinterpret_cast<LONG_PTR>(::GetDesktopWindow()), L"normal", L"normal", L"normal", 0);
}
```

**影响**

- 任何一次找色/找图（`with_captured_region` → `check_bind`）在未绑定状态下都会**真的执行一次 `BindWindow`**：
  创建后端、打开共享内存与 mutex、hook 注入（`normal` 不会注入，但会建共享资源），并让 `IsBind()` 从此返回 1。
- 上层逻辑因此失真：Twinkle 用 `IsBind()` 判断“是否已绑定目标窗口”，却发现刚创建就“已绑定”，
  绑定的还是整个桌面 → 坐标空间从“目标窗口客户区”悄悄变成“全屏物理坐标”。
- 副作用还会影响 `UnBindWindow` 的语义（用户没绑过却要解绑）、以及 F-01 的失败路径（`check_bind` 调 `BindWindow`
  也会先 `reset_bind_state(false)`）。

**建议**：把“桌面兜底”做成显式的 `SetDisplayInput(L"screen")` + `BindWindow(0, ...)` 组合，`check_bind()` 只做检查返回 false；
若必须保留，至少不要写回 `_is_bind`（用一个 `_implicit_bind` 标记区分）。

**置信度**：0.9（行为确定）。

---

## F-11（P2）`str2colordfs` 对空串/尾随分隔符的处理与 `FindColor` 不一致

**位置**

- `libop/image/ImageSearchService.cpp:274-302`
  - `:285-287` `if (color_str.empty()) return 1;`（返回 1 = “背景色模式”，且 `colors` 为空）
  - `:291-300` 按 `|`/`-` 切分；`Utils.cpp:150-164` 的 `split` 对 `"FF0000|"` 会产生一个空尾段
- 分流点：`libop/image/ImageSearchService.cpp:394-407` `str2binaryfbk()`
  - `ret == 0` → `bgr2binary(colors)`（前景色二值化）
  - `ret == 1`（**空串也会走到这里**）→ `bgr2binarybk(colors)`（背景色自动二值化，`ImageSearchAlgorithms.cpp:1206+`）

**影响**

- `FindColor("")` → `colors` 空 → 永远不匹配 → 返回 0（“没找到”）。
  但 `FindColorBlock("")` / `FindLine("")` → 静默切换成**背景色自动二值化**模式 → 用完全不同的语义去匹配。
  同一个空串在这三个 API 上含义不同，属于典型的“默认参数语义漂移”。
- `FindPic("a.png||b.png")` / 颜色串末尾多一个 `|` 会额外插入一个 `000000` 颜色（`str2color("")` 走 `swscanf` 失败 →
  三个分量保持 0，见 `Color.h:38-47`），于是“找红色”变成“找红色 **或纯黑**”。

**建议**：`str2colordfs` 里跳过空段并在 `colors.empty()` 时返回一个明确的错误码（例如 `-1`）；
`str2binaryfbk` 对 `colors.empty()` 直接返回（不产生 `_binary`），让上层返回 0。

**置信度**：0.85。

---

## F-12（P2）HTTP OCR 的正则解析局限（嵌套 / code 误匹配 / 空结果 / 大响应成本）

**位置**：`libop/ocr/OcrService.cpp:131-178`；同样的模式在 `libop/yolo/YoloDetector.cpp:104-142`

1. `obj_re = \{[^\{\}]*\}`（`:143`）：只匹配“不含花括号的叶子对象”。
   服务端若返回 `"bbox":{"x":1,"y":2,...}` 这类嵌套写法，对象内出现 `{` → 该结果被**静默跳过**（`:155-158 continue`），
   表现为“OCR 返回 0 条，但 `results` 非空”。
2. `code_re`（`:131-137`）在**整个 body** 上 `regex_search` 取第一个 `"code":<n>`：若某个结果项里也有 `code` 字段
   （有些服务会带 `"code"` per-line），可能取到错误的值 → `code != 0` → 返回 `-8`；文本内容里出现 `"code":` 也会误判。
3. `empty_result_re`（`:170-176`）：只有 `"results": [ ]` 字面量才认作“空结果”；服务端返回 `{"code":0}`、
   或返回 `"data":{"results":[]}` 之外的形状时 → 返回 `-9`（“解析失败”），把正常空结果变成错误。
4. `std::regex` 性能/栈：MSVC 的 `std::regex` 是递归匹配器，对单条超长 `text`（几 MB 的一行）或畸形 JSON 有栈溢出风险；
   而且 `obj_re` 是 O(body) 扫描，`sregex_iterator` 每个对象再跑 3 个 regex → 大响应（如 64MB 图 → base64 后 ~85MB 响应）成本显著。
   目前只有 `pixel_bytes_size_t > 64MB` 的**入参**上限（`:100-103`），**响应体没有上限**。
5. `text` 的 `\\.` 允许任意转义（`:144`），但 `json_unescape` 对 `\uD83D\uDE00` 这类代理对是否合并成单码位取决于
   `internal::json::UnescapeString`（未在本次审计范围内逐行核对），即使合并成功，也会被 F-06 的编码链再次破坏。

**建议**：用真正的 JSON 解析器（仓库已有 `libop/base/JsonUtils.h`，只是在“生成”方向；可复用/补一个轻量 parser），
至少：先定位 `"results"` 数组再逐元素解析、给响应体加大小上限（如 8MB）、`code` 只在顶层对象里找。

**置信度**：0.8（假设服务端返回形状稳定时不会触发；但这是外部进程，形状不受 OP 控制）。

---

## F-13（P2）单次 FindPic 的全帧遍历次数与截图拷贝次数偏高，且没有 frameId 级缓存

**位置**

- `libop/capture/backends/GdiCapture.cpp:159-223`：每次 `requestCapture` 都
  `release_capture_bitmap` → `CreateCompatibleBitmap` → `SelectObject` → `BitBlt`(或 `PrintWindow`) →
  `GetDIBits`(写共享内存) → 逐行 `memcpy`(到 `Image`)，最后再 `DeleteObject` 全部销毁。
  一帧 = 屏幕→DC→DIB→共享内存→`Image`，**4 次全帧搬运**，位图/DC 每次创建销毁。
  （`:210-211` `GetDIBits` 的返回值还没检查；失败时 `Image` 里是上一帧的脏数据。）
- `libop/image/ImageSearchAlgorithms.cpp:705-706`（`FindPic`）、`:746-747`（`FindPicTh`）、`:819`、`:861`：
  每次调用都 `_gray.fromImage4(_src)`（全帧 3→1 通道）+ `record_sum`（再全帧扫两遍建积分图）。
- 已有 `FrameInfo.frameId`（`GdiCapture.cpp:302-310`）但**没人用它做缓存 key**：同一帧上连续做
  `FindColor` + `FindPic` + `OCR` 时，`_gray`/`_binary`/`_sum` 会被反复重建。

**影响**：1920×1080 下 `FindPic` 单次约 2~5ms 的纯搬运/预处理开销；4K 下 ×4。轮询式脚本（每秒几十次）非常可观。

**建议**

1. 用 `FrameInfo.frameId` 缓存 `_gray`/`_sum`（`if (frame_id != cached_frame_id) rebuild`）；
2. GDI 路径复用 `HBITMAP`/`HDC`（尺寸不变时不必每次重建），并考虑 `GetDIBits` 直接写入 `Image::pdata`，
   跳过“共享内存中转”（共享内存是给 hook 跨进程用的，GDI 同进程不需要）；
3. `Image::Image(const Image&)`/`operator=`（`Image.h:125-133,165-173`）是全量 `memcpy`，
   `set_display_method` 里 `_pic = image`（`BindingSession.cpp:539,552`）会多复制一次大图，可改 `std::move`。

**置信度**：0.85。

---

## F-14（P2）`Delay()` 忙等占满一个核；`KeyPressStr` 每字符调用一次

**位置**

- `libop/base/Utils.cpp:258-269`
  ```cpp
  while (::GetTickCount64() < deadline) {
      if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
  }
  ```
  无消息时是纯自旋（没有 `MsgWaitForMultipleObjects` / `Sleep(1)`）。
- `libop/op/OpInput.cpp:360-372` `KeyPressStr`：每个字符 `::Delay(delay > 0 ? delay : 1)`。

**影响**：`OpDelay(1000)` 占满一核 1 秒（笔记本上直接风扇起飞、抢占目标进程 CPU 影响录制）；
`KeyPressStr("很长的文本", 50)` 会自旋 N×50ms。且自旋期间 `DispatchMessage` 会把宿主窗口消息也一并泵出，
对于“不希望重入 UI”的脚本是隐式风险（不过这是既有行为）。

**建议**：改成 `MsgWaitForMultipleObjects(0, nullptr, FALSE, 剩余毫秒, QS_ALLINPUT)` 循环（既等消息又让出 CPU）；
`KeyPressStr` 里改成一次性 `Delay(delay * n)` + 内部用小步长让出。

**置信度**：0.95（行为确定）。

---

## F-15（P2）DPI：`SetProcessDPIAware()` 的返回值被丢弃，`OpGetProcessDpiAware` 可能报 1 而进程实际未感知

**位置**

- `libop/op/OpContext.cpp:58-67`
  ```cpp
  g_dpi_aware_locked = true;
  if (g_dpi_aware_enabled) { ::SetProcessDPIAware(); }   // BOOL 被丢弃
  ```
- `libop/op/OpContext.cpp:44-56`（`process_dpi_aware_enabled()` 只返回配置值）
- 对外：`libop/op/OpRuntime.cpp:15-21`、`libop/c_api/op_c_api.cpp:168-176`
- 文档自述：`doc/cpp-only/DPI.md:39-41`“返回的是 OP 内部的配置值（0/1），不是 OS 实际状态”、`:68-69`“manifest 已经声明时可能失败/无效”

**影响**

- 一旦宿主进程用 manifest 声明了 DPI 感知（很常见），`SetProcessDPIAware()` 会失败（`ERROR_ACCESS_DENIED`），
  但配置值是 1 → `OpGetProcessDpiAware()` 返回 1 → 宿主据此认为“OP 已切到物理像素”，而实际感知级别可能仍是
  per-monitor（manifest 生效的值）→ 坐标换算用错基准，出现 1.25×/1.5× 偏差（这正是 DPI.md 第 1 节要解决的问题）。
- 目前只有“配置被锁定”一种失败能被感知（`OpSetProcessDpiAware` 返回 0），没有“配置被接受但未生效”的信号。
- 另外 `OpSetProcessDpiAware(0)` 在已锁定后也返回 0，调用方无法区分“你设的值和我现在的值是否一致”。

**建议**

1. `apply_process_dpi_aware_once()` 里保存 `BOOL ok = SetProcessDPIAware()`，并额外用
   `GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())` 记录**实际**级别；
2. 新增/扩展查询：`OpGetProcessDpiAware()` 返回实际级别（0=unaware,1=system,2=per-monitor…），
   把“配置值”和“生效值”分开暴露（或 `OpGetProcessDpiAwareEx(int* configured, int* effective)`）；
3. `OpSetProcessDpiAware` 已锁定时返回“当前实际值”而不是 0，避免调用方误判。

**置信度**：0.85。

---

## F-16（P2）C API 未文档化线程安全与返回值生命周期

**位置**：`include/op_c_api.h:30-44`（无任何线程/寿命说明）；实现 `libop/c_api/op_c_api.cpp:77-105`

- `call_string`/`call_json_string` 返回 `handle->string_result.c_str()`：下一次对**同一 handle** 调用会 `clear()` 重新写入，
  旧指针立即失效（典型的“static buffer”约定，但头文件没写）。
- `op_c_context` 内只有一个 `op::Op`（`libop/c_api/op_c_api.cpp:28-37`），而 `op::Op` 及其 `ImageSearchService`
  明确“不支持并发”（`ImageSearchService.h:16-17`、`OpContext.h`）。两个线程共用一个 `op_handle` 会同时写
  `string_result`、`_src`、`_gray`、`_binary`、`_sum` → 数据竞争 + 越界（`img.create` 与 `memcpy` 交错）。
- 头文件里 `op_handle` 是 `void*` 语义的不透明指针，调用方很容易“一个 handle 全局共用”。

**建议**

1. 头文件顶部加一段约定：`op_handle` 非线程安全，必须每线程独立 `OpCreate`；
   `const wchar_t*` 返回值在下一次同一 handle 的调用前有效；
2. 可选：Debug 版加“owner thread id”断言（`std::this_thread::get_id()`），把误用就地暴露；
3. `OpGetScreenData`/`OpGetScreenDataBmp`（`OpImage.cpp:219-291`）返回的是 `OpContext::screenData*` 内部指针，
   同样在下一次调用后失效，需要一并写进头文件。

**置信度**：0.9（事实层面；影响取决于调用方是否遵守未写明的约定）。

---

## 交叉主题：线程安全现状小结（对应审计要点 5）

| 对象 | 现状 | 结论 |
| --- | --- | --- |
| `ImageSearchService`（每 Op 一份） | 成员 `_src/_gray/_binary/_sum/_x1` 全共享，无锁 | 文档已声明“不支持并发”，**但 `op_handle` 没有任何运行时/文档约束**（F-16） |
| `HttpOcrService`（进程单例） | 一把 `m_mutex` 覆盖整个 HTTP 往返 | 线程安全但**串行**（F-05） |
| `YoloDetector`（进程单例） | 同 OCR 模式（`YoloDetector.cpp:77`） | 同上，且不常被注意 |
| `g_pic_cache`（进程级图片缓存） | `std::shared_mutex` 保护，`shared_ptr` 快照 | 实现正确（`ImageSearchService.cpp:20-86`）；但**无上限、无淘汰**，`LoadPic` 永远写入（`:315-333`），长期运行按图片数线性增长，且没有任何 `FreeAll` 出口 |
| `OpContext` DPI 全局态 | `g_dpi_mutex` + locked 标志 | 实现正确（`OpContext.cpp:33-67`），问题在语义（F-15） |
| `BindingSession` | 无锁 | 与 F-01/F-10 相关；`lock_data/unlock_data` 全仓无调用者（死代码） |
| Hook 共享帧 | `ProcessMutex` + `SharedFrameHasCapacity` + 显式边界检查 | **实现质量明显高于 GDI 路径**（对照 F-02） |

---

## 测试缺口（对应审计要点 9）

现有用例覆盖面不错（`image_color_test` 覆盖边界点/方向/透明图/失败清空；`opencv_test` 覆盖大量匹配路径；
`mouse_keyboard_test` 覆盖 DX/Windows 双后端；`wgc_test` 覆盖窗口捕获）。但仍缺以下与本次结论直接相关的用例：

| 缺口 | 建议用例 | 对应发现 |
| --- | --- | --- |
| 绑定失败后的输入调用 | `BindWindow(非法hwnd/非法模式串)` 后 `LeftClick/KeyPress/GetCursorPos` 必须返回 0 且不崩 | F-01 |
| 窗口 resize 后的截图 | `gdi` 模式绑定 → `SetWindowSize` 变大/变小 → `FindColor`，断言不越界（可用 PageHeap/ASAN 或断言容量） | F-02 |
| DPI 开关 | 目前只有文档里的临时 `dpi_probe`（`DPI.md:107-119`），仓库内**没有** `SetProcessDpiAware` 的 gtest；建议加“锁定后返回 0”“Get 与 GetAwareness 一致”两条 | F-15 |
| OCR 解析 | `HTTP OCR` 用例全部依赖真实服务（`tests/ocr_test.cpp`），无法覆盖畸形/嵌套/空结果/超大响应；建议把解析抽成纯函数（`parse_ocr_response(const std::string&, vocr_rec_t&)`）后做表驱动测试 | F-04/F-12 |
| OCR 失败语义 | 端点指向一个关闭的端口 → 断言返回值/错误码可区分“无文字” | F-04/F-05 |
| OCR 文本顺序 | 构造两条 `left_top` 同列、y 相差 8 与 20 的 `ocr_rec_t`，断言 `FindStr` 命中第一行 | F-03 |
| 大尺寸算术 | 5K 合成图（或直接把 255 灰度整幅填充）跑 `FindPic/FindColorBlock`，断言正确 | F-07 |
| `FindLine` | 空帧/单色帧/一条直线三种输入：断言返回值与字符串语义（现在没有一条 `FindLine` 用例） | F-09 |
| 共享内存容量 | Hook 路径已有校验；建议对 `RequiredSharedFrameBytes` 边界（0 / 溢出 / 恰好相等）做单测 | F-02 |
| C API 多线程误用 | 两个线程共用一个 `op_handle` 的“文档级”负向测试（或 Debug 断言测试） | F-16 |

---

## 附：审计中确认“没问题/已经做对”的点（避免误判为遗漏）

- C API 每个入口都有 `try/catch`，`OpCreate/OpDestroy` 也包了异常（`op_c_api.cpp:156-166`）；`in_int/out_*` 都做了空指针保护。
- COM 的窗口句柄类型用的是 `hyper`（`libop/com/op.idl:55-86`），x64 下不会截断；`tests/handle_compat_test.cpp:39-66`
  明确记录了 `LONG` 装不下 64 位 HWND 的风险。剩余截断风险在 Twinkle 侧的 `ControllerAPI int hwnd`（本仓库之外）。
- `OpGetScreenData/Bmp` 的 BMP 头计算正确（`sizeof(BITMAPFILEHEADER)=14`、`bfOffBits=54`，`wingdi.h` 里是 `pack(2)`），
  `SC_DATA_BOTTOM` 的“正 biHeight + 行翻转”与“负 biHeight + 不翻转”两种组合各自自洽。
- `Image`/`ImageBin`/`SharedMemory`/`ProcessMutex`/`WinHttpHandle` 都有 RAII（`Image.h:9-90`、`WindowsHandle.h`、
  `HttpClient.cpp:21-51`），没有发现 GDI 句柄/`HGLOBAL`/`IStream` 泄漏；`GdiCapture::UnBindEx`
  还特意先把原对象 `SelectObject` 回去再删位图（`GdiCapture.cpp:17-29`）。
- `base64_encode` 的 `needed-1` 分配是正确写法（`CryptBinaryToStringA` 的 `needed` 含结尾 NUL，
  `std::string` 的分配必然 ≥ `size()+1`）（`HttpClient.cpp:152-167`）。
- Hook 捕获路径有完整的容量/边界校验与 `ensureSharedFrameCapacity` 退避重试（`HookCapture.cpp:309-406`、`SharedFrame.h`）。
- `str2colordfs` 的“显式色差优先于 `sim` 推算”逻辑正确（`explicit_dfs` 与 `colors` 下标一一对应，`ImageSearchService.cpp:394-407`）。
- `FindColorBlock/Ex` 的 `height > _binary.height || width > _binary.width` 与 `region_sum` 的 `+1` 哨兵行/列匹配，
  单像素/极小区域的越界已被挡住（`ImageSearchAlgorithms.cpp:922-962`、`:1137-1161`）；`CmpColor/GetColor` 在屏幕右下角
  也已有 `tests/image_color_test.cpp:67` 覆盖。

---

## 附录 A：临时探针输出（`build/audit_probe.cpp`，已删除）

编译：`MSVC 14.41 /std:c++17 /EHsc /O2`，只验证比较器与算术两条静态结论，不链接 OP。

```
[cmp] a<b=0 b<c=0 a<c=1  (c<b=1 b<a=1)
[cmp] cycle a<c && c<b && b<a => 1
[cmp] k1<k2=0 k2<k1=0 (equivalent keys)
[map] size=3 order: (10,8) (20,0) (0,16)
[map] find((20,0)) -> found
[map] find((0,16)) -> found
[map] equivalent keys stored: size=1
[sum] 3840x2160 pixels=8294400 gray_total=2115072000 INT_MAX=2147483647 overflow=0
[sum] 5120x2880 pixels=14745600 gray_total=3760128000 INT_MAX=2147483647 overflow=1
[sum] 7680x4320 pixels=33177600 gray_total=8460288000 INT_MAX=2147483647 overflow=1
[sum] int32 wrap for 7680x4320 -> -129646592
```

（比较器代码逐字复制自 `libop/base/Types.h:29-34`；积分图公式逐字复制自 `ImageSearchAlgorithms.cpp:1137-1156` 的
`record_sum` 最坏情况上界 `255*w*h`。）

## 附录 B：建议的修复顺序

1. **F-01**（10 行内可修，收益最大：消掉一类必崩）；
2. **F-02**（GDI 容量/尺寸守卫，或先禁用 `gdi/gdi2`，把 `normal.auto` 的候选里去掉 printwindow 分支）；
3. **F-03 / F-04**（OCR 后处理正确性与可观测性，改动小但影响面大）；
4. **F-05 / F-06**（OCR 服务化后的可用性与编码，建议与 OCR 服务端一起联调）；
5. **F-07**（尺寸上限断言止血，再决定是否上 64 位积分图）；
6. **F-08**（COM 统一 try/catch，属于防御性投入）；
7. 其余 P2 按发布节奏排。
