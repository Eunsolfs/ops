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

// 进程级 DPI 感知配置。SetProcessDPIAware 是进程级、一次性的，必须在本进程
// 第一次创建 OpContext 之前调用；创建过 Op 之后再设置不会生效。
// 返回 false 表示已经创建过 Op（或已被其他组件锁定）。
bool set_process_dpi_aware(bool enable);
bool process_dpi_aware_enabled();
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
};

} // namespace op::internal

#endif // OP_OP_CONTEXT_H_
