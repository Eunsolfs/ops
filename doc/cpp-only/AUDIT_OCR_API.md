# OP 精简分支 API / OCR 一致性审计（点阵字库删除后的隐藏回归）

> **快照说明**：本报告是 HEAD `cdb2eb3` 的审计结果；部分 P0/P1 已在工作区修复，
> 当前状态以 [AUDIT_SUMMARY.md](AUDIT_SUMMARY.md) 为准。

> 审计员：API/OCR 审计（只读）
> 审计基线：`slim/cpp-only-vs` @ **`cdb2eb3`（HEAD）**，对照基线 `d00f837`
> 审计范围：`libop/image/ImageSearchAlgorithms.*`、`ImageSearchService.*`、`libop/op/OpOcr.cpp`、
> `include/libop.h`、`include/op_c_api.h`、`libop/c_api/op_c_api.cpp`、`libop/com/op.idl`、
> `libop/com/OpAutomation.*`、`tests/ocr_test.cpp`、`tests/image_color_test.cpp`、相关文档
> 方法：`git diff d00f837..HEAD -- <file>` + 读 HEAD 源码 + `git show HEAD:<file>` 取行号 +
> `dumpbin /exports` 实测导出表 + MSVC 独立探针（`%TEMP%`，不入库） + 运行现有 `op_test.exe`
> **只读声明**：未修改任何源码/工程，未提交 git；本文件是唯一新增内容。

## 0. 阅读须知：审计期间工作区是脏的

本仓库在我审计期间**被另一条并行修复线（runtime 审计）持续修改**，`git status` 有 19 个已改文件
（`Types.h`、`ImageSearchService.cpp`、`OcrService.cpp`、`doc/ocr.md` 等）和一个未跟踪的
`doc/cpp-only/AUDIT_RUNTIME.md`。因此：

- **本报告所有 `file:line` 一律指 HEAD `cdb2eb3`**（用 `git show HEAD:<file>` 逐行核对），
  不是当前工作区行号。两者在 `ImageSearchService.cpp` 的 OCR 段相差 6 行。
- 每条发现都有「工作区状态」标注：`未修` / `工作区已修待提交` / `不适用`。parent 可直接据此排重。
- 编号 `A-xx` 是我的编号；与 `AUDIT_RUNTIME.md` 的 `F-xx` 重叠处在条目末尾注明，避免重复施工。

## 1. 结论摘要

| 编号 | 级别 | 主题 | 位置（HEAD） | 工作区状态 |
| --- | --- | --- | --- | --- |
| A-01 | **P1** | COM `dual` 接口中段删 27 个方法、IID/typelib 版本未升 → 早期绑定旧客户端静默错槽调用 | `op.idl:28-30,179,244` | 未修 |
| A-02 | **P1** | x86 C API 导出名带 stdcall 装饰（`_OpOcr@32`），按名 `GetProcAddress` 全部失效 | `op_c_api_x64.vcxproj:98` | 未修 |
| A-03 | **P1** | `Ocr` 与服务顺序 / `FindStr*` 的 map 顺序两套文本模型；同 `left_top` 框互相覆盖 | `ImageSearchService.cpp:256,490,510` | 部分（仅修了 UB） |
| A-04 | P2 | `OcrEx`/`FindStrEx`/`FindColorBlockEx` 结果上限 off-by-one（1800 → 实际 1801） | `ImageSearchService.cpp:468`、`ImageSearchAlgorithms.cpp:1026` | 未修 |
| A-05 | P2 | `OcrFromFile*` 不检查 `Image::read` 失败：1bpp/16bpp/损坏图 → 空帧 OCR，仍“成功” | `ImageSearchService.cpp:520-543` | 部分 |
| A-06 | P2 | `Op::FindStr` 失败路径预置 `ret=0`，与“第 0 个模式命中”不可区分 | `OpOcr.cpp:45` | 未修 |
| A-07 | P2 | `str2binaryfbk` 在 `@` 背景色分支完全忽略 `sim`（FindLine/FindColorBlock 受影响） | `ImageSearchService.cpp:394-407` | 未修 |
| A-08 | P2 | `BitFunctions.h` 删字库后已零使用者，`ImageSearchAlgorithms.h:9` 仍 include | `ImageSearchAlgorithms.h:9` | 未修 |
| A-09 | P2 | 其它死代码：`ImageView.h`、`Connectivity`/`extractConnectivity`/`gen_next`、`HEX2INT`、`str2colors`、`_dx/_dy` | 见条目 | 未修（pre-existing） |
| A-10 | P2 | 文档把被删 C API 写成“28 个”，头/实现/IDL 实测均为 **27** | `BUILD_VS.md:177` 等 5 处 | 未修 |
| A-11 | P2 | 公开头注释仍承诺 `color_format`/“自动二值化”；`doc/ocr.md` 默认端点写错 | `libop.h:462,468,471,474`、`doc/ocr.md:33` | 部分（ocr.md 已修） |
| A-12 | **P1** | 测试缺口：FindLine 零用例、OcrEx/FindStr 只打印不断言、OCR 用例无服务时全 SKIP、COM 零用例、删符号无守护 | `tests/ocr_test.cpp:25-86`、`test_support.cpp:762` | 未修 |
| A-13 | P2 | `OcrEx` 文本含逗号/竖线且无转义，直接破坏 `x,y,text` 的竖线分隔协议 | `ImageSearchService.cpp:461-466` | 未修 |
| A-14 | P2 | `SetOcrEngine` 是实例方法却改进程级单例状态，跨 Op/跨测试互相污染 | `OcrService.cpp:66-69`、`OpOcr.cpp:13` | 未修 |
| A-15 | P2 | `OCR` 不设 `_max_return_obj_ct` 上限，与 `OcrEx` 的截断语义不一致且无文档 | `ImageSearchService.cpp:256-262` | 未修 |

