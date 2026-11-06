# 找图/找色合成基准（2026-11-05）

用途：为 [REVIEW_OPTIMIZATION.md](REVIEW_OPTIMIZATION.md) 的 O5（找图后端）、O20（逐像素 SIMD）、
O21（`FindPicEx`）提供可复现数据，避免凭直觉改匹配算法。

## 1. 方法与环境

- 机器：Windows，16 逻辑核；MSVC v143（14.44），x64 Release。
- 合成图：确定性伪随机 BGRA + 唯一模板（模板从图中裁切后原位粘回），命中位置固定在画面中心，
  因此每次都是**最坏情况全图扫描**。
- `findpic_cold`：每次调用前 `invalidate_frame_cache()`，包含灰度 + 积分图重建（模拟每帧新图）。
- `findpic_warm`：同一帧复用灰度/积分图缓存（`frameId` 不变）。
- `opencv_*`：`cv::cvtColor(BGRA2GRAY)` + `cv::matchTemplate(TM_CCOEFF_NORMED)` + `minMaxLoc`；
  `cold` 每次含灰度转换，`warm` 复用灰度。
- `findcolor`：颜色在图中不存在，强制整图扫描；分别测 1 色与 4 色。

复现：

```powershell
pwsh scripts/build.ps1 -Target test            # Release x64
pwsh scripts/bench.ps1                          # 结果打印并写入 build/bench-latest.log
```

## 2. 结果（单位 ms，mean / p50 / p95）

### 2.1 `FindPic`（自定义 SAD + 积分图）vs OpenCV 模板匹配

| 画面 | 模板 | FindPic cold | FindPic warm | OpenCV warm | OpenCV/FindPic |
| --- | --- | --- | --- | --- | --- |
| 1280×720 | 32 | 67.6 / 67.8 / 70.8 | 63.5 / 63.8 / 72.7 | 24.4 / 24.3 / 25.6 | **2.6×** |
| 1280×720 | 64 | 210.6 / 209.2 / 218.4 | 213.2 / 211.6 / 233.2 | 29.7 / 29.5 / 31.2 | **7.2×** |
| 1920×1080 | 32 | 139.1 / 138.9 / 142.3 | 135.3 / 131.2 / 167.0 | 51.3 / 51.4 / 53.2 | **2.6×** |
| 1920×1080 | 64 | 485.8 / 484.2 / 497.9 | 480.3 / 475.7 / 510.0 | 63.2 / 61.8 / 83.4 | **7.6×** |
| 2560×1440 | 32 | 256.7 / 255.2 / 263.6 | 247.0 / 245.8 / 270.6 | 93.5 / 93.3 / 95.2 | **2.6×** |
| 2560×1440 | 64 | 892.8 / 892.4 / 899.8 | 873.4 / 868.6 / 896.4 | 104.9 / 104.3 / 110.9 | **8.3×** |

### 2.2 `FindColor`

| 画面 | 颜色数 | mean / p50 / p95 |
| --- | --- | --- |
| 1280×720 | 1 | 1.2 / 1.2 / 1.3 |
| 1280×720 | 4 | 3.7 / 3.6 / 4.0 |
| 1920×1080 | 1 | 2.8 / 2.7 / 3.2 |
| 1920×1080 | 4 | 8.3 / 8.4 / 8.9 |
| 2560×1440 | 1 | 5.0 / 5.0 / 5.3 |
| 2560×1440 | 4 | 14.2 / 14.2 / 14.7 |

## 3. 结论

1. **`FindPic` 的主要成本是模板逐像素 SAD**：模板 32→64 px（面积 ×4）时耗时约 ×3.5–3.7，
   说明 `real_match` 的内层循环随模板面积增长。现有积分图预筛只挡“灰度总和差超阈值”的位置，
   随机/复杂画面上几乎不挡，未命中位置仍要比较到误差超阈值（约模板 10–15% 的像素）才退出。
2. **OpenCV `matchTemplate` 对模板尺寸不敏感**（1280×720 下 32→64 px 只从 24.4ms 到 29.7ms），
   64 px 模板时比自定义实现快 **7–8×**，32 px 时快 **2.6×**；大 ROI 时差距更大。
