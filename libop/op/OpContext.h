#pragma once
#ifndef OP_OP_CONTEXT_H_
#define OP_OP_CONTEXT_H_

#include "binding/BindingSession.h"
#include "image/ImageSearchService.h"
#include "window/WindowService.h"

#include <map>
#include <string>
#include <vector>

namespace op::internal {

// 分类错误码，数值与 include/op_c_api.h 的 op_error_code 一一对应。
enum class OpErrorCode : long {
    ok = 0,
    invalid_handle = 1,
    invalid_arg = 2,
    not_bound = 3,
    capture = 4,
    not_found = 5,
    io = 6,
    backend = 7,
    internal = 8,
};

// 进程级 DPI 感知配置。SetProcessDPIAware 是进程级、一次性的，必须在本进程
// 第一次创建 OpContext 之前调用；创建过 Op 之后再设置不会生效。
// 返回 false 表示已经创建过 Op（或已被其他组件锁定）。
bool set_process_dpi_aware(bool enable);
bool process_dpi_aware_enabled();
// 查询进程/当前线程的实际 DPI 感知状态（不是“配置值”）。
bool process_dpi_aware_actual();
// 第一次创建 OpContext 时调用：按配置决定是否调用 SetProcessDPIAware()，
// 并锁定配置，之后不能再改。
void apply_process_dpi_aware_once();

struct OpContext {
    explicit OpContext(int client_id);

    // Windows API helpers
    WindowService window_service;
    // Window binding and capture/input state
    op::binding::BindingSession bkproc;
    // Image processing module
    op::image::ImageSearchService image_proc;
    // work path
    std::wstring curr_path;

    std::map<std::wstring, long> vkmap;
    std::vector<unsigned char> screenData;
    std::vector<unsigned char> screenDataBmp;
    std::wstring opPath;
    long screen_data_mode = 0;
    int id = 0;
    // 最近一次调用的分类错误码（0=成功）。由 C API 层在每次调用入口清零。
    long last_error_code = 0;
};

// 记录分类错误码；只在失败路径调用。
void set_last_error(OpContext *context, OpErrorCode code);

} // namespace op::internal

#endif // OP_OP_CONTEXT_H_