**级别判定口径**：P0 = 崩溃/数据破坏；P1 = 会给出错误结果或让旧客户端静默错调；P2 = 语义/文档/死代码/可维护性。

## 2. 详细发现

### A-01（P1）COM `dual` 接口中段删除 27 个方法、IID 与 typelib 版本未升 → 旧早期绑定客户端静默调用错误槽位

- **位置**：`libop/com/op.idl:28-30`（`uuid(85f31925-…)` + `dual` + `nonextensible`）、`:179`（删除块注释）、`:244`（`version(1.0)`）；`libop/com/OpAutomation.h:31`（`IDispatchImpl<IOpAutomation, …, 1, 0>`）。
- **证据**：
  - 接口是 **`dual`**：vtable 顺序 = IDL 声明顺序。删除的 27 个方法位于 `[id(213)]` 之后、`[id(250)]` 之前，因此 **dispid ≥ 250 的 57 个方法整体前移 27 个槽位**（HEAD 共 185 个方法，d00f837 为 212 个，差值正是 27）。
  - IID 未变、`version(1.0)` 未变、`coclass OpAutomation` 未变 → `CoCreateInstance` 不会报 `E_NOINTERFACE`，旧客户端拿到的仍是“同一个接口”。
  - 文档只覆盖了 late binding：`doc/cpp-only/API_CHANGES.md:203-204`「旧客户端调用这些 dispid 会得到 `DISP_E_MEMBERNOTFOUND`」。对 `IDispatch::Invoke`（按 dispid/名字）这句是对的；对早期绑定（`#import` 生成的 `raw_IOpAutomation`、VB6 编译后的 vtable 调用、`[ComImport]` 的 vtable 布局）**不会**得到该错误，而是跳到错误函数；靠近 vtable 末尾的旧槽位甚至会越过新 vtable 末尾 → 调用野指针。
- **复现思路**：用 d00f837 的 `op.tlb` 生成 `#import` 客户端（或手工 `vtable[old_slot]`）调用 `[id(250)] WriteData`，替换为 HEAD 的 `op_x64.dll` 后调用，观察落在哪个方法上（x86 下参数个数不同的 stdcall 还会破坏栈）。
- **影响**：任何早期绑定消费者在不重编译的情况下静默错调，最坏 AV；属于“删除 COM 中段方法但不换 IID”的经典陷阱。
- **建议**：① 首选新 IID（`IOpAutomation2`）+ 新 coclass，旧的保留为退化 stub 返回 `E_NOTIMPL`/`DISP_E_MEMBERNOTFOUND`；② 退而求其次，至少 `version(2.0)` + 文档明确「仅 late binding 兼容，early binding 必须重编」；③ 加一条 COM 冒烟用例（`GetIDsOfNames("SetDict")` 应失败）。
- **置信度**：高（vtable 布局由 IDL 顺序决定，删除位置与位移量已核实）。真实影响面取决于是否存在早期绑定消费者；Twinkle 侧走 C API（`external/OP/op-include/op_c_api.h`），我未发现 COM 消费者。
- **工作区状态**：未修。

### A-02（P1）x86 C API 导出名带 stdcall 装饰，按名动态解析全部失效

- **位置**：`op_c_api_x64.vcxproj:98`（只靠 `OP_C_API_EXPORTS` → `__declspec(dllexport)`，**没有 .def、没有 `/EXPORT:` 重命名**）；`include/op_c_api.h:7`（`#define OP_CALL __stdcall`）。
- **证据**（实测 `dumpbin /exports`）：

  ```
  bin\x86\op_c_api_x86.dll : 200 number of functions / 200 number of names
      14    D 00024120 _OpCreate@0
      62   3D 00024D80 _OpFindLine@32
     138   89 00025FC0 _OpOcr@32
     142   8D 000260B0 _OpOcrEx@32
     163   A2 00026570 _OpSetOcrEngine@16

  bin\x64\op_c_api_x64.dll :  14  OpCreate / 62 OpFindLine / 138 OpOcr …（无装饰）
  ```

- **影响**：
  1. x86 宿主 `LoadLibrary` + `GetProcAddress(h, "OpOcr")` → `NULL`；所有按头文件名字动态绑定的语言层（Lua/Python/易语言/按键精灵式宿主）在 x86 上不可用，只有静态链接 `.lib` 能用。
  2. 连带使 `doc/cpp-only/BUILD_VS.md:177-179` 的验证方法失效：该处“已删的 28 个字库类符号 0 个存在”是对 x64 无装饰名做的字符串核对；在 x86 上所有导出都带 `_@N`，用同样方法核对**永远得出“不存在”**，即使真的漏删也检测不到。
