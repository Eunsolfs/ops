# OCR 实现说明（HTTP-only）

> 本文描述 OP 精简改造后的 OCR 实现。点阵字库 / 本地字库 / 内存字库引擎已删除。

## 1. 能力划分

改造后 OCR 相关的对外能力分成两类：

### 1.1 HTTP OCR（识别）

- `SetOcrEngine`：配置 OCR 服务地址与参数。
- `Ocr` / `OcrEx`：识别截图区域；`OcrEx` 额外返回每个文本块的坐标。
- `OcrAuto`：自动 OCR，不需要颜色描述。
- `OcrFromFile` / `OcrAutoFromFile`：从图片文件识别。
- `FindStr` / `FindStrEx`：在 HTTP OCR 结果上做字符串查找，返回命中的坐标。

以上接口统一由 `op::image::ImageSearchService` 调用 `op::ocr::HttpOcrService` 完成，
后者是进程内单例（内部加锁），负责把 BGRA 像素编码后 POST 给外部 OCR 服务并解析结果。

### 1.2 二值图能力（不依赖 OCR）

- `FindLine`：对指定颜色规则生成的二值图做 Hough 直线检测，输出 `角度,距离`；返回值是直线上的点数。
- `FindColorBlock` / `FindColorBlockEx`：在二值图上搜索指定大小的颜色块。

这些接口只需要颜色描述，与字库无关。二值预处理固定使用默认值（关闭），
即历史 `SetBinaryPreprocess(0, ...)` 的默认状态；相关设置接口已删除。

## 2. HTTP OCR 流程

1. 调用方给出截图区域，`ImageSearchService` 复用当前截图（`_src`，BGRA）。
2. 调用 `HttpOcrService::ocr(data, width, height, 4, result)`：
   - 按 `SetOcrEngine` 配置的端点/超时发送 HTTP 请求；
   - 未显式配置时默认 `paddle_ncnn` 端点 `http://127.0.0.1:8082/api/v1/ocr`；
   - `OP_OCR_BACKEND=tesseract` → `http://127.0.0.1:8080/api/v1/ocr`；
   - `OP_OCR_BACKEND=paddle` → `http://127.0.0.1:8081/api/v1/ocr`；
   - 环境变量 `OP_OCR_URL` / `OP_OCR_BACKEND` / `OP_OCR_TIMEOUT_MS` 可覆盖。
3. 解析返回的文本块：`ocr_rec_t { left_top, right_bottom, text, confidence }`。
4. `Ocr` / `OcrEx` 按 `sim` 过滤置信度并拼装输出；
   `FindStr` / `FindStrEx` 把结果按坐标排序后拼成文本，在文本中查找目标字符串，
   再把命中位置映射回对应文本块的坐标。

## 3. 二值化

颜色描述格式沿用 OP 约定：

- 普通颜色：`RRGGBB-DRDGDB`，多段用 `|` 分隔，每段可省略色差；
- 背景色：以 `@` 开头，表示按背景色反选；
- `sim` 在未显式给出色差时换算为隐式通道容差。

`str2binaryfbk(color, sim)` 负责：

1. 解析颜色描述为 `color_df_t` 列表；
2. 若为前景色描述，调用 `ImageSearchAlgorithms::bgr2binary` 生成二值图；
3. 若为背景色描述，调用 `ImageSearchAlgorithms::bgr2binarybk` 反选生成二值图。

`FindLine` / `FindColorBlock` / `FindColorBlockEx` 都只依赖这一步生成的二值图。

## 4. 相关文件

- `libop/ocr/OcrService.h/.cpp`：HTTP OCR 客户端（单例 + 互斥）。
- `libop/image/ImageSearchService.h/.cpp`：OCR / 二值图入口，颜色解析与截图复用。
- `libop/image/ImageSearchAlgorithms.h/.cpp`：二值化、直线检测、颜色/图像查找算法。
- `libop/op/OpOcr.cpp`：`op::Op` 层的 OCR 方法实现。
- `include/libop.h`、`include/op_c_api.h`、`libop/com/op.idl`：对外 API 声明。

> 已删除：`libop/ocr/Dictionary.h`、`libop/ocr/TesseractOcr.*`、字库管理
> （`SetDict`/`UseDict`/`AddDict`/`SaveDict`/`ClearDict`/...）、点阵取字与预览
> （`FetchWord*`/`ExtractWordRects*`/`GetBinaryPreview`/`GetWordPreview`/...）、
> 二值预处理配置（`SetBinaryPreprocess`/`GetBinaryPreprocess`）。
