# API 变更清单（点阵字库 OCR 移除）

> 关联：[README.md](README.md) 第 4.2 节
> 原则：字库相关能力全部删除；HTTP OCR 与通用图像识别保留。

## 1. 保留（HTTP OCR / 通用识别）

### C++ (`include/libop.h`)

- `SetOcrEngine`
- `Ocr`
- `OcrEx`
- `OcrAuto`
- `OcrFromFile`
- `OcrAutoFromFile`
- `FindStr`
- `FindStrEx`
- `FindLine`
- `SetYoloEngine` / `YoloDetect` / `YoloDetectFromFile`（与本次无关，保留）

### C (`include/op_c_api.h`)

- `OpSetOcrEngine`
- `OpOcr`
- `OpOcrEx`
- `OpOcrAuto`
- `OpOcrFromFile`
- `OpOcrAutoFromFile`
- `OpFindStr`
- `OpFindStrEx`
- `OpFindLine`

### COM (`libop/com/op.idl`)

- `[id(202)] Ocr`
- `[id(203)] OcrEx`
- `[id(204)] FindStr`
- `[id(205)] FindStrEx`
- `[id(206)] OcrAuto`
- `[id(207)] OcrFromFile`
- `[id(208)] OcrAutoFromFile`
- `[id(209)] FindLine`
- `[id(210)] SetOcrEngine`

## 2. 删除（字库 / 点阵 OCR）

### 2.1 C++ 接口（`include/libop.h`）

字库管理：

- `SetDict`
- `GetDict`
- `SetMemDict`
- `UseDict`
- `AddDict`
- `SaveDict`
- `ClearDict`
- `GetDictCount`
- `GetNowDict`

二值预处理：

- `SetBinaryPreprocess`
- `GetBinaryPreprocess`

取字 / 词块 / 预览 / 字库工具：

- `FetchWord`
- `FetchWordEx`
- `ExtractWordRects`
- `ExtractWordRectsEx`
- `FetchWords`
- `FetchWordsEx`
- `FetchWordsByRects`
- `GetBinaryPreview`
- `GetWordPreview`
- `CheckWordDict`
- `NormalizeWordDict`
- `RenameWordDict`
- `GetWordsNoDict`
- `GetWordResultCount`
- `GetWordResultPos`
- `GetWordResultStr`

### 2.2 C API（`include/op_c_api.h` + `libop/c_api/op_c_api.cpp`）

- `OpSetDict`
- `OpGetDict`
- `OpSetMemDict`
- `OpUseDict`
- `OpAddDict`
- `OpSaveDict`
- `OpClearDict`
- `OpGetDictCount`
- `OpGetNowDict`
- `OpSetBinaryPreprocess`
- `OpGetBinaryPreprocess`
- `OpFetchWord`
- `OpFetchWordEx`
- `OpExtractWordRects`
- `OpExtractWordRectsEx`
- `OpFetchWords`
- `OpFetchWordsEx`
- `OpFetchWordsByRects`
- `OpGetBinaryPreview`
- `OpGetWordPreview`
- `OpCheckWordDict`
- `OpNormalizeWordDict`
- `OpRenameWordDict`
- `OpGetWordsNoDict`
- `OpGetWordResultCount`
- `OpGetWordResultPos`
- `OpGetWordResultStr`

### 2.3 COM（`libop/com/op.idl` + `libop/com/OpAutomation.*`）