- **复现思路**：`dumpbin /exports bin\x86\op_c_api_x86.dll | findstr OpOcr` → 只出现 `_OpOcr@32`；写一个 20 行的 x86 程序 `GetProcAddress(GetModuleHandle(L"op_c_api_x86.dll"), L"OpOcr")` 断言非空即失败。
- **建议**：给 C API 工程加 `.def`（逐条 `OpOcr=_OpOcr@32` 形式或直接列出未装饰名 + `PRIVATE`），或对每个导出用 `#pragma comment(linker, "/EXPORT:OpOcr=_OpOcr@32")`；随后在 x64/x86 两侧都跑一遍导出名 == 头文件声明的核对脚本。
- **置信度**：高（导出表实测）。

### A-03（P1）`Ocr` 与 `FindStr*` 使用两套文本模型：拼接顺序不同 + 同 `left_top` 覆盖

- **位置**：`libop/image/ImageSearchService.cpp:256-261`（`Ocr` 按 `res` 向量顺序拼接）、`:485-493` / `:505-513`（`FindStr` / `FindStrEx` 先灌 `std::map<point_t, ocr_rec_t>`）、`:490` / `:510`（`ocr_res[it.left_top] = it;`）、`libop/image/ImageSearchAlgorithms.cpp:81-106`（`build_ocr_text_spans` 按 map 迭代顺序拼 haystack）。
- **证据**：
  - `Ocr()` 的 haystack = 服务返回顺序；`FindStr()/FindStrEx()` 的 haystack = `point_t` 比较器定义的顺序。两者可以不同，于是 **`Ocr` 明明返回了 `"HelloWorld"`，`FindStr("HelloWorld")` 却 MISS**。
  - 同 `left_top` 的两个框在 `map` 中互相覆盖 → 其中一段文字在 `FindStr*` 里彻底消失（`Ocr` 里还在）。
  - 框与框之间拼接**没有任何分隔符**，所以服务返回按词切分时 `FindStr("Hello World")` 一定 MISS（`Ocr` 也只拼成 `"HelloWorld"`）。
  - 独立探针（HEAD 的 `point_t::operator<`，MSVC 实测）：

    ```
    a<b=0 b<a=0   (a~b: neither is less)      // a(0,0) b(0,8)
    b<c=0 c<b=0   (b~c: neither is less)      // c(0,16)
    a<c=1 c<a=0   (but a<c)                    → 不是严格弱序

    boxes[(10,100)]="OK"; boxes[(10,104)]="Cancel";
    boxes.size()=1                      // 期望 2：一段文字被吃掉

    同一条视觉行的抖动：World(300,10) / Hello(10,20)
    concatenated text = WorldHello
    FindStr("HelloWorld") -> MISS
    ```

- **影响**：`FindStr`/`FindStrEx`（Twinkle 主要靠它们做“找字点击”）会漏命中；漏命中在自动化里表现为“步骤超时/点不到”，难排查。
- **建议**：统一成一个 `build_ocr_text(const vocr_rec_t&)`（一次 `std::stable_sort` 按 `(band, x, y)`，**并且 Ocr/OcrEx/FindStr/FindStrEx 全部用它**）；同坐标框用 `std::vector` + 前缀和做 index→框 映射，杜绝覆盖；若保留无分隔符拼接，至少在文档写清“多词会被粘在一起”。
- **置信度**：高（两条代码路径 + 探针实测）；工作区状态：`point_t::operator<` 已按 `(band, x, y)` 重写（修掉了 UB，见 `AUDIT_RUNTIME.md` F-03），但**两套顺序不一致与同 key 覆盖仍然存在**——用新比较器跑上面的例子，`World(300,10)` 仍在 `Hello(10,20)` 之前（band 1 vs band 2），MISS 依旧。

### A-04（P2）结果上限 off-by-one：声明的 `_max_return_obj_ct = 1800`，实际最多输出/返回 1801

- **位置**：`libop/image/ImageSearchService.cpp:459-474`（`OcrEx`：先 `++find_ct` 再 `if (find_ct > _max_return_obj_ct) break;`）、`libop/image/ImageSearchAlgorithms.cpp:1026-1027`（`FindStrEx` 同型）、`:952`（`FindColorBlockEx` 同型）；上限定义 `ImageSearchAlgorithms.h:68`。
- **证据**：`find_ct` 从 0 开始，追加第 1801 条后 `1801 > 1800` 才 break，返回 1801；`retstr` 也含 1801 段。
- **影响**：与文档/常量语义不符，边界上一类“明明有上限却多一条”，下游若按 1800 分配/校验会错位。目前只在超大 OCR 结果（>1800 框）时触发，实际很难遇到 → P2。
- **建议**：改成 `if (find_ct >= _max_return_obj_ct) break;`（先判断再追加），三处一起改。
- **置信度**：高。

### A-05（P2）`OcrFromFile`/`OcrAutoFromFile` 不检查 `Image::read` 失败：读图失败 → 空帧 OCR，HEAD 上仍返回“成功”

