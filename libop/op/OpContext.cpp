#include "OpContext.h"

#include "base/Environment.h"

#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

namespace {

std::wstring current_directory() {
    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    if (!ec)
        return path.wstring();

    const DWORD required = ::GetCurrentDirectoryW(0, nullptr);
    if (required == 0)
        return L"";

    std::vector<wchar_t> buffer(required, L'\0');
    const DWORD copied = ::GetCurrentDirectoryW(required, buffer.data());
    if (copied == 0 || copied >= required)
        return L"";

    return std::wstring(buffer.data(), copied);
}

} // namespace

namespace {
// DPI 感知是进程级、一次性的：SetProcessDPIAware() 必须在创建任何窗口/Op 之前
// 调用，且之后不可撤销。这里用 mutex 保护“配置 + 锁定”两个状态，避免多线程
// 首次创建 Op 时把刚设置的值丢掉。
std::mutex g_dpi_mutex;
bool g_dpi_aware_enabled = false; // 默认不修改宿主进程的 DPI 感知
bool g_dpi_aware_locked = false;
} // namespace

namespace op::internal {

bool set_process_dpi_aware(bool enable) {
    std::lock_guard<std::mutex> lock(g_dpi_mutex);
    if (g_dpi_aware_locked) {
        return false;
    }
    g_dpi_aware_enabled = enable;
    return true;
}

bool process_dpi_aware_enabled() {
    std::lock_guard<std::mutex> lock(g_dpi_mutex);
    return g_dpi_aware_enabled;
}

void apply_process_dpi_aware_once() {
    std::lock_guard<std::mutex> lock(g_dpi_mutex);
    if (g_dpi_aware_locked) {
        return;
    }
    g_dpi_aware_locked = true;
    if (g_dpi_aware_enabled) {
        ::SetProcessDPIAware();
    }
}

} // namespace op::internal

op::internal::OpContext::OpContext(int client_id) : id(client_id) {
    // 默认不改变进程 DPI 感知；需要物理像素的宿主必须在第一次 OpCreate 之前
    // 调用 OpSetProcessDpiAware(1)。否则由宿主通过 manifest / API 自行决定。
    apply_process_dpi_aware_once();

    // 初始化目录
    curr_path = current_directory();
    image_proc._curr_path = curr_path;

    // 初始化键码表
    vkmap[L"back"] = VK_BACK;
    vkmap[L"ctrl"] = VK_CONTROL;
    vkmap[L"lctrl"] = VK_LCONTROL;
    vkmap[L"rctrl"] = VK_RCONTROL;
    vkmap[L"alt"] = VK_MENU;
    vkmap[L"lalt"] = VK_LMENU;
    vkmap[L"ralt"] = VK_RMENU;
    vkmap[L"shift"] = VK_SHIFT;
    vkmap[L"lshift"] = VK_LSHIFT;
    vkmap[L"rshift"] = VK_RSHIFT;
    vkmap[L"win"] = VK_LWIN;
    vkmap[L"lwin"] = VK_LWIN;
    vkmap[L"rwin"] = VK_RWIN;
    vkmap[L"space"] = VK_SPACE;
    vkmap[L"cap"] = VK_CAPITAL;
    vkmap[L"tab"] = VK_TAB;
    vkmap[L"esc"] = VK_ESCAPE;
    vkmap[L"enter"] = VK_RETURN;
    vkmap[L"up"] = VK_UP;
    vkmap[L"down"] = VK_DOWN;
    vkmap[L"left"] = VK_LEFT;
    vkmap[L"right"] = VK_RIGHT;
    vkmap[L"menu"] = VK_APPS;
    vkmap[L"print"] = VK_SNAPSHOT;
    vkmap[L"insert"] = VK_INSERT;
    vkmap[L"delete"] = VK_DELETE;
    vkmap[L"pause"] = VK_PAUSE;
    vkmap[L"scroll"] = VK_SCROLL;
    vkmap[L"home"] = VK_HOME;
    vkmap[L"end"] = VK_END;
    vkmap[L"pgup"] = VK_PRIOR;
    vkmap[L"pgdn"] = VK_NEXT;
    vkmap[L"f1"] = VK_F1;
    vkmap[L"f2"] = VK_F2;
    vkmap[L"f3"] = VK_F3;
    vkmap[L"f4"] = VK_F4;
    vkmap[L"f5"] = VK_F5;
    vkmap[L"f6"] = VK_F6;
    vkmap[L"f7"] = VK_F7;
    vkmap[L"f8"] = VK_F8;
    vkmap[L"f9"] = VK_F9;
    vkmap[L"f10"] = VK_F10;
    vkmap[L"f11"] = VK_F11;
    vkmap[L"f12"] = VK_F12;

    // Numpad keys
    vkmap[L"num0"] = VK_NUMPAD0;
    vkmap[L"num1"] = VK_NUMPAD1;
    vkmap[L"num2"] = VK_NUMPAD2;
    vkmap[L"num3"] = VK_NUMPAD3;
    vkmap[L"num4"] = VK_NUMPAD4;
    vkmap[L"num5"] = VK_NUMPAD5;
    vkmap[L"num6"] = VK_NUMPAD6;
    vkmap[L"num7"] = VK_NUMPAD7;
    vkmap[L"num8"] = VK_NUMPAD8;
    vkmap[L"num9"] = VK_NUMPAD9;
    vkmap[L"numlock"] = VK_NUMLOCK;
    vkmap[L"num."] = VK_DECIMAL;
    vkmap[L"num*"] = VK_MULTIPLY;
    vkmap[L"num+"] = VK_ADD;
    vkmap[L"num-"] = VK_SUBTRACT;
    vkmap[L"num/"] = VK_DIVIDE;

    opPath = RuntimeEnvironment::getBasePath();
}