| dispid | 方法 |
| --- | --- |
| 200 | `SetDict` |
| 201 | `UseDict` |
| 220 | `SetMemDict` |
| 221 | `GetDict` |
| 222 | `AddDict` |
| 223 | `SaveDict` |
| 224 | `ClearDict` |
| 225 | `GetDictCount` |
| 226 | `GetNowDict` |
| 227 | `FetchWord` |
| 228 | `GetWordsNoDict` |
| 229 | `GetWordResultCount` |
| 230 | `GetWordResultPos` |
| 231 | `GetWordResultStr` |
| 232 | `ExtractWordRects` |
| 233 | `FetchWords` |
| 234 | `GetBinaryPreview` |
| 235 | `GetWordPreview` |
| 236 | `CheckWordDict` |
| 237 | `FetchWordsByRects` |
| 238 | `FetchWordEx` |
| 239 | `ExtractWordRectsEx` |
| 240 | `FetchWordsEx` |
| 241 | `NormalizeWordDict` |
| 242 | `RenameWordDict` |
| 243 | `SetBinaryPreprocess` |
| 244 | `GetBinaryPreprocess` |

> dispid 保留空洞，不重排其余 id，避免影响未删除接口。

### 2.4 源文件

删除：

- `libop/ocr/Dictionary.h`
- `libop/ocr/TesseractOcr.h`
- `libop/ocr/TesseractOcr.cpp`

保留：

- `libop/ocr/OcrService.h`
- `libop/ocr/OcrService.cpp`（HTTP OCR 客户端）

### 2.5 实现层需要清理的位置

- `libop/op/OpOcr.cpp`：删除上述字库 API 的实现；`Ocr/OcrEx/FindStr/FindStrEx` 改为始终走 HTTP。
- `libop/image/ImageSearchService.h/.cpp`：
  - 删除 `Dictionary _private_dicts[]`、`_private_dict_overrides[]`、`_curr_idx`、`_binary_*` 状态。
  - 删除 `SetDict/GetDict/SetMemDict/UseDict/AddDict/SaveDict/ClearDict/GetDictCount/GetNowDict`。
  - 删除 `SetBinaryPreprocess/GetBinaryPreprocess`、`FetchWord*`、`ExtractWordRects*`、`GetBinaryPreview`、`GetWordPreview`、`CheckWordDict`、`NormalizeWordDict`、`RenameWordDict`。
  - 删除全局字库槽（`find_global_file_dict` / `store_global_file_dict` / `clear_global_file_dict`）。
  - `OCR/OcrEx/FindStr/FindStrEx` 去掉 `ActiveDict` 分支，统一调用 `HttpOcrService`。
  - 保留 `FindLine` 需要的二值化路径（`str2binaryfbk` 等），并给二值预处理使用固定默认值。
- `libop/image/ImageSearchAlgorithms.h/.cpp`：
  - 删除 `#include "../ocr/Dictionary.h"`。
  - 删除 `Ocr(Dictionary&, ...)`、`OcrEx(Dictionary&, ...)`、`bin_ocr`、`_bin_ocr`。
  - 保留 `FindStr/FindStrEx`（基于 `std::map<point_t, ocr_rec_t>`，供 HTTP OCR 结果使用）与 `FindLine`。
  - 清理只服务于字库的 `get_rois` / `bin_image_cut` 等（如仅 `FindLine` 不需要）。
- `libop/com/OpAutomation.h/.cpp`：删除上述 COM 方法声明/实现。
- `libop/c_api/op_c_api.cpp`：删除上述 C API 实现。
- `tests/image_color_test.cpp`、`tests/ocr_test.cpp`：删除字库用例；保留图像/颜色/HTTP OCR 用例。若整个文件以字库为主，可从测试工程排除并记录。
- 文档：`doc/ocr.md`、`README.md`、`README_EN.md`、`CLAUDE.md` 删除字库/点阵/Tesseract 描述，保留 HTTP OCR。

## 3. Twinkle 侧同步

文件：

- `Y:\sourcecode\cpp\Twinkle\modules\Twinkle.Background\include\OPController.h`
- `Y:\sourcecode\cpp\Twinkle\modules\Twinkle.Background\src\OPController.cpp`