- **位置**：`libop/image/ImageSearchService.cpp:520-530`、`:532-543`（`_src.read(fullpath.data()); return OCR(...)`，返回值被丢弃）；`libop/image/Image.h:174-187`（`read` 失败走 `clear()`，`_src` 变空）、`:107-110`（`IsSupportedImageFormat` 只接受 1/3/4 字节/像素）。
- **证据**：`Image::read` 先 `clear()`（`width=height=0`）再 `Load`，失败返回 false；`ImageSearchService` 不看返回值，直接把这个空 `_src` 交给 `OCR`。`HttpOcrService::ocr` 对 `w<=0` 返回 `-1`，而 HEAD 的 `OCR` 无条件 `return 1`（`:262`）→ 调用方拿到“成功 + 空串”。
- **影响**：很具体的一类输入会踩到——**1bpp/4bpp/16bpp 位图**（`IsSupportedImageFormat` 直接拒绝）、损坏文件、超长路径；`OcrFromFile` 于是静默返回空串而不是报错，自动化脚本会把“图没读到”当成“图里没字”。（注意：我在初稿里以为会复用上一帧，核对 `Image::read` 后确认是**空帧**而不是旧帧，特此修正。）
- **建议**：`if (!_src.read(...)) { retstr.clear(); return 0; }`；`Op::OcrFromFile` 目前是 `void`，至少让 `ImageSearchService::OcrFromFile` 把 0 传出去，并在文档标注“不支持 1/4/16bpp 图”。
- **置信度**：高；工作区状态：`OCR` 已加 `ocr_ret < 0 → return 0`（F-04 修复），但 `read` 失败仍未判断，即“读图失败”与“OCR 失败”仍会被合并成同一个 0。

### A-06（P2）`Op::FindStr` 失败路径预置 `ret = 0`，与“第 0 个模式命中”撞车

- **位置**：`libop/op/OpOcr.cpp:41-51`（`:45` `internal::set_result(ret, 0L);`，仅当 `with_captured_region` 真的进了 lambda 才会被覆盖）；`libop/image/ImageSearchAlgorithms.cpp:964-989`（成功返回**模式下标 0..n-1**，失败返回 -1）。
- **证据**：未绑定窗口 / 捕获失败 / 区域非法时 `ret` 保持 0，而 `x,y` 是 -1/-1；`OpFindStr`（`op_c_api.cpp:1091-1098`）把同一个 0 透出。
- **影响**：调用方若用 `ret == 0` 判失败，会在“第 0 个模式恰好命中”时误判；若用 `ret >= 0` 判成功，会在捕获失败时误判成功并去读 (-1,-1)。
- **建议**：预置 -1（与失败语义一致），或把返回码改成明确的 `1/0` + 命中的模式下标另走 out 参数；文档补一句。
- **置信度**：高（pre-existing，但在本次 OCR 家族 API 一致性范围内）。

### A-07（P2）`str2binaryfbk` 在 `@` 背景色分支完全忽略 `sim`

- **位置**：`libop/image/ImageSearchService.cpp:394-407`。
- **证据**：前景色分支会按 `sim` 给缺省 `df` 赋 `sim_to_point_color_diff(sim)`；`else`（`@` 开头，背景色反选）直接把 `colors` 交给 `bgr2binarybk`，`sim` 全程未用；`bgr2binarybk` 内部只在 `bk_colors.empty()` 时走灰自动二值化，否则用 `bk.df`（通常 000000）。
- **影响**：`FindLine`/`FindColorBlock`/`FindColorBlockEx` 用 `@RRGGBB` 时，`sim` 完全是废参数（既不报错也不生效），与 `doc/ocr.md:47`（「`sim` 在未显式给出色差时换算为隐式通道容差」）不符。
- **建议**：背景色分支同样对缺省 `df` 应用 `sim` 换算；或在 `str2binaryfbk` 入口统一做 `sim` 补 df，`bgr2binary`/`bgr2binarybk` 只吃已备好的 `colors`。
- **置信度**：高（pre-existing；字库删除后 `FindLine` 是这条路径的主要消费者，影响面变大）。

### A-08（P2）`BitFunctions.h` 已成纯死代码，`ImageSearchAlgorithms.h:9` 仍在 include

- **位置**：`libop/image/BitFunctions.h`（全文） + `libop/image/ImageSearchAlgorithms.h:9`。
- **证据**：`GET_BIT`/`SET_BIT`/`get_bit_count` 在 HEAD 中**零使用者**；d00f837 中有 5 处（`full_match`/`part_match`/`GetWordPreview`/`Dictionary.h`），全部随点阵字库删除。`#include "../image/BitFunctions.h"` 是删除后遗留。
- **影响**：无功能影响；但会误导后续维护者以为还有位图压缩逻辑，且是“删干净了没有”的显性反例。
- **建议**：删除 `BitFunctions.h` 与该 include（`ImageSearchAlgorithms.h:9`）。
- **置信度**：高（`git grep` 双向核对）。

### A-09（P2）其它死代码（多为 pre-existing，一并登记）

