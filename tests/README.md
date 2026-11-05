# OP 测试说明

`op_test`（GoogleTest）按环境依赖分三层，避免在开发者机器上抢焦点/移动鼠标：

| 层 | 用例 | 依赖 | 运行方式 |
| --- | --- | --- | --- |
| `core` | `UtilsTest`、`HandleCompatTest`、`AlgorithmTest`、`ImageColorTest`、`OpenCvTest`、`YoloTest` | 仅本进程/内存/临时文件；OpenCV 用例缺真实图时 SKIP | `pwsh scripts/test.ps1` |
| `desktop` | `WindowServiceTest`、`WgcTest`、`IntegrationTest` | 交互式桌面、可创建窗口；WGC 需 Win10 1903+ | `pwsh scripts/test.ps1 -Scope all`（另设 `OP_SKIP_WGC_TESTS` 可跳过） |
| `inject` | `MouseKeyTest` 的全局输入 / DX 模式用例 | 真实桌面 + 目标窗口；会调用 `SendInput` | 手动运行；CI/无头环境设 `OP_SKIP_GLOBAL_INPUT=1` |

常用命令：

```powershell
pwsh scripts/test.ps1                                   # core，x64
pwsh scripts/test.ps1 -Platform Win32                   # core，x86
pwsh scripts/test.ps1 -Scope all                        # 完整套件
pwsh scripts/test.ps1 -Filter "ImageColorTest.*"        # 指定用例
pwsh scripts/test.ps1 -AlsoDisabled                     # 含 DISABLED_（bench 等）
```

环境变量：

- `OP_SKIP_OCR_TESTS=1`：跳过 OCR（默认 core 集已设置）。
- `OP_SKIP_WGC_TESTS=1`：跳过快照/WGC 相关用例。
- `OP_OCR_URL`：OCR 服务地址，默认 `http://127.0.0.1:8082`。
- `OP_SKIP_GLOBAL_INPUT=1`：跳过会注入全局输入的用例（建议在远程/CI 桌面设置）。

测试数据：真实图片用例需要 `assets/`（不入库），缺失时用例会 SKIP 而不是失败；
需要覆盖时把图片放到仓库外的 `assets/` 或按 `tests/opencv_test.cpp` 中的候选路径放置。
