# Third-Party Notices

OP 的分发产物（`op_c_api_*.dll` / `op_*.dll`）静态链接了下列第三方库。发布二进制时请随包携带本文件与各自许可证文本。

| 组件 | 版本 | 许可证 | 用途 | 上游 |
| --- | --- | --- | --- | --- |
| OpenCV | 5.0.0 | Apache-2.0 | 模板匹配、图像预处理 | https://github.com/opencv/opencv |
| BlackBone | master（`scripts/bootstrap-deps.ps1` 拉取） | MIT | 进程内存 / 注入 | https://github.com/DarthTon/Blackbone |
| MinHook | vcpkg `minhook` | BSD-2-Clause | API Hook | https://github.com/TsudaKageyu/minhook |
| kiero2 | 仓库内 `3rd_party/kiero2` | MIT | D3D/OpenGL Hook 引导 | https://github.com/Rebzzel/kiero |
| DirectX-Headers | vcpkg `directx-headers` | MIT | D3D12 头文件 | https://github.com/microsoft/DirectX-Headers |
| GoogleTest | vcpkg `gtest` | BSD-3-Clause | 仅测试工程，不随运行时产物分发 | https://github.com/google/googletest |
| Windows App SDK / WinRT | 系统组件 | Microsoft 许可 | WGC 采集 | https://learn.microsoft.com/windows/apps/ |

说明：

- `op_c_api_*.dll` 的导入表只依赖系统 DLL；上述静态库已编入二进制。
- 发布时请把 `LICENSE`（本仓库 MIT）与本文件一起放入压缩包根目录；OpenCV 的
  `LICENSE`/`NOTICE` 可从其源码树 `LICENSE` 获取，MinHook/BlackBone/kiero 的许可证文本
  分别见各自仓库或 `build/_deps`、`3rd_party` 目录。
- 本文件不构成法律意见；对外分发前请按所在司法辖区复核。