| 符号 | 位置 | 状态 |
| --- | --- | --- |
| `ImageView`（整文件） | `libop/image/ImageView.h` | 无任何 include/引用（d00f837 亦然） |
| `Connectivity(ImageBin,ImageBin)` | `ImageSearchAlgorithms.cpp:231`（声明缺失，仅定义） | 无调用者 |
| `extractConnectivity` | `ImageSearchAlgorithms.cpp:257` + `.h:54` | 无调用者 |
| `gen_next` | `ImageSearchAlgorithms.cpp:182` + `.h:45` | 无调用者 |
| `HEX2INT` | `ImageSearchAlgorithms.h:18` | 无使用者（原本给 `Dictionary.h` 用） |
| `ImageSearchService::str2colors` | `.cpp:304` + `.h:92` | 无调用者（d00f837 亦无） |
| `_dx` / `_dy` 成员 | `ImageSearchAlgorithms.h:164` | 只有构造函数清零（`.cpp:303`），全仓无赋值点 → 所有 `+ _dx/+ _dy` 恒等于 0 的噪声 |

- **建议**：`ImageView.h`、`Connectivity`、`extractConnectivity`、`gen_next`、`HEX2INT`、`str2colors` 直接删；`_dx/_dy` 要么接上真实来源，要么删掉并简化坐标表达式。
- **置信度**：高（逐符号 grep 计数）。
- **备注**：`region_sum`/`record_sum`/`_sum`/`get_bk_color`/`_gray` **不是**死代码（仍被 `FindColorBlock*`、`FindPic*`、`bgr2binarybk` 使用），不要误删。

### A-10（P2）文档把被删 C API 记成“28 个”，实测三层均 27

- **位置**：`doc/cpp-only/BUILD_VS.md:177`、`doc/cpp-only/PROGRESS.md:119`、`:134`、`:183`、`doc/cpp-only/TWINKLE_SYNC.md:8`、`:97`。
- **证据**（脚本从 diff 精确统计）：

  ```
  libop/c_api/op_c_api.cpp 删除的 OP_CALL 实现     : 27
  include/libop.h        删除的 void 方法声明      : 27
  libop/com/op.idl       删除的 [id(n)]            : 27（200,201,220-244）
  libop/com/OpAutomation.h 删除的 STDMETHOD(...)   : 27
  ```

  27 个名字双向核对一致（`SetDict/GetDict/SetMemDict/UseDict/AddDict/SaveDict/ClearDict/GetDictCount/GetNowDict/
  SetBinaryPreprocess/GetBinaryPreprocess/FetchWord/FetchWordEx/ExtractWordRects/ExtractWordRectsEx/FetchWords/
  FetchWordsEx/FetchWordsByRects/GetBinaryPreview/GetWordPreview/CheckWordDict/NormalizeWordDict/RenameWordDict/
  GetWordsNoDict/GetWordResultCount/GetWordResultPos/GetWordResultStr`），HEAD 中 0 残留。
- **影响**：纯文档误差，但这是“删干净了没有”的核对基准；数字对不上会让人怀疑验证本身。
- **同类残留**：`BUILD_VS.md:184`/`PROGRESS.md:134,183` 写“C API 导出 198 个”，实测两架构均为 **200**（DPI 提交 `f8644fe` 新增的 `OpSetProcessDpiAware`/`OpGetProcessDpiAware` 未计入），验证记录未随代码更新。
- **建议**：统一改成 27，导出数改成 200（并注明“截至 cdb2eb3”），在 BUILD_VS 里写清“x64 导出名核对（x86 需去装饰，见 A-02）”。
- **置信度**：高。

### A-11（P2）公开头注释仍承诺已被删除的语义

- **位置与内容（HEAD）**：
  - `include/libop.h:462`「识别…内**符合 color_format** 的字符串」（`color` 在 `ImageSearchService.cpp:250` 已是 `(void)color`）。
  - `include/libop.h:468`、`:471` 同样“符合 color_format”。
  - `include/libop.h:474`「`OcrAuto` … **自动二值化**，而无需指定颜色」——二值化已不在 OCR 路径上。
  - `include/libop.h:477` 参数名 `color_format`（实现完全忽略）。
  - `doc/cpp-only/API_CHANGES.md:199` 在 HEAD 上写「返回值由 HTTP 分支原来的 `0` 改为 `1`（**成功语义**）」——HEAD 的实现是无条件 `return 1`，HTTP 失败也算“成功”（`ImageSearchService.cpp:262`）。
  - `doc/ocr.md:33` 在 HEAD 上写「未显式配置时按 **tesseract** 后端默认端点 `http://127.0.0.1:8080/api/v1/ocr`」，而代码默认是 **paddle_ncnn :8082**（`libop/ocr/OcrService.cpp:15` + `:38-46`，`tests/test_support.cpp` 里的注释也明说“不是 tesseract :8080”）。
- **影响**：`color`/`sim` 参数现在含义已变（`sim` 从“像素相似度”变成“置信度阈值”），注释不更新会让调用方继续传颜色描述并期待过滤效果——**白字/深底场景的识别率会明显下降**，而用户以为自己在过滤颜色。
- **建议**：改注释为「`color` 仅为签名兼容，已忽略；`sim` 是置信度阈值（0-1）」；`OcrAuto` 去掉“自动二值化”。
- **置信度**：高；工作区状态：`doc/ocr.md:33` 与 `API_CHANGES.md:199` 已被并行修复线改好，`include/libop.h` 未改。