3. **预处理不是瓶颈**：cold 与 warm 的差在 8–19ms，只占 64 px 场景的 2–4%。
4. `FindColor` 成本为 `面积 × 颜色数`，1080p 单色约 2.8ms、4 色 8.3ms；1440p 4 色 14.2ms。
   对轮询场景可接受，SIMD 化的收益有限（O20 降为可选）。
5. 本基准是最坏情况（唯一模板、命中在画面中心）。真实场景命中随机分布，平均扫描量约为一半。

## 4. 对 REVIEW 项的决策

| 项 | 决策 | 理由 |
| --- | --- | --- |
| O5 找图后端 | **不直接改 `FindPic` 默认语义**；建议调用方对大模板（≥48px）或大 ROI 改用已有 `OpCvMatchTemplate` / `OpCvMatchAnyTemplate`（已支持 strip、多尺度、mask、score） | 两者相似度口径不同（`FindPic` 的 `sim` 是 SAD 比例，OpenCV 是归一化相关），切换需黄金用例并可能改变命中点 |
| O20 逐像素 SIMD | **降级为可选** | `FindColor` 已 <15ms；`FindPic` 的真正瓶颈是算法而非字节级优化 |
| O21 `FindPicEx` | **暂缓** | 只在“同屏大量命中”时才有收益；当前基准未覆盖，且语义等价性改造需要额外黄金用例 |
| 新增：`FindPic` 快速路径 | 若后续确有需求，建议**新增 opt-in API**（如 `OpFindPicEx2` 带 `backend` 参数），保留旧 API 行为 | 避免破坏 Twinkle 现有 `sim`/命中点预期 |

## 5. 已知限制

- 合成图不含透明/掩码模板、缩放、低对比度、重复纹理；这些场景需要真实素材后再补。
- 只测了 x64 Release；x86 与 Debug 的绝对值不同但结论方向一致。
- 基准本身 50s 左右，未加入默认 CI 门禁；可用 `pwsh scripts/bench.ps1` 手动跑或作为独立 job。

## 6. 用 OpenCV 替换 `FindPic` 的可行性评估（2026-11-06）

**结论：能替换，但不建议直接替换默认实现。** 建议按场景选择：

### 6.1 不能 drop-in 的语义点

| 语义点 | `FindPic`（现） | OpenCV 方案 | 处理方式 |
| --- | --- | --- | --- |
| 相似度 | `sim` 经 `0.5+sim/2` 映射成 SAD 比例 | `threshold` 是归一化相关/SQDIFF | 需重新标定；同一参数命中不同 |
| 透明模板 | 四角同色 → 只比前景点 | `mask` 匹配 | 需把“四角启发式”换成显式 mask |
| `delta_color` | 仅透明模板生效 | 无对应概念 | 需明确禁用或转成 mask |
| 多模板 | `files` 以 `|` 分隔，返回模板下标 | `MatchAny/AllTemplates` 返回名字 | 返回语义要改 |
| 方向 | `dir` 0–7 | `SearchDirection` 只有 4 向 | 需扩展或映射 |
| 返回上限 | 1800 | 由调用方控制 | 调用方处理 |
| 同帧缓存 | 灰度+积分图跨调用复用 | 每次 `cvtColor`（可自行缓存） | 可加灰度缓存 |

### 6.2 两条落地路线

- **路线 A（推荐，今天可用）**：调用方在“大模板/大 ROI”场景改用已有 `OpCvLoadTemplate` +
  `OpCvMatchTemplate`（或 `OpCvMatchAnyTemplate`），保留 `FindPic` 处理小模板/兼容路径。
  Twinkle 已封装 `CvMatchTemplate`，只需在调用处按模板尺寸或场景切换。
- **路线 B（ops 内部改）**：新增 opt-in API（例如 `OpFindPicEx2(..., backend, color_mode, threshold)`），
  内部走 OpenCV，`FindPic` 行为不变；需要补黄金用例锁命中点与阈值。
- **不推荐 C**：直接把 `FindPic` 改成 OpenCV——会改变所有现有调用方的命中结果与失败语义，
  且透明模板/`delta_color` 场景需要重做。

### 6.3 彩色/灰色按钮的区分

见 [API_CHANGES.md §10](API_CHANGES.md)：**必须用 `color_mode=Color`**；灰度模式下
“同亮度彩色 vs 灰色”数学上不可区分（已用 `OpenCvTest.ColorModeDistinguishesGrayFromColored` 固化）。
