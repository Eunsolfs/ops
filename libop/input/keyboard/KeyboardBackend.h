#pragma once
#include "../../base/Types.h"

namespace op::input {

class KeyboardBackend {
  public:
    KeyboardBackend();

    virtual ~KeyboardBackend();

    virtual long Bind(HWND hwnd, long mode) = 0;

    virtual long UnBind();

    virtual long GetKeyState(long vk_code) = 0;

    virtual long KeyDown(long vk_code) = 0;

    // virtual long GetKeyState(long vk_code);

    virtual long KeyUp(long vk_code) = 0;

    virtual long WaitKey(long vk_code, unsigned long time_out) = 0;

    virtual long KeyPress(long vk_code) = 0;

    virtual long InputChar(wchar_t ch) = 0;

    // 批量输入；默认逐字符 InputChar，delay_ms>0 时字符间 Delay。
    virtual long InputString(const wchar_t *text, long delay_ms);

  protected:
    HWND _hwnd;
    int _mode;
};

} // namespace op::input