删除与 2.2 对应的 `OPController` 成员函数（`SetDict`、`GetDict`、`SetMemDict`、`UseDict`、`AddDict`、`SaveDict`、`ClearDict`、`GetDictCount`、`GetNowDict`、`SetBinaryPreprocess`、`GetBinaryPreprocess`、`FetchWord*`、`ExtractWordRects*`、`FetchWords*`、`GetBinaryPreview`、`GetWordPreview`、`CheckWordDict`、`NormalizeWordDict`、`RenameWordDict`、`GetWordsNoDict`、`GetWordResult*`）。

保留：`Ocr`、`OcrEx`、`FindStr`、`FindStrEx`、`OcrAuto`、`OcrFromFile`、`OcrAutoFromFile`、`FindLine`。

同时把 Twinkle 使用的 `op_c_api.h` 更新为新仓库版本（`external/ops/include/op_c_api.h`），并重新复制新的 `op_c_api_x64.lib/.dll`、`op_x64.dll`。

## 4. 实现差异与行为说明

- `Ocr` / `OcrEx` / `FindStr` / `FindStrEx` 的 `color` 参数不再参与识别，仅保留签名兼容
  （内部 `(void)color`）；OCR 始终走 `HttpOcrService`。
- `ImageSearchService::OCR` 的返回值：HTTP 成功返回 `1`（即使没有文字），HTTP 失败/超时/解析失败返回 `0`
  （旧实现无条件返回 `1`，无法区分“后端不可用”和“空结果”）。C++/C/COM 三层都是 `void` 或字符串返回，不影响 ABI。
- `FindLine` 保留；它是独立的二值图直线检测，不依赖字库。二值预处理固定为默认值
  （关闭），等价于旧 `SetBinaryPreprocess(0, ...)` 的初始状态。无有效二值图时返回 `0` 且输出空串
  （旧实现会返回 `-1` 却输出伪造的 `"0,0"`）。
- 删除的 COM dispid 保留空洞（200/201、220-244），其余 id 不重排；**late binding** 客户端调用这些
  dispid 会得到 `DISP_E_MEMBERNOTFOUND`。
- **早期绑定兼容性（重要）**：`op.idl` 是 `dual` 接口，删除接口中段的方法会让后续方法的 vtable
  槽位整体前移。旧 early-binding 客户端（`#import` raw/vtable、VB6 `[ComImport]`）必须重新导入
  新的 typelib 并重新编译；否则不会报 `DISP_E_MEMBERNOTFOUND`，而是可能静默调用到错误方法。
  根治需要升 IID / typelib 版本或新增 `IOpAutomation2`（本次未做，属于后续兼容性决策）。
- `libop/ocr/OcrService.*`（HTTP 客户端）保留；`TesseractOcr.*`（本地模型）删除。

## 5. 新增 API（DPI 感知，2026-10-25）

新增（C++ / C 两层，COM 不新增）：

```cpp
static bool op::Op::SetProcessDpiAware(bool enable);
static bool op::Op::IsProcessDpiAware();
```

```c
OP_C_API int OP_CALL OpSetProcessDpiAware(int enable);
OP_C_API int OP_CALL OpGetProcessDpiAware(void);
```

- 进程级、一次性；必须在第一次 `OpCreate` / 构造 `op::Op` 之前调用。
- 默认 `0`：不调用 `SetProcessDPIAware()`，不修改宿主 DPI 感知。
- `1`：第一次创建 Op 时调用 `SetProcessDPIAware()`，OP 使用物理像素。
- 创建过 Op 后再调用返回 `0`，无法撤销。
- 详细说明见 [DPI.md](DPI.md)。

## 6. x86 导出名（GetProcAddress 注意）

- x64：`OpCreate` / `OpOcr` 等导出名无修饰。
- x86：C API 使用 `__stdcall`，导出名带修饰，例如 `_OpCreate@0`、`_OpOcr@32`、
  `_OpFindLine@32`。隐式链接请使用 `op_c_api_x86.lib`；`GetProcAddress` 必须传修饰名
  （或改用 `#pragma comment(linker, "/alternatename:...")` / 显式 `.def` 别名）。
  按 `OpOcr` 查找会返回 `NULL`。