### A-12（P1）测试缺口：本次删改涉及的关键路径在 CI 里等于零覆盖

- **位置与证据（实测运行 `bin/x64/op_test.exe`）**：
  1. **FindLine 零用例**：`grep -r FindLine tests/` 无任何命中；而 `FindLine` 是“删字库后仍保留”的唯一 OCR 邻接能力，且 HEAD 上对空二值图会输出伪造的 `"0,0"`（`ImageSearchAlgorithms.cpp:1039-1077`）。
  2. **OcrEx/FindStr/FindStrEx 只打印不断言**：`tests/ocr_test.cpp:31-35`、`:74-79`、`:81-86` 只 `wcout`，没有任何 `EXPECT_*`；`OcrEx` 的 `x,y,text|` 格式、`_x1/_y1` 偏移、`sim` 置信度过滤、上限截断全部无断言。
  3. **OCR 用例在无服务时整体 SKIP**：`tests/test_support.cpp:762` 的健康检查让 15 个用例全部跳过；实测 `15 tests ran / 0 PASSED / 15 SKIPPED`。也就是说**HTTP OCR 全链路默认无覆盖**，只有 3 个断言用例（`OcrFromFile*`、`OcrAutoFromGenerated…`）在被跳过的同一批里。
  4. **没有“Ocr 出来的串能被 FindStr 找到”的一致性用例**——正是 A-03 的照妖镜。
  5. **COM 零用例**：`tests/op_test.vcxproj:161` 专门编译了 MIDL 生成的 `op_i.c`（注释说明“提供 IID/CLSID”），但全仓测试没有一处 `CoCreateInstance`/`GetIDsOfNames`/`IDispatch` → dispid 空洞、注册表注册、删除方法的行为（A-01）无人验证，那行编译配置目前是纯开销。
  6. **删除符号无守护**：没有编译期/链接期断言或导出表核对脚本证明那 27 个符号“保持删除”；BUILD_VS 的核对是一次性手工 `dumpbin`，且方法在 x86 上无效（A-02）。
- **建议**（按性价比排序）：① 把 OCR 响应解析抽成 `parse_ocr_response(const std::string&, vocr_rec_t&)` 纯函数，用固定 JSON 做表驱动用例（覆盖 A-03/A-13/A-04）；② `OcrEx`/`FindStr` 用例补断言（格式、偏移、上限）；③ 加 `FindLine` 三态用例（空帧/单色/一条直线）；④ 加一条 late-binding COM 用例断言删除名返回 `DISP_E_UNKNOWNNAME`；⑤ 加脚本把 `op_c_api.h` 声明集合与 `dumpbin` 导出集合对比（x86 先去装饰）。
- **置信度**：高（实测 + grep）。

### A-13（P2）`OcrEx` 文本含 `,`/`|` 无转义，破坏 `x,y,text|` 协议

- **位置**：`libop/image/ImageSearchService.cpp:461-466`（`left_top.x, left_top.y, it.text` 直接拼接、`|` 分隔）；`FindStrEx` 输出 `i,x,y|` 无文本，风险较低。
- **证据**：HTTP OCR 的文本是自由文本（标点、竖线、逗号都可能出现，例如表格里的 `a|b`、价格 `1,234`）；旧点阵字库是单字，几乎不会出现这两种字符，所以删字库后风险显著上升。
- **影响**：上层按 `,`/`|` 切分 `OcrEx` 输出时会错位，把文本片段当成坐标。
- **建议**：输出 JSON（与 `YoloDetect` 保持一致）或定义转义；至少在 `doc/ocr.md` 写清“text 中不允许出现 `,`/`|`，出现时行为未定义”。
- **置信度**：中（取决于 OCR 服务返回内容）。

### A-14（P2）`SetOcrEngine` 是实例方法却修改进程级单例：跨 Op、跨测试互相污染

- **位置**：`libop/op/OpOcr.cpp:13-20`（直接 `HttpOcrService::getInstance()->init(...)`）、`libop/ocr/OcrService.cpp:66-69`（magic static 单例）。
- **证据**：本机跑测试时可见污染链——`OcrFixture.SetOcrEngineBaseUrl` 把端点改成 `8080` 之后，后续每个用例的 `TearDown` 打印的都是 `8080`（原本默认 `8082`）。`Op` 之间同理：A 实例 `SetOcrEngine` 会影响 B 实例的 `Ocr`。
- **影响**：多 Op/多线程宿主里“改自己的 OCR 端点”会改到别人；测试之间不能独立。
- **建议**：把端点/超时放进 `OpContext`（实例级），或改名 `SetGlobalOcrEngine` 并在文档明确全局语义。
- **置信度**：高（与 `AUDIT_RUNTIME.md` F-05 的串行化问题是同一根源的不同表现：F-05 讲性能/阻塞，本条讲作用域语义）。

### A-15（P2）`OCR` 不设结果上限，与 `OcrEx` 的 1800 截断语义不一致

- **位置**：`libop/image/ImageSearchService.cpp:256-262`（`OCR` 遍历全部 `res`）vs `:456-474`（`OcrEx` 受 `_max_return_obj_ct` 限制）。
- **影响**：同一张图，`Ocr` 与 `OcrEx` 覆盖的文本范围可能不同（>1800 框时），文档未说明；调用方用 `Ocr` 校验、用 `OcrEx` 取坐标时会出现“文字有但坐标没有”。
- **建议**：两处统一（要么都截断，要么 `Ocr` 只拼 `OcrEx` 会返回的那些框）；并在 `doc/ocr.md` 写明上限语义。
- **置信度**：高（代码路径明确，触发条件是超大 OCR 结果）。

## 3. 已核对确认“没问题”的点（避免误判为遗漏）

- **删符号一致性（要点 3）**：27 个被删 C API 在 `include/op_c_api.h`、`libop/c_api/op_c_api.cpp`、`include/libop.h`、`libop/com/op.idl`、`libop/com/OpAutomation.h/.cpp` 六处**完全一致、零残留、无半删**（脚本双向核对）；C API 没有 `.def`，导出完全由 `__declspec(dllexport)` 决定，因此头/实现/导出三者天然一致（x86 名字装饰问题另见 A-02）。
- **头 ↔ 实现 ↔ 导出**：`op_c_api.h` 189 条声明全部有实现（鼠标/OpenCV 系列由 `OP_MOUSE_RET`/`OP_CV_FILE_RET` 宏生成，regex 统计会漏，已人工确认）；x64 实测 200 个导出（含 `HookExport.cpp` 的 hook 导出与 `OpUnbindWindow` 兼容别名——后者是 d00f837 就有的未声明别名，非本次回归）。
- **保留函数的异常安全**：C API 全部经 `call_int/call_ret/call_string/call_intptr` `try/catch(...)` 包裹，失败返回 0/`L""`；`call_string` 写入 `handle->string_result` 后返回其 `c_str()`，生命周期为“到本 handle 下一次调用”，与旧行为一致（文档化问题见 `AUDIT_RUNTIME.md` F-16）。
- **`SetBinaryPreprocess` 删除后的默认值**：旧构造是 `_binary_preprocess_mode = 0`（d00f837 `ImageSearchService` 构造），且 `FindLine` 本来走的是 `str2binaryfbk` 而非 `str2pointbinaryfbk`（不应用预处理）→ `doc/cpp-only/API_CHANGES.md:201-202`「等价于旧 `SetBinaryPreprocess(0,...)` 初始状态」**成立**。
- **`str2binaryfbk`/`bgr2binary`/`bgr2binarybk` 自洽性**：分支判定（`str2colordfs` 返回 0=前景 / 1=背景）与文档一致；两者都先 `_binary.clear()` 并在 `_src.empty()` 时直接返回；`FindColorBlock*` 有 `_binary.empty()` 与尺寸校验保护；`FindLine` 在 HEAD 上缺该保护（工作区已补 `if (_binary.empty()) return 0;`）。
- **极小帧 / 单像素边界**：`1×1` 帧时 `FindLine` 的 `h = (int)sqrt(1+1)+2 = 3`，`_sum` 为 360×3，唯一像素只在 `d=0` 累加 → 返回 `1` 与 `"0,0"`，不会越界；一般情形 `d ≤ sqrt((w-1)²+(h-1)²) < sqrt(w²+h²) < h`，`assert(d <= h)`（`ImageSearchAlgorithms.cpp:1057`）恒成立（release 下 assert 为空，但结论一致）。也就是说**极小帧本身不崩**，问题只在“空帧也输出 `0,0`”（AUDIT_RUNTIME F-09 / 本报告 A-12 第①条）。
- **`HttpOcrService::ocr` 的入参保护**：`data==nullptr || w<=0 || h<=0 || bpp∉{1,3,4}` 返回 -1，`>64MB` 返回 -2，`result` 每次先 `clear()` → 空帧/极小帧不会崩，只是失败码被上层吞掉（A-05）。
- **`_src` 与 `bpp=4` 的匹配**：`Image` 内部恒 4 字节/像素，`read()` 会把 8/24/32bpp 统一 `translate` 成 BGRA，`Image::ptr/at<color_t>` 也按 4 字节寻址 → `ocr(_src.pdata, w, h, 4, res)` 的 bpp 参数正确。
- **dispid 空洞**：200/201、220-244 保留空洞、其余 id 未重排（脚本列出 HEAD 全部 id，未发现被复用的空洞）；`version(1.0)` 未升的问题另见 A-01。
- **`OcrAutoFromFile`/`OcrAuto` 复用 `OCR`**：`OcrAuto` 走 `OCR(L"", ...)`，`FindLine` 与 `OCR` 共用同一个 `str2binaryfbk` 二值入口，注释「FindLine/FindColorBlock 共用」与实现一致。
- **构建状态**：`bin/x64`、`bin/x86` 产物齐全（`op_test.exe`/`cpp_call.exe`/两个 DLL），x64 全量跑 `ImageColorTest` 20/20 PASS；期间有并行 rebuild，产物一度被清空，非本报告结论依据。

## 4. 与 `AUDIT_RUNTIME.md` 的关系（排重）

- 我用独立证据**确认**了它的 F-03（`point_t` 非严格弱序，见 A-03 的探针输出）、F-04（`OCR` 恒返回 1，见 A-05/A-11）、F-06（`utf8→CP936→CP_ACP`，`OcrService.cpp:165`）、F-09（`FindLine` 输出伪造 `"0,0"`）。
- 本报告新增、且 `AUDIT_RUNTIME.md` 未覆盖的：**A-01**（COM dual vtable/IID）、**A-02**（x86 导出装饰）、**A-04**（上限 off-by-one）、**A-06**（FindStr ret 二义性）、**A-07**（背景色忽略 sim）、**A-08/A-09**（死代码清单）、**A-10**（28 vs 27）、**A-13/A-14/A-15**、以及 **A-03 中“两套顺序不一致/同 key 覆盖”** 这半边（F-03 只修了 UB）。
- 两报告对 `FindLine` 的描述不冲突：F-09 讲“HEAD 上会输出 `0,0`”，A-12 讲“没有任何用例”，互补。

## 5. 建议修复顺序

1. **A-02**（x86 导出名）——影响面明确、改动小（一个 `.def`），且能恢复导出核对的可靠性。
2. **A-01**（COM IID/版本）——决定要不要保早期绑定兼容；若确定“只支持 late binding”，至少要改文档 + 加用例。
3. **A-03 + A-15 + A-04**——一次性把“OCR 文本/坐标模型”收敛到一个函数，顺带统一上限语义；这是 `FindStr` 漏命中的根因。
4. **A-12**（测试）——先做 ①②（纯函数解析 + 断言），再补 FindLine/COM 用例；否则上面三条改完仍无回归网。
5. **A-05、A-06、A-07、A-11**——错误码/参数语义/注释，工作量小，一起提交。
6. **A-08、A-09、A-10、A-13、A-14**——清理与文档，可最后批量做。

## 附录 A：独立探针（不依赖 OCR 服务，`%TEMP%\op_point_audit\probe.cpp`，未入库）

```cpp
#include "libop/base/Types.h"   // /I <repo>，用 HEAD 的真实比较器
#include <cstdio>
#include <map>
#include <string>
using namespace op;

int main() {
    point_t a(0, 0), b(0, 8), c(0, 16);
    printf("a<b=%d b<a=%d\n", (int)(a < b), (int)(b < a));   // 0 0  → a~b
    printf("b<c=%d c<b=%d\n", (int)(b < c), (int)(c < b));   // 0 0  → b~c
    printf("a<c=%d c<a=%d\n", (int)(a < c), (int)(c < a));   // 1 0  → 但 a<c（非严格弱序）

    std::map<point_t, std::wstring> boxes;
    boxes[point_t(10, 100)] = L"OK";
    boxes[point_t(10, 104)] = L"Cancel";                     // 同 x、Δy=4
    printf("boxes.size()=%zu (expected 2)\n", boxes.size()); // 实测 1

    std::map<point_t, std::wstring> line;                    // 同一条视觉行、y 抖动 10px
    line[point_t(300, 10)] = L"World";
    line[point_t(10, 20)]  = L"Hello";
    std::wstring text; for (auto &it : line) text += it.second;
    printf("text=%ls\n", text.c_str());                      // 实测 WorldHello
    return 0;
}
```

实测输出（MSVC 14.41，`cl /std:c++17 /EHsc`）：

```
a<b=0 b<a=0
b<c=0 c<b=0
a<c=1 c<a=0
boxes.size()=1 (expected 2)
  (10,100)=Cancel
find((10,104)) -> hit
concatenated text = WorldHello
FindStr("HelloWorld") -> MISS
```

## 附录 B：核对脚本要点（可复现）

```powershell
# 1) 被删符号三处一致性（应为 27/27/27/27，且 HEAD 中 0 残留）
git diff d00f837..HEAD -- libop/c_api/op_c_api.cpp | Select-String '^-.*OP_CALL\s+(Op\w+)'
git diff d00f837..HEAD -- include/libop.h         | Select-String '^-\s+void\s+(\w+)'
git diff d00f837..HEAD -- libop/com/op.idl        | Select-String '^-.*\[id\((\d+)\)\]'
git diff d00f837..HEAD -- libop/com/OpAutomation.h| Select-String '^-.*STDMETHOD\((\w+)\)'

# 2) x86/x64 导出（看有无 stdcall 装饰）
dumpbin /exports bin\x86\op_c_api_x86.dll | findstr /i "OpOcr OpFindLine OpCreate"
dumpbin /exports bin\x64\op_c_api_x64.dll | findstr /i "OpOcr OpFindLine OpCreate"

# 3) 死代码
git grep -n "GET_BIT\|SET_BIT\|get_bit_count" HEAD -- libop
git grep -n "Connectivity\|extractConnectivity\|gen_next\|HEX2INT\|str2colors" HEAD -- libop

# 4) 测试覆盖
.\bin\x64\op_test.exe --gtest_filter="OcrFixture.*"   # 无服务：15 SKIPPED / 0 PASSED
```

---

**审计结论一句话**：27 个 API 的删除本身在 C++/C/COM/IDL 四处做得干净一致，真正的隐患不在“删没删干净”，而在**删了之后留下的三处契约断裂**——COM 早期绑定（A-01）、x86 导出名（A-02）、OCR 文本/坐标模型分裂（A-03），以及一层“看起来有 15 个用例、实际 0 个断言生效”的测试假象（A-12）。
